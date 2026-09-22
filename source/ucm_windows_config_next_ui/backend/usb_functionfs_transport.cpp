#include "usb_functionfs_transport.h"
#include "usb_runtime_progress_v9.h"

#include <QJsonArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QThread>
#include <QtEndian>
#include <QScopeGuard>

#include <limits>
#include <chrono>
#include <cstring>

namespace ucm {
namespace {

void appendU32(QByteArray &bytes, quint32 value)
{
    const quint32 little = qToLittleEndian(value);
    bytes.append(reinterpret_cast<const char *>(&little), sizeof(little));
}

void appendU64(QByteArray &bytes, quint64 value)
{
    const quint64 little = qToLittleEndian(value);
    bytes.append(reinterpret_cast<const char *>(&little), sizeof(little));
}

qint32 readErrorStatus(const QByteArray &payload)
{
    if (payload.size() < 4) return -1;
    return static_cast<qint32>(qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(payload.constData())));
}

quint16 readU16(const QByteArray &bytes, int offset)
{
    return qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar *>(bytes.constData() + offset));
}

quint32 readU32(const QByteArray &bytes, int offset)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(bytes.constData() + offset));
}

quint64 readU64(const QByteArray &bytes, int offset)
{
    return qFromLittleEndian<quint64>(
        reinterpret_cast<const uchar *>(bytes.constData() + offset));
}

} // namespace

UsbFunctionfsTransport::UsbFunctionfsTransport()
    : UsbFunctionfsTransport(std::make_unique<LibusbBackend>())
{
}

UsbFunctionfsTransport::UsbFunctionfsTransport(bool autoInitialize)
    : UsbFunctionfsTransport(std::make_unique<LibusbBackend>(), autoInitialize)
{
}

UsbFunctionfsTransport::UsbFunctionfsTransport(std::unique_ptr<LibusbBackend> backend, bool autoInitialize)
    : m_backend(std::move(backend))
{
    if (autoInitialize) (void)initialize();
}

UsbFunctionfsTransport::~UsbFunctionfsTransport()
{
    if (m_backend) m_backend->close();
}

void UsbFunctionfsTransport::invalidateSession(const QString &reason)
{
    invalidateProductOperations();
    if (m_backend) m_backend->close();
    m_initialized = false;
    m_active = {};
    m_capabilities = {}; m_extendedDiscovery = {}; m_controlAuthority = {};
    m_runtimeStatus = {}; m_compositeRuntimeStatus = {};
    m_extendedCapabilitiesObject.clear(); m_parameterCatalogObject.clear();
    m_controlAuthorityObject.clear(); m_runtimeStatusObject.clear();
    m_compositeRuntimeStatusObject.clear();
    m_productState = {}; m_productPublication.clear(); m_productInput.clear();
    m_lastProductIdentity = {}; m_productProgressTimer.invalidate();
    m_logEntries.clear();
    m_compositeRuntimeTrustSource.clear(reason);
    m_lastError = reason;
    m_protocolEvidence.insert(QStringLiteral("last_error"), reason);
}

void UsbFunctionfsTransport::disconnect()
{
    invalidateSession(QStringLiteral("已手动断开USB。"));
}

bool UsbFunctionfsTransport::initialize()
{
    if (!m_backend) return false;
    const UsbOpenResult opened = m_backend->open();
    m_protocolEvidence.insert(QStringLiteral("open"), opened.evidence);
    if (!opened.success) { invalidateSession(opened.message); return false; }
    UsbFrameV1 response;
    QString error;
    if (!transact(UsbMessageTypeV1::Capabilities, 0, {}, &response, &error)
        || response.flags != 9U
        || !decodeUsbProductCapabilitiesV9(response.payload, &m_capabilities, &error)) {
        invalidateSession(error.isEmpty() ? QStringLiteral("基础能力响应不是完整只读对象。") : error);
        return false;
    }
    discoverExtendedV2();
    if (!m_extendedDiscovery.available
        || m_extendedDiscovery.capabilities.protocolRevision != 9U) {
        const auto reason = m_extendedDiscovery.message;
        invalidateSession(QStringLiteral("协议不兼容：%1").arg(reason));
        return false;
    }
    m_initialized = true;
    m_failurePreservesActive = true;
    // Missing authority/runtime snapshots do not prevent a read-only session.
    readControlAuthorityState();
    return m_initialized;
}

bool UsbFunctionfsTransport::transact(
    UsbMessageTypeV1 type, quint64 transactionId,
    const QByteArray &requestPayload, UsbFrameV1 *response,
    QString *error, unsigned timeoutMs, qint32 *wireStatus)
{
    std::unique_lock<std::mutex> lock(m_transactionMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        if (error) *error = QStringLiteral("USB已有在途请求。");
        return false;
    }
    bool frameAccepted = false;
    auto invalidateOnFailure = qScopeGuard([&] {
        if (!frameAccepted) invalidateSession(error && !error->isEmpty()
            ? *error : QStringLiteral("USB传输或协议校验失败，请重新连接。"));
    });
    if (wireStatus != nullptr) {
        *wireStatus = std::numeric_limits<qint32>::min();
    }
    if (!m_backend || !m_backend->isOpen() || response == nullptr
        || m_nextSequence == 0
        || m_nextSequence == std::numeric_limits<quint64>::max()) {
        if (error != nullptr) *error = QStringLiteral("USB 设备未初始化或序号已耗尽。");
        return false;
    }
    const quint64 sequence = m_nextSequence++;
    QString codecError;
    const QByteArray request = encodeUsbFrameV1(
        type, 0, sequence, transactionId, requestPayload, &codecError);
    if (request.isEmpty()) {
        if (error != nullptr) *error = codecError;
        return false;
    }
    QElapsedTimer deadline;
    deadline.start();
    auto remaining = [&]() -> unsigned {
        return static_cast<unsigned>(qMax<qint64>(1, static_cast<qint64>(timeoutMs) - deadline.elapsed()));
    };
    auto expired = [&]() {
        if (deadline.elapsed() < timeoutMs) return false;
        if (error) *error = QStringLiteral("USB请求超过总超时预算。");
        return true;
    };
    if (expired()) return false;
    const UsbTransferResult headerWritten = m_backend->writeAll(
        request.left(kUsbWireHeaderBytesV1), remaining());
    if (!headerWritten.success()) {
        if (error != nullptr) *error = headerWritten.message;
        return false;
    }
    if (!requestPayload.isEmpty()) {
        if (expired()) return false;
        const UsbTransferResult payloadWritten = m_backend->writeAll(
            request.mid(kUsbWireHeaderBytesV1), remaining());
        if (!payloadWritten.success()) {
            if (error != nullptr) *error = payloadWritten.message;
            return false;
        }
    }
    if (expired()) return false;
    const UsbTransferResult headerRead = m_backend->readExact(
        kUsbWireHeaderBytesV1, remaining());
    quint32 payloadBytes = 0;
    quint32 payloadCrc = 0;
    if (!headerRead.success()
        || !decodeUsbFrameHeaderV1(headerRead.data, response,
                                   &payloadBytes, &payloadCrc, &codecError)) {
        if (error != nullptr) *error = headerRead.success()
            ? codecError : headerRead.message;
        return false;
    }
    QByteArray payload;
    if (payloadBytes != 0) {
        if (expired()) return false;
        const UsbTransferResult payloadRead = m_backend->readExact(
            static_cast<int>(payloadBytes), remaining());
        if (!payloadRead.success()) {
            if (error != nullptr) *error = payloadRead.message;
            return false;
        }
        payload = payloadRead.data;
    }
    if (expired()) return false;
    if (!finishUsbFrameV1(response, payload, payloadBytes, payloadCrc,
                          &codecError)
        || response->sequence != sequence
        || response->transactionId != transactionId) {
        if (error != nullptr) *error = codecError.isEmpty()
            ? QStringLiteral("USB 响应序号或事务号不匹配。") : codecError;
        return false;
    }
    if (response->messageType == static_cast<quint16>(UsbMessageTypeV1::Error)
        || (response->flags & 0x0002U) != 0U) {
        if (response->messageType != 255U || response->flags != 3U
            || response->payload.size() != 32
            || readU64(response->payload, 8) != sequence
            || readU64(response->payload, 16) != transactionId
            || readU64(response->payload, 24) != 0U
            || readErrorStatus(response->payload) < 1
            || readErrorStatus(response->payload) > 12) {
            if (error) *error = QStringLiteral("USB错误对象身份或保留字段不匹配。");
            return false;
        }
        const qint32 status = readErrorStatus(response->payload);
        frameAccepted = status != 4 && status != 5;
        if (status == 10) { m_controlAuthority = {}; m_controlAuthorityObject.clear(); }
        if (wireStatus != nullptr) *wireStatus = status;
        if (error != nullptr) {
            *error = QStringLiteral("ARM USB receiver 拒绝请求，status=%1（%2）。")
                .arg(status).arg(usbWireStatusTextV1(status));
        }
        return false;
    }
    if (response->messageType != static_cast<quint16>(type)) {
        if (error != nullptr) *error = QStringLiteral("USB 响应消息类型不匹配。");
        return false;
    }
    if (wireStatus != nullptr) *wireStatus = 0;
    frameAccepted = true;
    return true;
}

