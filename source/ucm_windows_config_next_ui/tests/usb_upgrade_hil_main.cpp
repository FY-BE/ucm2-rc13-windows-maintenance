#include "config_session.h"
#include "upgrade_package_check.h"
#include "usb_extended_wire_v2.h"
#include "usb_functionfs_transport.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTextStream>
#include <QThread>

#include <memory>

namespace {

QJsonObject operationObject(const ucm::ProductOperationResult &result)
{
    return {
        {QStringLiteral("ok"), result.ok},
        {QStringLiteral("outcome"), result.outcome},
        {QStringLiteral("message"), result.message},
        {QStringLiteral("fields"), result.fields}
    };
}

void emitReport(const QJsonObject &report)
{
    QTextStream(stdout)
        << QJsonDocument(report).toJson(QJsonDocument::Compact) << '\n';
}

quint64 transactionId()
{
    quint64 value = QRandomGenerator::global()->generate64();
    return value == 0U ? 1U : value;
}

bool terminalState(const ucm::ProductOperationResult &result, int state)
{
    return result.ok && result.fields.value(QStringLiteral("state")).toInt()
        == state;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    const int packageAt = arguments.indexOf(QStringLiteral("--package"));
    const bool execute = arguments.contains(QStringLiteral("--execute-real-usb"));
    const bool activate = arguments.contains(QStringLiteral("--activate"));
    QJsonObject report {
        {QStringLiteral("tool"), QStringLiteral("UCM_USB_UPGRADE_HIL_V1")},
        {QStringLiteral("wire_revision"), 9},
        {QStringLiteral("real_usb_write_requested"), execute && activate}
    };
    QJsonArray steps;
    auto fail = [&](int code, const QString &message) {
        report.insert(QStringLiteral("passed"), false);
        report.insert(QStringLiteral("error"), message);
        report.insert(QStringLiteral("steps"), steps);
        emitReport(report);
        return code;
    };

    if (!execute || !activate || packageAt < 0
        || packageAt + 1 >= arguments.size()) {
        return fail(2, QStringLiteral(
            "必须同时提供 --execute-real-usb、--activate 和 --package；未打开USB。"));
    }

    QFile packageFile(arguments.at(packageAt + 1));
    if (!packageFile.open(QIODevice::ReadOnly))
        return fail(2, QStringLiteral("无法只读打开升级包。"));
    UpgradePackageMetadata packageMetadata;
    QString error;
    if (!readUpgradePackageMetadata(packageFile, &packageMetadata, &error))
        return fail(2, error);
    report.insert(QStringLiteral("target_build_id"),
                  QString::fromUtf8(packageMetadata.targetBuildId));
    report.insert(QStringLiteral("required_build_id"),
                  QString::fromUtf8(packageMetadata.requiredBuildId));
    report.insert(QStringLiteral("device_model"),
                  QString::fromUtf8(packageMetadata.deviceModel));
    report.insert(QStringLiteral("package_version"),
                  int(packageMetadata.packageVersion));

    ucm::ConfigurationSession session(
        std::make_unique<ucm::UsbFunctionfsTransport>());
    ucm::TransportInfo transport = session.transportInfo();
    for (int attempt = 0;
         (!transport.realUsbOpened || !transport.armReceiverContacted)
             && attempt < 40;
         ++attempt) {
        QThread::msleep(250U);
        session.reconnectTransport();
        transport = session.transportInfo();
    }
    report.insert(QStringLiteral("real_usb_opened"), transport.realUsbOpened);
    report.insert(QStringLiteral("arm_receiver_contacted"),
                  transport.armReceiverContacted);
    if (!transport.realUsbOpened || !transport.armReceiverContacted)
        return fail(3, QStringLiteral("真实USB revision 9握手失败。"));

    ucm::UsbExtendedDiscoveryV2 discovery = session.usbExtendedDiscovery();
    const QByteArray activeBuildId = discovery.capabilities.buildId.toUtf8();
    report.insert(QStringLiteral("active_build_before"),
                  discovery.capabilities.buildId);
    const auto liveProduct = session.readProductState();
    report.insert(QStringLiteral("live_product_state_success"), liveProduct.success);
    report.insert(QStringLiteral("live_device_model"),
                  liveProduct.deviceModel.value(QStringLiteral("active_document"))
                      .toObject().value(QStringLiteral("model_name")));
    report.insert(QStringLiteral("live_product_state_message"), liveProduct.message);
    QByteArray wholeSha;
    if (!discovery.available || !discovery.catalogReady
        || !checkUpgradePackage(packageFile, packageMetadata.packageVersion,
                                packageMetadata.deviceModel,
                                packageMetadata.targetBuildId,
                                activeBuildId, &wholeSha, &error)
        || !packageFile.seek(0)) {
        return fail(4, error.isEmpty()
            ? QStringLiteral("Windows升级包预检失败。") : error);
    }
    report.insert(QStringLiteral("package_sha256"),
                  QString::fromLatin1(wholeSha.toHex()));
    steps.push_back(QStringLiteral("Windows U2P1 preflight passed"));

    ucm::ControlAuthorityResultV2 authority =
        session.readControlAuthorityState();
    if (!authority.success
        || authority.state.appliedMode != ucm::kUsbControlModeAutonomousV2)
        return fail(5, QStringLiteral("升级开始时设备不处于明确AUTONOMOUS状态。"));
    authority = session.switchControlMode(ucm::kUsbControlModeHostManagedV2);
    steps.push_back(QStringLiteral("HOST_MANAGED takeover success=%1 safe=%2")
        .arg(authority.success)
        .arg(ucm::usbHostManagedStoppedV2(authority.state)));
    if (!authority.success || !ucm::usbHostManagedStoppedV2(authority.state)) {
        session.switchControlMode(ucm::kUsbControlModeAutonomousV2);
        return fail(5, QStringLiteral("ARM未确认HOST_MANAGED硬件安全停止态。"));
    }

    ucm::productv9::UpgradePackage package;
    package.transactionId = transactionId();
    package.totalBytes = static_cast<quint64>(packageFile.size());
    package.packageVersion = packageMetadata.packageVersion;
    package.deviceModel = packageMetadata.deviceModel;
    package.buildId = packageMetadata.targetBuildId;
    package.sha256 = wholeSha;
    report.insert(QStringLiteral("transaction_id"),
                  QString::number(package.transactionId));

    ucm::ProductOperationResult result =
        session.beginProductUpgrade(package, true);
    steps.push_back(operationObject(result));
    if (!result.ok) {
        session.switchControlMode(ucm::kUsbControlModeAutonomousV2);
        return fail(6, result.message);
    }

    int chunks = 0;
    while (!packageFile.atEnd()) {
        const QByteArray block = packageFile.read(65536);
        if (block.isEmpty()) {
            session.switchControlMode(ucm::kUsbControlModeAutonomousV2);
            return fail(6, QStringLiteral("升级包分块读取失败。"));
        }
        if ((chunks % 2) == 1) {
            const ucm::ControlAuthorityResultV2 lease =
                session.renewControlLease();
            if (!lease.success) {
                session.switchControlMode(ucm::kUsbControlModeAutonomousV2);
                return fail(6, lease.message);
            }
        }
        result = session.writeProductUpgradeChunk(block);
        steps.push_back(operationObject(result));
        if (!result.ok) {
            session.switchControlMode(ucm::kUsbControlModeAutonomousV2);
            return fail(6, result.message);
        }
        ++chunks;
    }
    report.insert(QStringLiteral("chunks"), chunks);

    result = session.finalizeProductUpgrade();
    steps.push_back(operationObject(result));
    for (int poll = 0; result.ok && !terminalState(result, 3)
         && poll < 120; ++poll) {
        QThread::msleep(250U);
        result = session.pollProductUpgrade();
        steps.push_back(operationObject(result));
    }
    if (!terminalState(result, 3)) {
        session.switchControlMode(ucm::kUsbControlModeAutonomousV2);
        return fail(7, result.message.isEmpty()
            ? QStringLiteral("升级包未进入STAGED终态。") : result.message);
    }

    result = session.activateProductUpgrade(true);
    steps.push_back(operationObject(result));
    bool installed = terminalState(result, 5);
    for (int poll = 0; !installed && poll < 120; ++poll) {
        QThread::msleep(1000U);
        if (!result.ok || result.outcome == QStringLiteral("unresolved")) {
            session.disconnectTransport();
            session.reconnectTransport();
        }
        result = session.pollProductUpgrade();
        steps.push_back(operationObject(result));
        installed = terminalState(result, 5);
        if (result.fields.value(QStringLiteral("state")).toInt() >= 6)
            break;
    }
    if (!installed)
        return fail(8, result.message.isEmpty()
            ? QStringLiteral("激活后120秒内未确认新槽。") : result.message);

    discovery = session.usbExtendedDiscovery();
    if (!discovery.available
        || discovery.capabilities.buildId.toUtf8() != packageMetadata.targetBuildId)
        return fail(9, QStringLiteral("升级已确认，但USB未读回目标Build ID。"));
    report.insert(QStringLiteral("active_build_after"),
                  discovery.capabilities.buildId);
    report.insert(QStringLiteral("transport_evidence"), session.evidence());
    report.insert(QStringLiteral("steps"), steps);
    report.insert(QStringLiteral("passed"), true);
    emitReport(report);
    return 0;
}
