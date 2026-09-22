#include "dashboard_bridge.h"
#include "ethercat_port_probe.h"
#include "libusb_backend.h"
#include "reference_force_source.h"
#include "usb_extended_wire_v2.h"

#include <QGuiApplication>
#include <QDir>
#include <QCryptographicHash>
#include <QFont>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTextStream>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include <memory>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <windows.h>
#endif

namespace {

bool applySystemBackdrop(QQuickWindow *window)
{
#ifdef Q_OS_WIN
    if (window == nullptr) return false;
    window->setColor(Qt::transparent);
    const HWND handle = reinterpret_cast<HWND>(window->winId());
    const MARGINS margins {-1, -1, -1, -1};
    constexpr DWORD systemBackdropAttribute = 38;
    constexpr int micaBackdrop = 2;
    const HRESULT frame = DwmExtendFrameIntoClientArea(handle, &margins);
    const HRESULT backdrop = DwmSetWindowAttribute(
        handle, systemBackdropAttribute, &micaBackdrop,
        sizeof(micaBackdrop));
    return SUCCEEDED(frame) && SUCCEEDED(backdrop);
#else
    Q_UNUSED(window)
    return false;
#endif
}

} // namespace

int main(int argc, char *argv[])
{
    const QStringList rawArguments = [&] {
        QStringList result;
        for (int index = 0; index < argc; ++index)
            result.push_back(QString::fromLocal8Bit(argv[index]));
        return result;
    }();
    const bool systemBackdrop =
        !rawArguments.contains(QStringLiteral("--no-system-backdrop"));
    int initialPage = 0;
    const int pageIndex = rawArguments.indexOf(QStringLiteral("--page"));
    if (pageIndex >= 0) {
        if (pageIndex + 1 >= rawArguments.size()) return 5;
        bool ok = false;
        initialPage = rawArguments.at(pageIndex + 1).toInt(&ok);
        if (!ok || initialPage < 0 || initialPage > 9) return 5;
    }
    int initialWidth = 0;
    int initialHeight = 0;
    const int windowSizeIndex = rawArguments.indexOf(
        QStringLiteral("--window-size"));
    if (windowSizeIndex >= 0) {
        if (windowSizeIndex + 1 >= rawArguments.size()) return 12;
        const QStringList parts = rawArguments.at(windowSizeIndex + 1)
                                      .toLower().split(QLatin1Char('x'));
        bool widthOk = false;
        bool heightOk = false;
        if (parts.size() == 2) {
            initialWidth = parts.at(0).toInt(&widthOk);
            initialHeight = parts.at(1).toInt(&heightOk);
        }
        if (!widthOk || !heightOk || initialWidth < 1180
            || initialHeight < 700) {
            return 12;
        }
    }
    QGuiApplication application(argc, argv);
    application.setFont(QFont(QStringLiteral("Microsoft YaHei UI")));
    application.setApplicationName(QStringLiteral("启真传感 · UCM"));
    application.setOrganizationName(QStringLiteral("Qizhen Sensing"));
    application.setWindowIcon(QIcon(
        QStringLiteral(":/qt/qml/UcmConfigStudio/Next/"
                       "assets/qizhen-ucm-icon.png")));
    QQuickStyle::setStyle(QStringLiteral("FluentWinUI3"));
    QQuickStyle::setFallbackStyle(QStringLiteral("Fusion"));

    if (rawArguments.contains(QStringLiteral("--ethercat-probe"))) {
        const EthercatPortProbeResult detected = EthercatPortScanner::scan();
        QVariantMap report = detected.evidence;
        report.insert(QStringLiteral("success"), detected.success);
        report.insert(QStringLiteral("detected"), detected.detected);
        report.insert(QStringLiteral("status"), detected.statusText);
        QTextStream(stdout) << QJsonDocument::fromVariant(report)
                                   .toJson(QJsonDocument::Indented);
        return detected.detected ? 0 : 13;
    }

    const bool usbDriverCheck = rawArguments.contains(
        QStringLiteral("--usb-driver-check"));
    const bool usbDriverCheckQuiet = rawArguments.contains(
        QStringLiteral("--usb-driver-check-quiet"));
    if (usbDriverCheck || usbDriverCheckQuiet) {
        ucm::LibusbBackend usb;
        const ucm::UsbOpenResult checked = usb.open();
        const QString details = checked.message + QStringLiteral("\n\n")
            + QString::fromUtf8(QJsonDocument(checked.evidence)
                                    .toJson(QJsonDocument::Indented));
#ifdef Q_OS_WIN
        if (!usbDriverCheckQuiet) {
            const UINT flags = MB_OK | (checked.success
                ? MB_ICONINFORMATION : MB_ICONWARNING);
            MessageBoxW(nullptr,
                        reinterpret_cast<LPCWSTR>(details.utf16()),
                        L"启真传感 UCM · USB 驱动/连接检查",
                        flags);
        }
#else
        Q_UNUSED(usbDriverCheck)
#endif
        if (usbDriverCheckQuiet) qInfo().noquote() << details;
        return checked.success ? 0 : 9;
    }

    if (rawArguments.contains(QStringLiteral("--usb-v2-probe"))) {
        ucm::ConfigurationSession session(
            std::make_unique<ucm::UsbFunctionfsTransport>());
        const ucm::UsbExtendedDiscoveryV2 extended =
            session.usbExtendedDiscovery();
        const ucm::BinaryObjectResult capabilitiesObject =
            session.readUsbExtendedCapabilitiesObject();
        const ucm::BinaryObjectResult parameterCatalogObject =
            session.readUsbParameterCatalogObject();
        const QJsonObject sessionEvidence = session.evidence();
        QJsonArray parameters;
        for (const ucm::UsbParameterDescriptorV2 &parameter
             : extended.parameters) {
            parameters.append(QJsonObject {
                {QStringLiteral("field_id"),
                 static_cast<int>(parameter.fieldId)},
                {QStringLiteral("name"),
                 ucm::usbParameterFieldNameV2(parameter.fieldId)},
                {QStringLiteral("group"),
                 ucm::usbParameterGroupNameV2(parameter.groupId)},
                {QStringLiteral("scope"),
                 ucm::usbParameterScopeNameV2(parameter.scope)},
                {QStringLiteral("access"),
                 ucm::usbParameterAccessTextV2(parameter.accessFlags)}
            });
        }
        const QJsonObject report {
            {QStringLiteral("available"), extended.available},
            {QStringLiteral("catalog_ready"), extended.catalogReady},
            {QStringLiteral("message"), extended.message},
            {QStringLiteral("real_usb_opened"),
             sessionEvidence.value(QStringLiteral("real_usb_opened"))},
            {QStringLiteral("arm_receiver_contacted"),
             sessionEvidence.value(QStringLiteral("arm_receiver_contacted"))},
            {QStringLiteral("transport_evidence"),
             sessionEvidence.value(QStringLiteral("transport_evidence"))},
            {QStringLiteral("schema_version"),
             static_cast<int>(extended.capabilities.schemaVersion)},
            {QStringLiteral("protocol_revision"),
             static_cast<int>(extended.capabilities.protocolRevision)},
            {QStringLiteral("device_class"),
             extended.capabilities.deviceClass},
            {QStringLiteral("build_id"), extended.capabilities.buildId},
            {QStringLiteral("parameter_catalog_crc32"),
             QStringLiteral("0x%1").arg(
                 extended.capabilities.parameterCatalogCrc32, 8, 16,
                 QLatin1Char('0')).toUpper()},
            {QStringLiteral("capabilities_object_bytes"),
             capabilitiesObject.data.size()},
            {QStringLiteral("capabilities_object_sha256"),
             QString::fromLatin1(QCryptographicHash::hash(
                 capabilitiesObject.data, QCryptographicHash::Sha256).toHex())},
            {QStringLiteral("capabilities_object_hex"),
             QString::fromLatin1(capabilitiesObject.data.toHex())},
            {QStringLiteral("parameter_catalog_object_bytes"),
             parameterCatalogObject.data.size()},
            {QStringLiteral("active_feature_mask"),
             QStringLiteral("0x%1").arg(
                 extended.capabilities.activeFeatureMask, 0, 16).toUpper()},
            {QStringLiteral("active_config_group_mask"),
             QStringLiteral("0x%1").arg(
                 extended.capabilities.activeConfigGroupMask, 0, 16).toUpper()},
            {QStringLiteral("active_upgrade_target_mask"),
             QStringLiteral("0x%1").arg(
                 extended.capabilities.activeUpgradeTargetMask, 0, 16).toUpper()},
            {QStringLiteral("parameters"), parameters}
        };
        QTextStream(stdout) << QJsonDocument(report)
                                   .toJson(QJsonDocument::Indented);
        return extended.available && extended.catalogReady
                && capabilitiesObject.success
                && capabilitiesObject.data.size() == 256
                && parameterCatalogObject.success
                && !parameterCatalogObject.data.isEmpty()
            ? 0 : 11;
    }

    if (rawArguments.contains(
            QStringLiteral("--configuration-apply-mock-self-test"))) {
        ucm::ConfigurationSession confirmed;
        ucm::Configuration candidate = confirmed.candidate();
        candidate.afe.digitalTgcAttenuationDb = 6;
        confirmed.setCandidate(candidate);
        const bool successPath = confirmed.prepare()
            && confirmed.validateCandidate() && confirmed.commitRam()
            && confirmed.confirmReadback()
            && confirmed.phase() == ucm::ApplyPhase::Confirmed
            && confirmed.active().afe.digitalTgcAttenuationDb == 6
            && !confirmed.hasStagedConfiguration()
            && !confirmed.transportInfo().nonvolatileWrite;

        ucm::ConfigurationSession failedReadback;
        candidate = failedReadback.candidate();
        candidate.afe.digitalTgcAttenuationDb = 6;
        failedReadback.setCandidate(candidate);
        const bool staged = failedReadback.prepare()
            && failedReadback.validateCandidate()
            && failedReadback.commitRam();
        failedReadback.injectReadbackFailure();
        const bool failurePath = staged
            && !failedReadback.confirmReadback()
            && failedReadback.active().afe.digitalTgcAttenuationDb == 0;
        return successPath && failurePath ? 0 : 8;
    }

    if (rawArguments.contains(QStringLiteral("--reference-force-sdk-self-test"))) {
        const QString root = QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("MVS"));
        const QStringList args = ReferenceForceSource::cameraSdkArguments();
        return args.size() >= 2 && args[0] == QStringLiteral("--sdk-dir")
            && args[1] == root ? 0 : 11;
    }

    if (rawArguments.contains(
            QStringLiteral("--reference-force-parser-self-test"))) {
        ReferenceForceFrame frame;
        QString error;
        const QByteArray valid = R"json({"schema_version":"forceInput_v1","timestamp_ms":1788400000123,"force_kN":[269.7,226.3,233.2,239.6],"source":"force_input_camera","evidence_level":"force_input_camera","confidence":0.95,"status":"ok"})json";
        const bool acceptsValid = decodeReferenceForcePayload(
            valid, &frame, &error)
            && frame.timestampMs == 1788400000123LL
            && qFuzzyCompare(frame.forceKn[0], 269.7)
            && qFuzzyCompare(frame.forceKn[3], 239.6)
            && qFuzzyCompare(frame.confidence, 0.95);
        const QByteArray lowConfidence = R"json({"schema_version":"forceInput_v1","timestamp_ms":1788400000123,"force_kN":[1,2,3,4],"source":"force_input_camera","evidence_level":"force_input_camera","confidence":0.79,"status":"ok"})json";
        const QByteArray wrongSource = R"json({"schema_version":"forceInput_v1","timestamp_ms":1788400000123,"force_kN":[1,2,3,4],"source":"simulated_force","evidence_level":"simulated","confidence":1.0,"status":"ok"})json";
        const QByteArray threeRods = R"json({"schema_version":"forceInput_v1","timestamp_ms":1788400000123,"force_kN":[1,2,3],"source":"force_input_camera","evidence_level":"force_input_camera","confidence":1.0,"status":"ok"})json";
        const bool rejectsUnsafe =
            !decodeReferenceForcePayload(lowConfidence, &frame, &error)
            && !decodeReferenceForcePayload(wrongSource, &frame, &error)
            && !decodeReferenceForcePayload(threeRods, &frame, &error);
        return acceptsValid && rejectsUnsafe ? 0 : 10;
    }

    QQmlApplicationEngine engine;
    const bool offlinePreview = rawArguments.contains(QStringLiteral("--offline"))
        || rawArguments.contains(QStringLiteral("--engineer-preview"))
        || rawArguments.contains(QStringLiteral("--screenshot"))
        || std::any_of(rawArguments.cbegin(), rawArguments.cend(),
            [](const QString &value) { return value.endsWith(QStringLiteral("self-test")); });
    DashboardBridge backend(nullptr, rawArguments.contains(
        QStringLiteral("--configuration-local-self-test")), offlinePreview);
    if (rawArguments.contains(
            QStringLiteral("--configuration-local-self-test"))) {
        const QVariantList fields = backend.afeConfigurationFields();
        const auto hasField = [&fields](const QString &key) {
            return std::any_of(fields.cbegin(), fields.cend(),
                               [&key](const QVariant &value) {
                return value.toMap().value(QStringLiteral("key")).toString()
                    == key;
            });
        };
        const bool completeAfeEditor = fields.size() == 9
            && hasField(QStringLiteral("lna_gain_db_rod_1"))
            && hasField(QStringLiteral("lna_gain_db_rod_4"))
            && hasField(QStringLiteral("digital_gain_steps_rod_1"))
            && hasField(QStringLiteral("digital_gain_steps_rod_4"))
            && hasField(QStringLiteral("digital_tgc_attenuation_db"))
            && !hasField(QStringLiteral("afe_gain_point"));
        if (!completeAfeEditor) return 6;
        backend.setConfigurationField(
            QStringLiteral("lna_gain_db_rod_1"), 24);
        backend.setConfigurationField(
            QStringLiteral("digital_gain_steps_rod_4"), 30);
        backend.setConfigurationField(
            QStringLiteral("digital_tgc_attenuation_db"), 6);
        backend.prepareConfiguration();
        const bool validCandidatePassed = backend.configurationPrepared()
            && backend.configurationErrors().isEmpty()
            && !backend.configurationIdentity().isEmpty()
            && backend.configurationHasChanges();
        backend.resetConfigurationDraft();
        backend.setConfigurationField(QStringLiteral("afe_gain_point"), 0);
        const bool obsoleteCombinedGainBlocked =
            !backend.configurationHasChanges()
            && backend.configurationStatus().contains(
                QStringLiteral("输入未采用"));
        backend.setConfigurationField(
            QStringLiteral("measurement_point_mm"), 6000);
        backend.prepareConfiguration();
        const bool invalidCandidateBlocked = !backend.configurationPrepared()
            && !backend.configurationErrors().isEmpty();
        return validCandidatePassed && obsoleteCombinedGainBlocked
                && invalidCandidateBlocked
            ? 0 : 7;
    }
    engine.rootContext()->setContextProperty(
        QStringLiteral("backend"), &backend);
    QVariantMap initialProperties {
        {QStringLiteral("engineerPreview"), offlinePreview && rawArguments.contains(QStringLiteral("--engineer-preview"))},
        {QStringLiteral("systemBackdropRequested"), systemBackdrop},
        {QStringLiteral("selectedPage"), initialPage}
    };
    if (initialWidth > 0) {
        initialProperties.insert(QStringLiteral("width"), initialWidth);
        initialProperties.insert(QStringLiteral("height"), initialHeight);
    }
    engine.setInitialProperties(initialProperties);
    engine.loadFromModule(QStringLiteral("UcmConfigStudio.Next"),
                          QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) return 2;
    auto *window = qobject_cast<QQuickWindow *>(
        engine.rootObjects().constFirst());
    const bool nativeBackdropActive = systemBackdrop
        && applySystemBackdrop(window);
    if (window != nullptr) {
        window->setProperty("nativeBackdropActive",
                            nativeBackdropActive);
    }

    const int screenshotIndex = rawArguments.indexOf(
        QStringLiteral("--screenshot"));
    if (screenshotIndex >= 0) {
        if (screenshotIndex + 1 >= rawArguments.size()) return 3;
        const QString outputPath = rawArguments.at(screenshotIndex + 1);
        QTimer::singleShot(1400, &application,
                           [&application, &engine, outputPath] {
            auto *rootWindow = qobject_cast<QQuickWindow *>(
                engine.rootObjects().constFirst());
            const bool written = rootWindow
                && rootWindow->grabWindow().save(outputPath);
            application.exit(written ? 0 : 4);
        });
    }
    return application.exec();
}