void UsbFunctionfsTransport::discoverExtendedV2()
{
    m_extendedDiscovery = {};
    m_extendedCapabilitiesObject.clear();
    m_parameterCatalogObject.clear();

    UsbFrameV1 response;
    QString error;
    qint32 wireStatus = std::numeric_limits<qint32>::min();
    if (!transact(UsbMessageTypeV1::ExtendedCapabilitiesV2, 0, {},
                  &response, &error, 3000U, &wireStatus)) {
        m_extendedDiscovery.message = QStringLiteral(
            "USB V2能力探测未闭合；本版本不回退到V1配置：%1")
                .arg(error);
        m_protocolEvidence.insert(QStringLiteral("usb_v2_available"), false);
        m_protocolEvidence.insert(QStringLiteral("usb_v2_status"),
                                  m_extendedDiscovery.message);
        return;
    }
    // Preserve the exact object even when strict decoding rejects it. This is
    // diagnostic evidence only; an invalid object never activates V2.
    m_extendedCapabilitiesObject = response.payload;
    if (response.payload.size() == kUsbExtendedCapabilitiesBytesV2) {
        m_protocolEvidence.insert(QStringLiteral("usb_v2_raw_supported_features"),
            QStringLiteral("0x%1").arg(
                readU64(response.payload, 72), 0, 16).toUpper());
        m_protocolEvidence.insert(QStringLiteral("usb_v2_raw_active_features"),
            QStringLiteral("0x%1").arg(
                readU64(response.payload, 80), 0, 16).toUpper());
        m_protocolEvidence.insert(QStringLiteral("usb_v2_raw_active_config_groups"),
            QStringLiteral("0x%1").arg(
                readU64(response.payload, 48), 0, 16).toUpper());
    }

    UsbExtendedCapabilitiesV2 capabilities;
    if ((response.flags & 0x0008U) == 0U
        || (response.flags & 0x0004U) != 0U
        || !decodeUsbExtendedCapabilitiesV2(
            response.payload, &capabilities, &error)) {
        m_extendedDiscovery.message = QStringLiteral(
            "ARM返回了USB V2能力对象，但严格解析失败：%1").arg(error);
        m_protocolEvidence.insert(QStringLiteral("usb_v2_available"), false);
        m_protocolEvidence.insert(QStringLiteral("usb_v2_status"),
                                  m_extendedDiscovery.message);
        return;
    }

    if (capabilities.protocolRevision != 9U) {
        m_extendedDiscovery.message = QStringLiteral("本产品要求USB revision 9，不回退旧配置协议。");
        return;
    }
    m_extendedDiscovery.available = true;
    m_extendedDiscovery.capabilities = capabilities;
    if ((capabilities.activeFeatureMask
         & kUsbExtendedFeatureParameterCatalogV2) != 0U) {
        quint32 totalEntries = 0U;
        bool more = true;
        QVector<UsbParameterDescriptorV2> parameters;
        QByteArray descriptorBytes;
        quint32 startIndex = 0U;
        bool catalogValid = true;
        while (more && catalogValid) {
            const QByteArray request = encodeUsbParameterCatalogRequestV2(
                startIndex, kUsbParameterCatalogMaximumEntriesV2,
                capabilities.parameterCatalogCrc32, &error);
            UsbFrameV1 catalogResponse;
            QVector<UsbParameterDescriptorV2> chunk;
            bool pageMore = false;
            if (request.isEmpty()
                || !transact(UsbMessageTypeV1::ParameterCatalogV2, 0,
                             request, &catalogResponse, &error, 3000U)
                || (catalogResponse.flags & 0x0008U) == 0U
                || !decodeUsbParameterCatalogChunkV2(
                    catalogResponse.payload, startIndex,
                    capabilities.parameterCatalogCrc32, &totalEntries,
                    &pageMore, &chunk, &error)) {
                catalogValid = false;
                break;
            }
            const bool frameMore =
                (catalogResponse.flags & 0x0004U) != 0U;
            if (frameMore != pageMore) {
                error = QStringLiteral(
                    "USB revision 9参数目录内外层MORE标志不一致（起点%1）。")
                        .arg(startIndex);
                catalogValid = false;
                break;
            }
            more = pageMore;
            descriptorBytes.append(catalogResponse.payload.mid(
                kUsbParameterCatalogHeaderBytesV2));
            parameters += chunk;
            startIndex = static_cast<quint32>(parameters.size());
        }
        if (catalogValid && more && error.isEmpty()) {
            error = QStringLiteral("USB revision 9参数目录分页未结束。");
        } else if (catalogValid
                   && totalEntries != capabilities.parameterCatalogEntries
                   && error.isEmpty()) {
            error = QStringLiteral(
                "USB revision 9参数目录总数不一致（能力%1，目录%2）。")
                    .arg(capabilities.parameterCatalogEntries)
                    .arg(totalEntries);
        } else if (catalogValid
                   && static_cast<quint32>(parameters.size()) != totalEntries
                   && error.isEmpty()) {
            error = QStringLiteral(
                "USB revision 9参数目录分页缺项（已收%1，总数%2）。")
                    .arg(parameters.size()).arg(totalEntries);
        } else if (catalogValid
                   && usbCrc32V1(descriptorBytes)
                       != capabilities.parameterCatalogCrc32
                   && error.isEmpty()) {
            error = QStringLiteral(
                "USB revision 9参数目录合并CRC不一致（实际0x%1，能力0x%2）。")
                    .arg(usbCrc32V1(descriptorBytes), 8, 16, QLatin1Char('0'))
                    .arg(capabilities.parameterCatalogCrc32, 8, 16,
                         QLatin1Char('0'));
        }
        if (!catalogValid
            || more
            || totalEntries != capabilities.parameterCatalogEntries
            || static_cast<quint32>(parameters.size()) != totalEntries
            || usbCrc32V1(descriptorBytes)
                != capabilities.parameterCatalogCrc32
            || !validateR2sWritableParameterCatalogV2(parameters, &error)) {
            m_extendedDiscovery.message = QStringLiteral(
                "USB revision 9参数目录未闭合：%1").arg(error);
            m_protocolEvidence.insert(QStringLiteral("usb_v2_catalog_ready"),
                                      false);
            m_protocolEvidence.insert(QStringLiteral("usb_v2_status"),
                                      m_extendedDiscovery.message);
            return;
        }
        m_parameterCatalogObject = descriptorBytes;
        m_extendedDiscovery.parameters = parameters;
        m_extendedDiscovery.catalogReady = true;
    }
    const quint64 productReadFeatures =
        kUsbExtendedFeatureDeviceModelControlV2
        | kUsbExtendedFeatureSystemInputPolicyControlV1;
    m_extendedDiscovery.message =
        (capabilities.activeFeatureMask & productReadFeatures)
                == productReadFeatures
        ? (m_extendedDiscovery.catalogReady
               ? QStringLiteral("USB revision 9 已连接；可写参数目录已校验，型号、输入策略和正式结果可查询。")
               : QStringLiteral("USB revision 9 已连接；型号、输入策略和正式结果可只读查询。"))
        : QStringLiteral("USB revision 9 已连接；正式结果可读，设备尚未开放型号与输入策略查询。");

    m_protocolEvidence.insert(QStringLiteral("usb_v2_available"), true);
    m_protocolEvidence.insert(QStringLiteral("usb_v2_catalog_ready"),
                              m_extendedDiscovery.catalogReady);
    m_protocolEvidence.insert(QStringLiteral("usb_v2_status"),
                              m_extendedDiscovery.message);
    m_protocolEvidence.insert(QStringLiteral("usb_v2_schema"),
                              capabilities.schemaVersion);
    m_protocolEvidence.insert(QStringLiteral("usb_v2_protocol_revision"),
                              static_cast<int>(capabilities.protocolRevision));
    m_protocolEvidence.insert(QStringLiteral("usb_v2_build_id"),
                              capabilities.buildId);
    m_protocolEvidence.insert(QStringLiteral("usb_v2_parameter_count"),
                              m_extendedDiscovery.parameters.size());
    m_protocolEvidence.insert(QStringLiteral("usb_v2_catalog_crc32"),
        QStringLiteral("0x%1").arg(
            capabilities.parameterCatalogCrc32, 8, 16,
            QLatin1Char('0')).toUpper());
    m_protocolEvidence.insert(QStringLiteral("usb_v2_active_features"),
        QStringLiteral("0x%1").arg(
            capabilities.activeFeatureMask, 0, 16).toUpper());
    m_protocolEvidence.insert(QStringLiteral("usb_v2_active_config_groups"),
        QStringLiteral("0x%1").arg(
            capabilities.activeConfigGroupMask, 0, 16).toUpper());
    m_protocolEvidence.insert(QStringLiteral("usb_v2_active_upgrade_targets"),
        QStringLiteral("0x%1").arg(
            capabilities.activeUpgradeTargetMask, 0, 16).toUpper());
    m_protocolEvidence.insert(QStringLiteral("usb_v2_control_mode"),
                              static_cast<int>(capabilities.appliedControlMode));
    m_protocolEvidence.insert(QStringLiteral("usb_v2_control_phase"),
                              static_cast<int>(capabilities.controlPhase));
    m_protocolEvidence.insert(QStringLiteral("usb_v2_authority_generation"),
                              QString::number(capabilities.authorityGeneration));
}

TransportInfo UsbFunctionfsTransport::info() const
{
    TransportInfo result;
    result.name = QStringLiteral(
        "USB product revision 9 / libusb WinUSB");
    result.simulationOnly = false;
    result.realUsbOpened = m_backend && m_backend->isOpen();
    result.armReceiverContacted = m_initialized;
    result.nonvolatileWrite = false;
    result.scope = QStringLiteral("product_v9_read_only");
    result.hardwareReceiptValid = false;
    result.failurePreservesActive = m_failurePreservesActive;
    return result;
}

QJsonObject UsbFunctionfsTransport::evidence() const
{
    QJsonObject result = m_protocolEvidence;
    result.insert(QStringLiteral("backend"),
                  m_backend ? m_backend->evidence() : QJsonObject {});
    result.insert(QStringLiteral("initialized"), m_initialized);
    result.insert(QStringLiteral("transactions"),
                  static_cast<double>(m_transactions));
    result.insert(QStringLiteral("last_error"), m_lastError);
    result.insert(QStringLiteral("hardware_receipt_valid"), false);
    result.insert(QStringLiteral("nonvolatile_write"), false);
    result.insert(QStringLiteral("usb_crs1_trust_source"),
                  m_compositeRuntimeTrustSource.evidence());
    return result;
}

BinaryObjectResult UsbFunctionfsTransport::readUsbExtendedCapabilitiesObject()
{
    if (m_extendedCapabilitiesObject.size()
        != kUsbExtendedCapabilitiesBytesV2) {
        return {false, m_extendedDiscovery.message, {}};
    }
    if (!m_extendedDiscovery.available) {
        return {false, m_extendedDiscovery.message,
                m_extendedCapabilitiesObject};
    }
    return {true, QStringLiteral("256 B USB V2能力对象读取成功。"),
            m_extendedCapabilitiesObject};
}

BinaryObjectResult UsbFunctionfsTransport::readUsbParameterCatalogObject()
{
    if (!m_extendedDiscovery.catalogReady
        || m_parameterCatalogObject.size()
            < kUsbParameterCatalogHeaderBytesV2) {
        return {false, m_extendedDiscovery.message, {}};
    }
    return {true,
            QStringLiteral("USB V2参数目录读取成功：%1项。")
                .arg(m_extendedDiscovery.parameters.size()),
            m_parameterCatalogObject};
}

UsbRuntimeConfigOperationResultV9 UsbFunctionfsTransport::readRuntimeConfigV9(
    quint32 kind, quint64 transactionId)
{
    QString error;
    if (!m_backend || !m_backend->isOpen() || !m_initialized
        || !m_extendedDiscovery.catalogReady
        || (m_extendedDiscovery.capabilities.activeFeatureMask
            & kUsbExtendedFeatureConfigV2) == 0U) {
        return {false, QStringLiteral("revision 9运行配置查询能力尚未就绪。"), {}};
    }
    const QByteArray request = encodeUsbRuntimeConfigQueryV9(
        kind, transactionId, &error);
    UsbFrameV1 response;
    if (request.isEmpty()
        || !transact(UsbMessageTypeV1::ConfigStateV2, 0U, request,
                     &response, &error, 3000U)
        || response.flags != 0x0009U) {
        return {false, error.isEmpty()
            ? QStringLiteral("CONFIG_STATE 回执标志无效。") : error, {}};
    }
    UsbRuntimeConfigReceiptV9 receipt;
    if (!decodeUsbRuntimeConfigReceiptV9(
            response.payload, &receipt, &error,
            m_extendedDiscovery.capabilities.parameterCatalogCrc32)) {
        return {false, error, {}};
    }
    m_protocolEvidence.insert(QStringLiteral("runtime_config_generation"),
                              QString::number(receipt.activeGeneration));
    m_protocolEvidence.insert(QStringLiteral("runtime_config_burst_cycles"),
                              static_cast<int>(receipt.activeConfiguration.txBurstCycles));
    m_protocolEvidence.insert(QStringLiteral("runtime_actual_burst_cycles"),
                              static_cast<int>(receipt.actualTxBurstCycles));
    return {true, QStringLiteral("revision 9运行配置与硬件实际值已回读。"), receipt};
}

UsbRuntimeConfigOperationResultV9 UsbFunctionfsTransport::submitRuntimeConfigV9(
    const UsbRuntimeConfigObjectV9 &object)
{
    QString error;
    if (!m_backend || !m_backend->isOpen() || !m_initialized
        || !m_extendedDiscovery.catalogReady
        || (m_extendedDiscovery.capabilities.activeFeatureMask
            & kUsbExtendedFeatureConfigV2) == 0U
        || m_extendedDiscovery.capabilities.activeConfigGroupMask == 0U) {
        return {false, QStringLiteral("ARM未开放 revision 9运行配置写入。"), {}};
    }
    const ControlAuthorityResultV2 authority = readControlAuthorityState();
    if (!authority.success || !usbHostManagedStoppedV2(authority.state)) {
        return {false, authority.success
            ? QStringLiteral("只有HOST_MANAGED且硬件安全停止时才能写运行配置。")
            : authority.message, {}};
    }
    const UsbRuntimeConfigOperationResultV9 active = readRuntimeConfigV9(
        UsbRuntimeConfigQueryActiveV9);
    if (!active.success
        || !usbRuntimeHardwareSafeForWriteV9(active.receipt.hardwareFlags)) {
        return {false, active.success
            ? QStringLiteral("只有UHW2确认设备已卸载时才能写运行配置。")
            : QStringLiteral("写入前无法确认ARM活动配置和卸载状态：%1")
                  .arg(active.message), {}};
    }
    const QByteArray request = encodeUsbRuntimeConfigObjectV9(object, &error);
    UsbFrameV1 response;
    if (request.isEmpty()
        || !transact(UsbMessageTypeV1::ConfigApplyV2, object.transactionId,
                     request, &response, &error, 5000U)
        || response.flags != 0x0001U) {
        return {false, error.isEmpty()
            ? QStringLiteral("CONFIG_APPLY 回执标志无效。") : error, {}};
    }
    UsbRuntimeConfigReceiptV9 receipt;
    if (!decodeUsbRuntimeConfigReceiptV9(
            response.payload, &receipt, &error,
            m_extendedDiscovery.capabilities.parameterCatalogCrc32)
        || receipt.transactionId != object.transactionId) {
        return {false, error.isEmpty()
            ? QStringLiteral("CONFIG_APPLY 事务身份不匹配。") : error, {}};
    }
    const bool appliedOrSaved = receipt.kind == UsbRuntimeConfigReceiptAppliedV9
        || receipt.kind == UsbRuntimeConfigReceiptSavedStartupV9;
    const bool terminalSuccess = appliedOrSaved
        || receipt.kind == UsbRuntimeConfigReceiptValidatedV9;
    if (terminalSuccess && receipt.fieldResults[6] != 0) {
        return {false, QStringLiteral("ARM回执中的burst字段结果不是成功。"), receipt};
    }
    if (appliedOrSaved
        && (receipt.activeConfiguration.txBurstCycles != object.txBurstCycles
            || receipt.actualTxBurstCycles != object.txBurstCycles)) {
        return {false, QStringLiteral("ARM回执中的burst设定与硬件实际值不一致。"), receipt};
    }
    return {true, terminalSuccess
        ? QStringLiteral("revision 9运行配置已由ARM回读确认。")
        : receipt.kind == UsbRuntimeConfigReceiptAcceptedV9
            ? QStringLiteral("ARM已受理运行配置，等待终态查询。")
            : QStringLiteral("ARM拒绝了运行配置；活动配置保持不变。"), receipt};
}

void UsbFunctionfsTransport::recordControlAuthority(
    const ControlAuthorityStateV2 &state)
{
    m_protocolEvidence.insert(QStringLiteral("usb_v2_control_mode"),
                              static_cast<int>(state.appliedMode));
    m_protocolEvidence.insert(QStringLiteral("usb_v2_control_phase"),
                              static_cast<int>(state.phase));
    m_protocolEvidence.insert(QStringLiteral("usb_v2_control_owner"),
                              static_cast<int>(state.owner));
    m_protocolEvidence.insert(QStringLiteral("usb_v2_hardware_active"),
                              state.hardwareActive);
    m_protocolEvidence.insert(QStringLiteral("usb_v2_authority_generation"),
                              QString::number(state.generation));
    m_protocolEvidence.insert(QStringLiteral("usb_v2_authority_last_transaction"),
                              QString::number(state.lastTransactionId));
    m_protocolEvidence.insert(QStringLiteral("usb_v2_authority_last_result"),
                              state.lastResult);
    const quint64 leaseRemainingNs =
        state.hostLeaseDeadlineNs > state.publishedMonotonicNs
        ? state.hostLeaseDeadlineNs - state.publishedMonotonicNs : 0U;
    m_protocolEvidence.insert(QStringLiteral("usb_v2_lease_remaining_ms"),
                              static_cast<double>(leaseRemainingNs / 1000000U));
}

void UsbFunctionfsTransport::recordRuntimeStatus(
    const RuntimeStatusV2 &status)
{
    m_protocolEvidence.insert(QStringLiteral("usb_v2_runtime_status_available"),
                              status.available);
    m_protocolEvidence.insert(QStringLiteral("usb_v2_runtime_flags"),
        QStringLiteral("0x%1").arg(
            status.flags, 8, 16, QLatin1Char('0')).toUpper());
    m_protocolEvidence.insert(QStringLiteral("usb_v2_daemon_instance_id"),
                              QString::number(status.daemonInstanceId));
    m_protocolEvidence.insert(QStringLiteral("usb_v2_heartbeat_sequence"),
                              QString::number(status.heartbeatSequence));
    m_protocolEvidence.insert(QStringLiteral("usb_v2_runtime_run_state"),
                              static_cast<int>(status.runState));
    m_protocolEvidence.insert(QStringLiteral("usb_v2_runtime_control_mode"),
                              static_cast<int>(status.controlMode));
    m_protocolEvidence.insert(QStringLiteral("usb_v2_runtime_control_phase"),
                              static_cast<int>(status.controlPhase));
    m_protocolEvidence.insert(QStringLiteral("usb_v2_runtime_hardware_active"),
                              status.hardwareActive);
    m_protocolEvidence.insert(QStringLiteral("usb_v2_runtime_canonical_phase"),
                              static_cast<int>(status.canonicalPhase));
    m_protocolEvidence.insert(QStringLiteral("usb_v2_runtime_template_state"),
                              static_cast<int>(status.templateState));
    m_protocolEvidence.insert(QStringLiteral("usb_v2_runtime_formal_valid"),
        (status.flags & kUsbRuntimeStatusFormalValidV2) != 0U);
    m_protocolEvidence.insert(QStringLiteral("usb_v2_runtime_fault_domain"),
                              static_cast<int>(status.lastFaultDomain));
    m_protocolEvidence.insert(QStringLiteral("usb_v2_runtime_fault_code"),
                              status.lastFaultCode);
    m_protocolEvidence.insert(QStringLiteral("usb_v2_runtime_recovery_count"),
                              QString::number(status.recoveryCount));
}

ControlAuthorityResultV2 UsbFunctionfsTransport::readControlAuthorityState()
{
    if (!m_backend || !m_backend->isOpen()) {
        return {false, QStringLiteral("USB未连接，无法读取设备控制权状态。")};
    }
    UsbFrameV1 response;
    QString error;
    if (!transact(UsbMessageTypeV1::AuthorityStateV2, 0, {},
                  &response, &error, 3000U)
        || (response.flags & 0x0008U) == 0U
        || (response.flags & 0x0004U) != 0U) {
        m_lastError = error.isEmpty()
            ? QStringLiteral("ARM控制权状态响应未标记为完整只读对象。")
            : error;
        return {false, m_lastError};
    }
    ControlAuthorityStateV2 state;
    if (!decodeUsbControlAuthorityStateV2(response.payload, &state, &error)) {
        m_lastError = error;
        return {false, error};
    }
    m_controlAuthority = state;
    m_controlAuthorityObject = response.payload;
    recordControlAuthority(state);
    return {true,
            QStringLiteral("控制权已读取：%1 · %2。")
                .arg(usbControlModeTextV2(state.appliedMode),
                     usbControlPhaseTextV2(state.phase)),
            state, state.lastTransactionId};
}

BinaryObjectResult UsbFunctionfsTransport::readControlAuthorityObject()
{
    const ControlAuthorityResultV2 result = readControlAuthorityState();
    if (!result.success) return {false, result.message, {}};
    return {true, result.message, m_controlAuthorityObject};
}

RuntimeStatusResultV2 UsbFunctionfsTransport::readRuntimeStatus()
{
    if (!m_backend || !m_backend->isOpen()) {
        return {false, QStringLiteral("USB未连接，无法读取ARM运行状态。")};
    }
    if (!m_extendedDiscovery.available
        || (m_extendedDiscovery.capabilities.activeFeatureMask
            & kUsbExtendedFeatureRuntimeStatusV2) == 0U) {
        return {false, QStringLiteral("ARM未声明USB V2运行状态能力。")};
    }

    m_runtimeStatusObject.clear();
    UsbFrameV1 response;
    QString error;
    if (!transact(UsbMessageTypeV1::RuntimeStatusV2, 0, {},
                  &response, &error, 3000U)
        || (response.flags & 0x0008U) == 0U
        || (response.flags & 0x0004U) != 0U) {
        m_lastError = error.isEmpty()
            ? QStringLiteral("ARM运行状态响应未标记为完整只读对象。")
            : error;
        m_protocolEvidence.insert(
            QStringLiteral("usb_v2_runtime_status_available"), false);
        m_protocolEvidence.insert(QStringLiteral("usb_v2_runtime_status_error"),
                                  m_lastError);
        return {false, m_lastError};
    }

    // Preserve exact bytes as diagnostic evidence even when strict decoding
    // rejects a semantically inconsistent object.
    m_runtimeStatusObject = response.payload;
    RuntimeStatusV2 status;
    if (!decodeUsbRuntimeStatusV2(response.payload, &status, &error)) {
        m_lastError = error;
        m_protocolEvidence.insert(
            QStringLiteral("usb_v2_runtime_status_available"), false);
        m_protocolEvidence.insert(QStringLiteral("usb_v2_runtime_status_error"),
                                  error);
        return {false, error};
    }

    m_runtimeStatus = status;
    recordRuntimeStatus(status);
    m_protocolEvidence.remove(QStringLiteral("usb_v2_runtime_status_error"));
    return {true,
            QStringLiteral("ARM运行状态已读取：%1 · %2 · 模板%3。")
                .arg(usbRuntimeRunStateTextV2(status.runState),
                     usbRuntimeCanonicalPhaseTextV2(status.canonicalPhase),
                     usbRuntimeTemplateStateTextV2(status.templateState)),
            status};
}

BinaryObjectResult UsbFunctionfsTransport::readRuntimeStatusObject()
{
    const RuntimeStatusResultV2 result = readRuntimeStatus();
    if (!result.success) {
        return {false, result.message, m_runtimeStatusObject};
    }
    return {true, result.message, m_runtimeStatusObject};
}

UsbRuntimeProgressResultV9 UsbFunctionfsTransport::readRuntimeProgressV9()
{
    UsbRuntimeProgressResultV9 result;
    if (!m_backend || !m_backend->isOpen()) {
        result.message = QStringLiteral("USB未连接，无法读取ARM运行进度。");
        return result;
    }
    if (!m_initialized ||
        (m_extendedDiscovery.capabilities.activeFeatureMask &
         kUsbExtendedFeatureRuntimeControlV1) == 0U) {
        result.message = QStringLiteral("当前ARM未启用运行进度接口。");
        return result;
    }
    UsbFrameV1 response;
    QString error;
    if (!transact(UsbMessageTypeV1::RuntimeProgressV1, 0, {},
                  &response, &error, 500U)
        || response.flags != 9U) {
        result.message = error.isEmpty()
            ? QStringLiteral("ARM运行进度响应合同不匹配。") : error;
        return result;
    }
    result.raw = response.payload;
    if (!decodeUsbRuntimeProgressV9(response.payload, &result.progress,
                                    &error)) {
        result.message = error;
        return result;
    }
    result.success = true;
    result.message = QStringLiteral("%1 · %2‰ · %3")
        .arg(usbRuntimeStageTextV9(result.progress.stage))
        .arg(result.progress.overallPermille)
        .arg(usbRuntimeQualityTextV9(result.progress.qualityLevel));
    return result;
}

UsbRuntimeActionResultV9 UsbFunctionfsTransport::executeRuntimeActionV9(
    quint32 action, quint64 expectedGeneration)
{
    UsbRuntimeActionResultV9 result;
    if (!m_backend || !m_backend->isOpen()) {
        result.message = QStringLiteral("USB未连接，无法执行ARM运行动作。");
        return result;
    }
    if (!m_initialized ||
        (m_extendedDiscovery.capabilities.activeFeatureMask &
         kUsbExtendedFeatureRuntimeControlV1) == 0U) {
        result.message = QStringLiteral("当前ARM未启用运行动作接口。");
        return result;
    }
    quint64 transactionId = QRandomGenerator::global()->generate64();
    if (transactionId == 0U) transactionId = 1U;
    QString error;
    const QByteArray request = encodeUsbRuntimeActionV9(
        action, transactionId, expectedGeneration, &error);
    if (request.isEmpty()) { result.message = error; return result; }
    UsbFrameV1 response;
    qint32 wireStatus = 0;
    if (!transact(UsbMessageTypeV1::RuntimeActionV1, transactionId,
                  request, &response, &error, 3000U, &wireStatus)
        || response.flags != 9U) {
        result.message = error.isEmpty()
            ? QStringLiteral("ARM运行动作响应合同不匹配（%1）。").arg(wireStatus)
            : error;
        return result;
    }
    if (!decodeUsbRuntimeActionReceiptV9(response.payload, &result, &error)) {
        result.message = error;
        return result;
    }
    if (response.transactionId != transactionId
        || result.transactionId != transactionId) {
        result.success = false;
        result.message = QStringLiteral("ARM运行动作事务号不匹配，结果未采用。");
    }
    return result;
}

void UsbFunctionfsTransport::recordCompositeRuntimeStatus(
    const UsbCompositeRuntimeStatusResultV1 &result)
{
    m_protocolEvidence.insert(
        QStringLiteral("usb_crs1_available"), result.success);
    m_protocolEvidence.insert(
        QStringLiteral("usb_crs1_decode_code"),
        usbCompositeRuntimeDecodeCodeTextV1(result.code));
    m_protocolEvidence.insert(
        QStringLiteral("usb_crs1_bytes"), result.rawObject.size());
    m_protocolEvidence.insert(
        QStringLiteral("usb_crs1_local_policy_allowlisted"),
        m_compositeRuntimeTrustSource.ready());
    m_protocolEvidence.insert(
        QStringLiteral("usb_crs1_trust_source"),
        result.trustEvidence.isEmpty()
            ? m_compositeRuntimeTrustSource.evidence()
            : result.trustEvidence);
    if (!result.success) {
        m_protocolEvidence.insert(
            QStringLiteral("usb_crs1_error"), result.message);
        m_protocolEvidence.insert(
            QStringLiteral("usb_crs1_runtime_chain_ready"), false);
        return;
    }
    m_protocolEvidence.remove(QStringLiteral("usb_crs1_error"));
    m_protocolEvidence.insert(
        QStringLiteral("usb_crs1_schema"), result.status.schemaVersion);
    m_protocolEvidence.insert(
        QStringLiteral("usb_crs1_snapshot_generation"),
        QString::number(result.status.snapshotGeneration));
    m_protocolEvidence.insert(
        QStringLiteral("usb_crs1_configuration_generation"),
        QString::number(result.status.configurationGeneration));
    m_protocolEvidence.insert(
        QStringLiteral("usb_crs1_runtime_chain_ready"),
        result.status.runtimeChainReady);
    m_protocolEvidence.insert(
        QStringLiteral("usb_crs1_formal_measurement_credit"), false);
}

UsbCompositeRuntimeTrustResultV1
UsbFunctionfsTransport::installCompositeRuntimeTrust(
    const UsbCompositeRuntimeTrustInputV1 &input,
    const UsbCompositeRuntimeManifestVerifierV1 &verifier)
{
    // Installing or rejecting a new trust object invalidates every previously
    // decoded CRS1 snapshot. A CRS1 object can never populate this source.
    m_compositeRuntimeStatus = {};
    m_compositeRuntimeStatusObject.clear();
    const UsbCompositeRuntimeTrustResultV1 result =
        m_compositeRuntimeTrustSource.install(input, verifier);
    m_protocolEvidence.insert(QStringLiteral("usb_crs1_trust_source"),
                              result.evidence);
    if (!result.success) m_lastError = result.message;
    return result;
}

void UsbFunctionfsTransport::clearCompositeRuntimeTrust(
    const QString &reason)
{
    m_compositeRuntimeTrustSource.clear(reason);
    m_compositeRuntimeStatus = {};
    m_compositeRuntimeStatusObject.clear();
    m_protocolEvidence.insert(
        QStringLiteral("usb_crs1_trust_source"),
        m_compositeRuntimeTrustSource.evidence());
}

UsbCompositeRuntimeStatusResultV1
UsbFunctionfsTransport::readCompositeRuntimeStatus()
{
    UsbCompositeRuntimeStatusResultV1 unavailable;
    unavailable.snapshot.authoritativeContractBound = true;
    unavailable.trustEvidence = m_compositeRuntimeTrustSource.evidence();
    // Never retain or replay a prior CRS1 result across a new read attempt.
    m_compositeRuntimeStatus = {};
    m_compositeRuntimeStatusObject.clear();
    if (!m_backend || !m_backend->isOpen()) {
        unavailable.message = QStringLiteral(
            "USB未连接，无法读取ARM CRS1复合运行状态。");
        unavailable.snapshot.message = unavailable.message;
        recordCompositeRuntimeStatus(unavailable);
        return unavailable;
    }
    if (!m_extendedDiscovery.available
        || (m_extendedDiscovery.capabilities.activeFeatureMask
            & kUsbExtendedFeatureCompositeRuntimeStatusV1) == 0U) {
        unavailable.message = QStringLiteral(
            "ARM未激活USB CRS1复合运行状态能力。");
        unavailable.snapshot.message = unavailable.message;
        recordCompositeRuntimeStatus(unavailable);
        return unavailable;
    }
    const UsbCompositeRuntimePolicyV1 *policy =
        m_compositeRuntimeTrustSource.policy();
    if (policy == nullptr) {
        unavailable.message = QStringLiteral(
            "未显式注入并验证独立CSP1/system-manifest/product-descriptor及精确PL身份claims；禁止读取CRS1后自批准。");
        unavailable.snapshot.message = unavailable.message;
        recordCompositeRuntimeStatus(unavailable);
        return unavailable;
    }

    UsbFrameV1 response;
    QString error;
    if (!transact(UsbMessageTypeV1::CompositeRuntimeStatusV1, 0, {},
                  &response, &error, 3000U)
        || (response.flags & 0x0008U) == 0U
        || (response.flags & 0x0004U) != 0U) {
        unavailable.message = error.isEmpty()
            ? QStringLiteral("ARM CRS1响应未标记为完整只读对象。")
            : error;
        unavailable.snapshot.message = unavailable.message;
        m_lastError = unavailable.message;
        recordCompositeRuntimeStatus(unavailable);
        return unavailable;
    }

    m_compositeRuntimeStatusObject = response.payload;
    UsbCompositeRuntimeStatusResultV1 result =
        makeUsbCompositeRuntimeStatusResultV1(response.payload, policy);
    if (!result.success) {
        m_compositeRuntimeTrustSource.clear(QStringLiteral(
            "CRS1与已验证CSP1/system-manifest/ARM/PL身份绑定不一致或对象无效；信任已失效，必须重新注入。"));
        result.trustEvidence = m_compositeRuntimeTrustSource.evidence();
        m_lastError = result.message;
        recordCompositeRuntimeStatus(result);
        return result;
    }
    result.trustEvidence = m_compositeRuntimeTrustSource.evidence();
    m_compositeRuntimeStatus = result.status;
    recordCompositeRuntimeStatus(result);
    return result;
}

BinaryObjectResult UsbFunctionfsTransport::readCompositeRuntimeStatusObject()
{
    const UsbCompositeRuntimeStatusResultV1 result =
        readCompositeRuntimeStatus();
    return {result.success, result.message, result.rawObject};
}

ControlAuthorityResultV2 UsbFunctionfsTransport::submitControlAuthorityAction(
    quint32 action, quint32 requestedMode)
{
    if (m_initialized && m_extendedDiscovery.capabilities.protocolRevision == 9U)
        discoverExtendedV2();
    if (!m_initialized || !m_extendedDiscovery.available
        || (m_extendedDiscovery.capabilities.supportedFeatureMask
            & kUsbExtendedFeatureControlAuthorityV2) == 0U
        || (m_extendedDiscovery.capabilities.activeFeatureMask
            & kUsbExtendedFeatureControlAuthorityV2) == 0U) {
        return {false, QStringLiteral("ARM未提供可用的USB V2控制权接口。")};
    }
    ControlAuthorityResultV2 current = readControlAuthorityState();
    if (!current.success) return current;

    if (action == kUsbAuthorityActionSwitchModeV2
        && requestedMode == current.state.appliedMode) {
        return {false, QStringLiteral("设备已经处于目标控制模式。"),
                current.state};
    }
    if (action == kUsbAuthorityActionRenewLeaseV2
        && (current.state.appliedMode != kUsbControlModeHostManagedV2
            || current.state.phase != kUsbControlPhaseHostActiveV2)) {
        return {false, QStringLiteral("只有USB手动控制有效时才能续租。"),
                current.state};
    }
    if (action == kUsbAuthorityActionResumeHostV2
        && (current.state.appliedMode != kUsbControlModeHostManagedV2
            || current.state.phase != kUsbControlPhaseHostSafeWaitV2)) {
        return {false, QStringLiteral("设备不在USB租约失效安全等待态。"),
                current.state};
    }

    quint64 transactionId = QRandomGenerator::global()->generate64();
    while (transactionId == 0U
           || transactionId == current.state.lastTransactionId) {
        transactionId = QRandomGenerator::global()->generate64();
    }
    QString error;
    const QByteArray request = encodeUsbControlAuthorityRequestV2(
        action, requestedMode, transactionId, current.state.generation,
        &error);
    if (request.isEmpty()) return {false, error, current.state};

    UsbFrameV1 response;
    UsbControlAuthorityReceiptV2 receipt;
    ++m_transactions;
    if (!transact(UsbMessageTypeV1::AuthorityCommandV2, transactionId,
                  request, &response, &error, 3000U)
        || response.flags != 1U
        || !decodeUsbControlAuthorityReceiptV2(
            response.payload, &receipt, &error)
        || receipt.transactionId != transactionId
        || receipt.expectedGeneration != current.state.generation) {
        m_lastError = error.isEmpty()
            ? QStringLiteral("控制权终态回执与请求事务不一致。") : error;
        return {false, m_lastError, current.state, transactionId};
    }
    recordControlAuthority(receipt.state);
    if (receipt.kind != kUsbAuthorityReceiptAppliedV2
        || receipt.result != 0) {
        return {false,
                QStringLiteral("ARM拒绝控制权事务：status=%1（%2）。")
                    .arg(receipt.result)
                    .arg(usbWireStatusTextV1(receipt.result)),
                receipt.state, transactionId};
    }
    const bool requestedStateReached =
        (action == kUsbAuthorityActionSwitchModeV2
         && receipt.state.appliedMode == requestedMode
         && (requestedMode == kUsbControlModeHostManagedV2
                ? receipt.state.phase == kUsbControlPhaseHostActiveV2
                : receipt.state.phase >= kUsbControlPhaseAutonomousBootstrapV2
                  && receipt.state.phase <= kUsbControlPhaseAutonomousRearmV2))
        || ((action == kUsbAuthorityActionRenewLeaseV2
             || action == kUsbAuthorityActionResumeHostV2)
            && receipt.state.appliedMode == kUsbControlModeHostManagedV2
            && receipt.state.phase == kUsbControlPhaseHostActiveV2);
    if (!requestedStateReached) {
        return {false,
                QStringLiteral("ARM终态回执的控制模式或阶段不匹配。"),
                receipt.state, transactionId};
    }
    if (action == kUsbAuthorityActionSwitchModeV2)
        discoverExtendedV2();
    m_lastError.clear();
    return {true,
            action == kUsbAuthorityActionRenewLeaseV2
                ? QStringLiteral("USB手动控制租约已续期。")
                : QStringLiteral("控制模式切换完成：%1 · %2。")
                    .arg(usbControlModeTextV2(receipt.state.appliedMode),
                         usbControlPhaseTextV2(receipt.state.phase)),
            receipt.state, transactionId};
}

ControlAuthorityResultV2 UsbFunctionfsTransport::switchControlMode(
    quint32 requestedMode)
{
    return submitControlAuthorityAction(
        kUsbAuthorityActionSwitchModeV2, requestedMode);
}

ControlAuthorityResultV2 UsbFunctionfsTransport::renewControlLease()
{
    return submitControlAuthorityAction(
        kUsbAuthorityActionRenewLeaseV2, 0U);
}

ControlAuthorityResultV2 UsbFunctionfsTransport::resumeHostControl()
{
    return submitControlAuthorityAction(
        kUsbAuthorityActionResumeHostV2, 0U);
}

ConfigurationReceiptDetails UsbFunctionfsTransport::configurationReceiptDetails() const
{
    return {};
}

TransportResult UsbFunctionfsTransport::reconnect()
{
    if (!m_backend) {
        return {false, QStringLiteral("USB backend不存在。")};
    }
    invalidateSession(QStringLiteral("正在重新连接USB。"));
    m_initialized = false;
    m_failurePreservesActive = false;
    m_capabilities = {};
    m_extendedDiscovery = {};
    m_extendedCapabilitiesObject.clear();
    m_parameterCatalogObject.clear();
    m_controlAuthority = {};
    m_controlAuthorityObject.clear();
    m_runtimeStatus = {};
    m_runtimeStatusObject.clear();
    clearCompositeRuntimeTrust(QStringLiteral(
        "USB重连使CSP1/system-manifest信任上下文失效；必须重新显式注入。"));
    m_protocolEvidence = {};
    m_lastError.clear();
    if (!initialize()) {
        return {false, m_lastError};
    }
    return {true, QStringLiteral("WinUSB revision 9 已重新连接；配置状态由独立查询更新。")};
}

TransportPingResult UsbFunctionfsTransport::ping(const QByteArray &payload)
{
    if (!m_initialized || payload.size() > 4096) {
        return {false, QStringLiteral("Ping要求已连接USB和0–4096 B负载。")};
    }
    UsbFrameV1 response;
    QString error;
    QElapsedTimer timer;
    timer.start();
    const bool passed = transact(UsbMessageTypeV1::Ping, 0, payload,
                                 &response, &error, 3000U);
    const qint64 elapsed = timer.nsecsElapsed() / 1000;
    if (!passed || response.payload != payload) {
        m_lastError = passed
            ? QStringLiteral("Ping回显内容不一致。") : error;
        return {false, m_lastError,
                static_cast<quint32>(payload.size()), elapsed};
    }
    return {true,
            QStringLiteral("USB Ping PASS：%1 B，往返 %2 μs。")
                .arg(payload.size()).arg(elapsed),
            static_cast<quint32>(payload.size()), elapsed};
}

TransportResult UsbFunctionfsTransport::stageRam(
    const Configuration &)
{
    m_failurePreservesActive = true;
    return {false, QStringLiteral(
        "产品只支持USB revision 9独立型号／输入策略事务；旧CFG2路径已移除。")};
}

ReadbackResult UsbFunctionfsTransport::readBackStaged()
{
    return {false, {}, QStringLiteral("旧CFG2回读路径已移除。")};
}

TransportResult UsbFunctionfsTransport::confirmReadback()
{
    return {false, QStringLiteral("旧CFG2确认路径已移除。")};
}

void UsbFunctionfsTransport::rollbackStagedRam()
{
}

ProductReadState UsbFunctionfsTransport::readProductState()
{
    ProductReadState result;
    const quint64 requiredFeatures =
        kUsbExtendedFeatureDeviceModelControlV2
        | kUsbExtendedFeatureSystemInputPolicyControlV1;
    result.supported = m_initialized
        && m_extendedDiscovery.capabilities.protocolRevision == 9U
        && (m_extendedDiscovery.capabilities.activeFeatureMask
            & requiredFeatures) == requiredFeatures;
    if (!result.supported) {
        result.message = m_initialized
            ? QStringLiteral("设备能力未开放型号与输入策略查询；未发送USB请求。")
            : m_lastError;
        return result;
    }
    result.pairedInputStatus = m_productState.pairedInputStatus;
    auto readDomain = [&](productv9::Domain domain, quint16 firstMessage) {
        QJsonObject object;
        productv9::State state;
        UsbFrameV1 response;
        QString error;
        const quint64 requestId = m_nextSequence;
        const auto query = productv9::encodeQuery(domain, 4, requestId, &error);
        if (!transact(static_cast<UsbMessageTypeV1>(firstMessage), 0, query, &response, &error, 500U)
            || response.flags != 9U || !productv9::decodeState(response.payload, domain, &state, &error)
            || state.responseRequestId != requestId) {
            object["available"] = false;
            object["message"] = error.isEmpty() ? QStringLiteral("配置状态响应身份错误。") : error;
            return object;
        }
        object["state"] = state.fields;
        for (quint32 kind = 1; kind <= 2; ++kind) {
            productv9::Document doc;
            const quint64 id = m_nextSequence;
            const auto request = productv9::encodeQuery(domain, kind + 4, id, &error);
            if (!transact(static_cast<UsbMessageTypeV1>(firstMessage + kind), 0, request, &response, &error, 500U)
                || response.flags != 9U || !productv9::decodeDocument(response.payload, domain, &doc, &error)
                || doc.responseRequestId != id || doc.kind != kind
                || !productv9::documentMatchesState(doc, state)) {
                object = {};
                object["available"] = false;
                object["message"] = error.isEmpty() ? QStringLiteral("配置在查询期间变化，丢弃整组文档。") : error;
                return object;
            }
            object[kind == 1 ? "active_document" : "startup_document"] = doc.configuration;
            object[kind == 1 ? "active_identity" : "startup_identity"] = doc.fields;
        }
        object["available"] = true;
        return object;
    };
    result.deviceModel = readDomain(productv9::Domain::DeviceModel, 24);
    if (m_initialized) result.inputPolicy = readDomain(productv9::Domain::InputPolicy, 39);
    result.success = result.deviceModel["available"].toBool() && result.inputPolicy["available"].toBool();
    if (result.success) {
        const auto model = result.deviceModel["state"].toObject();
        const auto policy = result.inputPolicy["state"].toObject();
        for (const auto *key : {"bootId", "authorityGeneration", "sessionId", "systemSha256"}) {
            if (model[key] == policy[key]) continue;
            result.deviceModel = {{"available", false}, {"message", QStringLiteral("跨域配置身份在读取期间变化，已丢弃整组配置。")}};
            result.inputPolicy = {{"available", false}};
            result.success = false;
            break;
        }
    }
    result.message = result.success ? QStringLiteral("当前与开机配置已读取。")
        : result.deviceModel["message"].toString() + " " + result.inputPolicy["message"].toString();
    if (m_initialized) m_productState = result;
    return result;
}

QByteArray UsbFunctionfsTransport::readTelemetry(QString *error)
{
    const auto result = readTelemetrySnapshot();
    if (!result.success) { if (error) *error = result.message; return {}; }
    return m_productPublication;
}

TelemetrySnapshot UsbFunctionfsTransport::readTelemetrySnapshot()
{
    m_productPublication.clear(); m_productInput.clear();
    m_productState.pairedInputStatus = {};
    TelemetrySnapshot snapshot;
    snapshot.productResult = true;
    snapshot.diagnosticFieldsAvailable = false;
    if (!m_initialized) { snapshot.message = m_lastError; return snapshot; }
    QString error;
    const bool pairLegacyInput =
        (m_extendedDiscovery.capabilities.activeFeatureMask
         & kUsbExtendedFeatureResultInputStatusV1) != 0U;
    const bool pairDiagnostics =
        (m_extendedDiscovery.capabilities.activeFeatureMask
         & kUsbExtendedFeatureResultDiagnosticsV1) != 0U;
    for (int attempt = 0; attempt < 3; ++attempt) {
        UsbFrameV1 resultFrame, inputFrame, diagnosticsFrame;
        productv9::Publication publication;
        productv9::InputStatus input;
        productv9::ResultDiagnostics diagnostics;
        if (!transact(UsbMessageTypeV1::ResultSnapshotV1, 0, {}, &resultFrame, &error, 300U)
            || resultFrame.flags != 9U
            || !productv9::decodePublication(resultFrame.payload, &publication, &error)) {
            snapshot.message = error.isEmpty() ? QStringLiteral("正式结果只读响应合同不匹配。") : error;
            return snapshot;
        }
        if (!publication.valid && publication.identity.frameCounter == 0U) {
            snapshot.primaryReasonCode = publication.reason;
            snapshot.message = QStringLiteral("ARM当前无有效采集帧（原因码%1）；没有可配对输入状态，正式值不可用。").arg(publication.reason);
            return snapshot;
        }
        if (pairLegacyInput) {
            if (!transact(UsbMessageTypeV1::ResultInputStatusV1, 0, {}, &inputFrame, &error, 300U)
                || inputFrame.flags != 9U
                || !productv9::decodeInputStatus(inputFrame.payload, &input, &error)) {
                snapshot.message = error.isEmpty() ? QStringLiteral("正式结果只读响应合同不匹配。") : error;
                return snapshot;
            }
            if (!productv9::samePublication(publication.identity, input.identity)) continue;
        }
        if (pairDiagnostics) {
            QString diagnosticsError;
            if (!transact(UsbMessageTypeV1::ResultDiagnosticsV1, 0, {},
                          &diagnosticsFrame, &diagnosticsError, 300U)
                || diagnosticsFrame.flags != 9U
                || !productv9::decodeResultDiagnostics(
                    diagnosticsFrame.payload, &diagnostics,
                    &diagnosticsError)) {
                diagnostics = {};
            } else if (!productv9::resultDiagnosticsMatchesPublication(
                           diagnostics, publication.identity)) {
                // Message 31 is optional display data. A racing publication
                // hides only this extension; message 30 remains authoritative.
                diagnostics = {};
            }
        }
        const auto &id = publication.identity;
        const bool samePublisher = id.publisherGeneration == m_lastProductIdentity.publisherGeneration;
        if (m_productProgressTimer.isValid() && samePublisher
            && id.publishedMonotonicNs == m_lastProductIdentity.publishedMonotonicNs
            && !productv9::samePublication(id, m_lastProductIdentity)) {
            snapshot.message = QStringLiteral("相同发布时间对应不同帧身份，本轮已丢弃。");
            return snapshot;
        }
        if (samePublisher && (id.publishedMonotonicNs < m_lastProductIdentity.publishedMonotonicNs
            || (id.sessionId == m_lastProductIdentity.sessionId && id.sequence < m_lastProductIdentity.sequence))) {
            snapshot.message = QStringLiteral("正式结果出现倒序，本轮已丢弃。");
            return snapshot;
        }
        if (!m_productProgressTimer.isValid() || !samePublisher
            || id.publishedMonotonicNs > m_lastProductIdentity.publishedMonotonicNs) {
            m_lastProductIdentity = id;
            m_productProgressTimer.restart();
        } else if (m_productProgressTimer.elapsed() > 500) {
            snapshot.message = QStringLiteral("ARM结果超过500ms未推进，当前值已失效。");
            return snapshot;
        }
        snapshot.success = true;
        snapshot.observedUtcMs = QDateTime::currentMSecsSinceEpoch();
        snapshot.observedMonotonicNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        snapshot.message = publication.formalTotalAvailable
            ? QStringLiteral("ARM正式结果可用。") : QStringLiteral("ARM当前正式结果无效。");
        snapshot.generation = id.publisherGeneration;
        snapshot.publishedMonotonicNs = id.publishedMonotonicNs;
        snapshot.sessionId = id.sessionId; snapshot.sequence = id.sequence;
        snapshot.frameCounter = id.frameCounter; snapshot.captureRequestId = id.captureRequestId;
        const auto &v = publication.fields;
        snapshot.profile = v["profile"].toInt();
        snapshot.measurementValidMask = v["measurementValidMask"].toInt();
        snapshot.rodValidMask = v["forceRodValidMask"].toInt();
        snapshot.forceAvailableMask = publication.valid && publication.forceAvailable ? snapshot.rodValidMask : 0;
        snapshot.formalForceValid = publication.valid && publication.forceAvailable && publication.formalTotalAvailable;
        snapshot.formalTotalN = snapshot.formalForceValid ? v["formalTotalN"].toDouble() : 0;
        snapshot.imbalanceIndex = snapshot.formalForceValid ? v["formalImbalance"].toDouble() : 0;
        snapshot.primaryReasonCode = publication.reason;
        snapshot.flags = 1U | (snapshot.formalForceValid ? 8U : 0U);
        const auto forces = v["forceRodN"].toArray();
        const auto t0 = v["t0Sample"].toArray();
        const auto delta = v["nccDeltaNs"].toArray();
        const auto snr = v["snrDb"].toArray();
        for (int i = 0; i < 4; ++i) {
            snapshot.rod[i].forceN = forces[i].toDouble();
            snapshot.rod[i].currentT0Sample = t0[i].toDouble();
            snapshot.rod[i].delayNs = delta[i].toDouble();
            snapshot.rod[i].snrDb = snr[i].toDouble();
        }
        if ((diagnostics.flags
             & productv9::kResultDiagnosticsValidV1) != 0U) {
            snapshot.diagnosticFieldsAvailable = true;
            snapshot.strainAvailableMask = diagnostics.rodValidMask;
            snapshot.lowLoadBiasInvalid = (diagnostics.flags
                & productv9::kResultDiagnosticsLowLoadBiasInvalidV1) != 0U;
            snapshot.biasValidMinTotalForceN =
                diagnostics.biasValidMinTotalForceN;
            snapshot.forcePredictionMask = diagnostics.predictionMask;
            snapshot.forceQualityDegradedMask =
                diagnostics.qualityDegradedMask;
            snapshot.forceCommonTrendOutlierMask =
                diagnostics.commonTrendOutlierMask;
            snapshot.plcState = diagnostics.plcState;
            snapshot.plcStale = (diagnostics.flags
                & productv9::kResultDiagnosticsPlcStaleV1) != 0U;
            snapshot.agcState = diagnostics.agcState;
            snapshot.agcReason = diagnostics.agcReason;
            snapshot.actualPgaDb = diagnostics.actualPgaDb;
            snapshot.actualVcntlDac = diagnostics.actualVcntlDac;
            snapshot.actualHvVolts = diagnostics.actualHvVolts;
            snapshot.actualBurstCycles = diagnostics.actualBurstCycles;
            for (int i = 0; i < 4; ++i) {
                snapshot.rod[i].strainMicrostrain =
                    diagnostics.strainMicrostrain[i];
                snapshot.rod[i].forceReason = diagnostics.invalidReason[i];
                snapshot.actualLnaDb[i] = diagnostics.actualLnaDb[i];
            }
        }
        if (pairLegacyInput) {
            snapshot.pairedInputStatus = input.fields;
            m_productState.pairedInputStatus = input.fields;
            m_productInput = inputFrame.payload;
        }
        m_productPublication = resultFrame.payload;
        return snapshot;
    }
    snapshot.message = QStringLiteral("结果与输入状态连续三组身份不匹配，本轮已丢弃。");
    return snapshot;
}

QByteArray UsbFunctionfsTransport::listLogs(QString *error)
{
    if (!m_initialized) { if (error) *error = m_lastError; return {}; }
    if (!m_extendedDiscovery.available ||
        (m_extendedDiscovery.capabilities.activeFeatureMask &
         kUsbExtendedFeatureLogCatalogV2) == 0U) {
        if (error) *error = QStringLiteral("ARM未激活日志目录能力。");
        return {};
    }
    UsbFrameV1 response;
    const auto request = productv9::encodeLogCatalogRequest();
    if (!transact(UsbMessageTypeV1::LogCatalogV2, 0, request, &response, error, 500U)
        || response.flags != 9U) return {};
    return response.payload;
}

QByteArray UsbFunctionfsTransport::readLog(quint32 sourceId, quint64 offset,
    quint32 maximumBytes, quint64 snapshotId, QString *error)
{
    if (!m_initialized || sourceId == 0 || sourceId > quint32(m_logEntries.size())) {
        if (error) *error = QStringLiteral("请先刷新日志目录。"); return {};
    }
    const auto entry = m_logEntries[int(sourceId - 1)];
    if (entry.snapshotId != snapshotId) {
        if (error) *error = QStringLiteral("日志目录身份已变化。"); return {};
    }
    const auto request = productv9::encodeLogReadRequest(entry, offset, maximumBytes, error);
    if (request.isEmpty()) return {};
    UsbFrameV1 response;
    qint32 status = 0;
    if (!transact(UsbMessageTypeV1::LogReadV2, 0, request, &response, error, 500U, &status)
        || (response.flags != 9U && response.flags != 13U)) {
        if (status == 6 || status == 12) m_logEntries.clear();
        return {};
    }
    productv9::LogChunk chunk;
    if (!productv9::decodeLogChunk(response.payload, entry, offset, maximumBytes, &chunk, error)
        || bool(response.flags & 4U) != chunk.more) { m_logEntries.clear(); return {}; }
    return response.payload;
}

DeviceLogListResult UsbFunctionfsTransport::readLogSources()
{
    m_logEntries.clear();
    DeviceLogListResult result;
    QString error;
    const auto payload = listLogs(&error);
    productv9::LogCatalog catalog;
    if (payload.isEmpty() || !productv9::decodeLogCatalog(payload, &catalog, &error)) {
        result.message = error; return result;
    }
    m_logEntries = catalog.entries;
    for (int i = 0; i < m_logEntries.size(); ++i) {
        const auto &entry = m_logEntries[i];
        DeviceLogSource source;
        source.sourceId = quint32(i + 1); source.available = entry.flags & 1U;
        source.totalBytes = entry.totalBytes; source.modifiedTimeNs = entry.modifiedTimeNs;
        source.snapshotId = entry.snapshotId; source.fileIdentityCrc32 = entry.fileIdentityCrc32;
        source.name = entry.name; source.mutableFile = entry.flags & 2U;
        source.kind = entry.kind;
        result.sources.append(source);
    }
    result.success = true;
    result.message = QStringLiteral("已读取%1个日志文件（设备目录总数%2）。")
        .arg(result.sources.size()).arg(catalog.totalEntries);
    return result;
}

DeviceLogChunkResult UsbFunctionfsTransport::readLogChunk(quint32 sourceId,
    quint64 offset, quint32 maximumBytes, quint64 snapshotId)
{
    DeviceLogChunkResult result;
    QString error;
    const auto payload = readLog(sourceId, offset, maximumBytes, snapshotId, &error);
    if (payload.isEmpty()) { result.message = error; return result; }
    const auto entry = m_logEntries[int(sourceId - 1)];
    productv9::LogChunk chunk;
    if (!productv9::decodeLogChunk(payload, entry, offset, maximumBytes, &chunk, &error)) {
        result.message = error; return result;
    }
    result.success = true; result.sourceId = sourceId; result.offset = offset;
    result.totalBytes = chunk.totalBytes; result.snapshotId = snapshotId;
    result.fileIdentityCrc32 = entry.fileIdentityCrc32; result.more = chunk.more; result.data = chunk.data;
    return result;
}

QByteArray UsbFunctionfsTransport::readWaveform(
    quint64 expectedGeneration, quint32 sliceOffset, QString *error)
{
    if (m_capabilities.waveformFlags == 0U) {
        if (error) *error = QStringLiteral("ARM未激活波形能力。");
        return {};
    }
    QByteArray request;
    appendU64(request, expectedGeneration);
    appendU32(request, 2048U * 4U * 2U);
    // Revision-9 message 7 keeps its 32-byte payload: +12 is the sample
    // offset in the retained 8192-point ARM frame and +16..31 stay reserved.
    appendU32(request, sliceOffset);
    request.append(QByteArray(16, '\0'));
    UsbFrameV1 response;
    if (!transact(UsbMessageTypeV1::Waveform, 0, request, &response, error,
                  300U)) return {};
    if (response.payload.size() != 64 + 2048 * 4 * 2
        || readU32(response.payload, 40) != 2048U
        || readU32(response.payload, 44) != 4U
        || readU32(response.payload, 48) != 2U
        || readU32(response.payload, 56) != 1U
        || readU32(response.payload, 60) != sliceOffset
        || (expectedGeneration != 0U
            && readU64(response.payload, 0) != expectedGeneration)
        || readU32(response.payload, 52)
            != usbCrc32V1(response.payload.mid(64))) {
        if (error != nullptr)
            *error = QStringLiteral("2048×4 波形身份、偏移或长度无效。");
        return {};
    }
    return response.payload;
}

WaveformSnapshot UsbFunctionfsTransport::readWaveformSnapshot()
{
    WaveformViewportRequest request;
    const auto result = readWaveformViewport(request);
    WaveformSnapshot snapshot = result.waveform;
    if (snapshot.success)
        snapshot.message = QStringLiteral("最新四杆波形读取成功。");
    return snapshot;
}

WaveformViewportResult UsbFunctionfsTransport::readWaveformViewport(
    const WaveformViewportRequest &request)
{
    WaveformViewportResult result;
    result.supported = supportsWaveformViewport();
    result.sliceOffset = request.sliceOffset;
    if (request.sampleCount != 2048U || request.sliceOffset > 6144U) {
        result.message = QStringLiteral(
            "波形切片必须为2048点，偏移范围0到6144。");
        return result;
    }
    WaveformSnapshot snapshot;
    QString error;
    const QByteArray payload = readWaveform(
        request.expectedGeneration, request.sliceOffset, &error);

    if (payload.size() != 64 + 2048 * 4 * 2) {
        snapshot.message = error.isEmpty()
            ? QStringLiteral("波形对象长度无效。") : error;
        result.message = snapshot.message;
        result.waveform = snapshot;
        return result;
    }
    snapshot.generation = readU64(payload, 0);
    snapshot.sessionId = readU64(payload, 8);
    snapshot.sequence = readU64(payload, 16);
    snapshot.frameCounter = readU64(payload, 24);
    snapshot.sampleRateHz = readU32(payload, 32);
    snapshot.windowStart = readU32(payload, 36);
    for (int rod = 0; rod < 4; ++rod) {
        QVector<qint16> &samples = snapshot.rodSamples[rod];
        samples.resize(2048);
        const uchar *source = reinterpret_cast<const uchar *>(
            payload.constData() + 64 + rod * 2048 * 2);
        for (int index = 0; index < 2048; ++index) {
            samples[index] = qFromLittleEndian<qint16>(source + index * 2);
        }
    }
    snapshot.success = true;
    snapshot.message = request.sliceOffset == 0U
        ? QStringLiteral("最新四杆波形读取成功。")
        : QStringLiteral("四杆波形切片读取成功（偏移%1）。")
            .arg(request.sliceOffset);
    result.success = true;
    result.message = snapshot.message;
    result.waveform = snapshot;
    return result;
}

} // namespace ucm
