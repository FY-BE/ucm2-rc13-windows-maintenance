#include "libusb_backend.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>

#include <libusb.h>

#include <QJsonArray>

#include <algorithm>
#include <iterator>
#include <limits>

namespace ucm {
namespace {

QString usbHex(quint16 value)
{
    return QStringLiteral("0x")
        + QString::number(value, 16).rightJustified(4, QLatin1Char('0'))
              .toUpper();
}

QString endpointHex(quint8 value)
{
    return QStringLiteral("0x")
        + QString::number(value, 16).rightJustified(2, QLatin1Char('0'))
              .toUpper();
}

QString libusbError(int code)
{
    return QStringLiteral("%1 (%2)")
        .arg(QString::fromLatin1(libusb_error_name(code)))
        .arg(code);
}

UsbBackendResult normalizedResult(int code)
{
    if (code == LIBUSB_SUCCESS) {
        return UsbBackendResult::Ok;
    }
    if (code == LIBUSB_ERROR_TIMEOUT) {
        return UsbBackendResult::Timeout;
    }
    if (code == LIBUSB_ERROR_NO_DEVICE) {
        return UsbBackendResult::Disconnected;
    }
    return UsbBackendResult::IoError;
}

bool endpointContractMatches(libusb_device *device,
                             const UsbDeviceIdentity &identity,
                             QJsonObject *evidence,
                             QString *error)
{
    libusb_config_descriptor *configuration = nullptr;
    const int status = libusb_get_config_descriptor_by_value(
        device, identity.configuration, &configuration);
    if (status != LIBUSB_SUCCESS || configuration == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("读取 USB configuration %1 失败：%2。")
                         .arg(identity.configuration)
                         .arg(libusbError(status));
        }
        return false;
    }

    bool interfaceFound = false;
    bool outFound = false;
    bool inFound = false;
    QJsonArray endpoints;
    for (int interfaceIndex = 0;
         interfaceIndex < configuration->bNumInterfaces;
         ++interfaceIndex) {
        const libusb_interface &interface =
            configuration->interface[interfaceIndex];
        for (int alternate = 0; alternate < interface.num_altsetting;
             ++alternate) {
            const libusb_interface_descriptor &descriptor =
                interface.altsetting[alternate];
            if (descriptor.bInterfaceNumber != identity.interfaceNumber
                || descriptor.bAlternateSetting != 0U) {
                continue;
            }
            interfaceFound = true;
            for (int endpointIndex = 0;
                 endpointIndex < descriptor.bNumEndpoints;
                 ++endpointIndex) {
                const libusb_endpoint_descriptor &endpoint =
                    descriptor.endpoint[endpointIndex];
                const bool bulk =
                    (endpoint.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK)
                    == LIBUSB_TRANSFER_TYPE_BULK;
                endpoints.append(endpointHex(endpoint.bEndpointAddress));
                if (bulk
                    && endpoint.bEndpointAddress == identity.outEndpoint) {
                    outFound = true;
                }
                if (bulk
                    && endpoint.bEndpointAddress == identity.inEndpoint) {
                    inFound = true;
                }
            }
        }
    }
    if (evidence != nullptr) {
        evidence->insert(QStringLiteral("enumerated_endpoints"), endpoints);
        evidence->insert(QStringLiteral("interface_found"), interfaceFound);
        evidence->insert(QStringLiteral("bulk_out_found"), outFound);
        evidence->insert(QStringLiteral("bulk_in_found"), inFound);
    }
    libusb_free_config_descriptor(configuration);
    if (!interfaceFound || !outFound || !inFound) {
        if (error != nullptr) {
            *error = QStringLiteral(
                "目标 USB 的 configuration/interface/bulk endpoint 合同不匹配。");
        }
        return false;
    }
    return true;
}

bool winUsbDriverMatches(const UsbDeviceIdentity &identity,
                         QJsonObject *evidence, QString *error)
{
    HDEVINFO devices = SetupDiGetClassDevsW(
        nullptr, L"USB", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) {
        if (error != nullptr) {
            *error = QStringLiteral("Windows PnP USB 枚举失败，error=%1。")
                         .arg(GetLastError());
        }
        return false;
    }

    const QString identityText = QStringLiteral("VID_%1&PID_%2")
        .arg(identity.vendorId, 4, 16, QLatin1Char('0'))
        .arg(identity.productId, 4, 16, QLatin1Char('0'))
        .toUpper();
    int matches = 0;
    QString service;
    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA deviceInfo {};
        deviceInfo.cbSize = sizeof(deviceInfo);
        if (!SetupDiEnumDeviceInfo(devices, index, &deviceInfo)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS && error != nullptr) {
                *error = QStringLiteral("Windows PnP 设备遍历失败，error=%1。")
                             .arg(GetLastError());
            }
            break;
        }
        wchar_t hardwareIds[2048] {};
        DWORD type = 0;
        DWORD requiredBytes = 0;
        if (!SetupDiGetDeviceRegistryPropertyW(
                devices, &deviceInfo, SPDRP_HARDWAREID, &type,
                reinterpret_cast<PBYTE>(hardwareIds),
                sizeof(hardwareIds), &requiredBytes)) {
            continue;
        }
        const int characterCount = static_cast<int>(
            std::min<DWORD>(requiredBytes / sizeof(wchar_t),
                            static_cast<DWORD>(std::size(hardwareIds))));
        const QString ids = QString::fromWCharArray(
            hardwareIds, characterCount).toUpper();
        if (!ids.contains(identityText)) {
            continue;
        }
        ++matches;
        wchar_t serviceName[256] {};
        if (SetupDiGetDeviceRegistryPropertyW(
                devices, &deviceInfo, SPDRP_SERVICE, &type,
                reinterpret_cast<PBYTE>(serviceName),
                sizeof(serviceName), &requiredBytes)) {
            service = QString::fromWCharArray(serviceName);
        }
    }
    SetupDiDestroyDeviceInfoList(devices);
    const bool matched = matches == 1
        && service.compare(QStringLiteral("WinUSB"),
                           Qt::CaseInsensitive) == 0;
    if (evidence != nullptr) {
        evidence->insert(QStringLiteral("windows_pnp_match_count"), matches);
        evidence->insert(QStringLiteral("windows_usb_service"), service);
        evidence->insert(QStringLiteral("winusb_driver_verified"), matched);
    }
    if (!matched && error != nullptr) {
        *error = matches != 1
            ? QStringLiteral("Windows PnP 未找到唯一目标 USB。")
            : QStringLiteral("目标 USB 未绑定 WinUSB，当前 service=%1。")
                  .arg(service.isEmpty() ? QStringLiteral("<empty>") : service);
    }
    return matched;
}

} // namespace

class LibusbBackend::Impl {
public:
    libusb_context *context = nullptr;
    libusb_device_handle *handle = nullptr;
    std::unique_ptr<UsbTransferEngine> transfer;
    UsbDeviceIdentity identity;
    QJsonObject identityEvidence;
    QString lastError;
    quint64 openAttempts = 0;
    quint64 successfulOpens = 0;
    bool interfaceClaimed = false;
};

LibusbBackend::LibusbBackend()
    : m_impl(std::make_unique<Impl>())
{
    m_impl->identity = requiredIdentity();
}

LibusbBackend::~LibusbBackend()
{
    close();
}

UsbDeviceIdentity LibusbBackend::requiredIdentity()
{
    return {};
}

UsbOpenResult LibusbBackend::open()
{
    close();
    ++m_impl->openAttempts;
    const UsbDeviceIdentity identity = m_impl->identity;
    QJsonObject evidence{
        {QStringLiteral("backend"), QStringLiteral("libusb-1.0/WinUSB")},
        {QStringLiteral("vid"), usbHex(identity.vendorId)},
        {QStringLiteral("pid"), usbHex(identity.productId)},
        {QStringLiteral("configuration"), identity.configuration},
        {QStringLiteral("interface"), identity.interfaceNumber},
        {QStringLiteral("bulk_out_endpoint"),
         endpointHex(identity.outEndpoint)},
        {QStringLiteral("bulk_in_endpoint"),
         endpointHex(identity.inEndpoint)},
        {QStringLiteral("real_usb_opened"), false},
        {QStringLiteral("nonvolatile_write"), false}
    };

    int status = libusb_init(&m_impl->context);
    if (status != LIBUSB_SUCCESS) {
        m_impl->lastError = QStringLiteral("libusb 初始化失败：%1。")
                                .arg(libusbError(status));
        m_impl->identityEvidence = evidence;
        return {false, m_impl->lastError, evidence};
    }

    libusb_device **devices = nullptr;
    const ssize_t count = libusb_get_device_list(m_impl->context, &devices);
    if (count < 0) {
        m_impl->lastError = QStringLiteral("USB 枚举失败：%1。")
                                .arg(libusbError(static_cast<int>(count)));
        m_impl->identityEvidence = evidence;
        close();
        return {false, m_impl->lastError, evidence};
    }

    libusb_device *selected = nullptr;
    int matches = 0;
    libusb_device_descriptor selectedDescriptor {};
    for (ssize_t index = 0; index < count; ++index) {
        libusb_device_descriptor descriptor {};
        if (libusb_get_device_descriptor(devices[index], &descriptor)
                == LIBUSB_SUCCESS
            && descriptor.idVendor == identity.vendorId
            && descriptor.idProduct == identity.productId) {
            ++matches;
            if (selected == nullptr) {
                selected = libusb_ref_device(devices[index]);
                selectedDescriptor = descriptor;
            }
        }
    }
    libusb_free_device_list(devices, 1);
    evidence.insert(QStringLiteral("matching_device_count"), matches);
    if (matches != 1 || selected == nullptr) {
        if (selected != nullptr) {
            libusb_unref_device(selected);
        }
        m_impl->lastError = matches == 0
            ? QStringLiteral("未发现唯一目标 USB 设备。")
            : QStringLiteral("发现多个同 VID/PID 设备，拒绝猜测目标。");
        m_impl->identityEvidence = evidence;
        close();
        return {false, m_impl->lastError, evidence};
    }

    QString contractError;
    if (!winUsbDriverMatches(identity, &evidence, &contractError)
        || !endpointContractMatches(selected, identity, &evidence,
                                 &contractError)) {
        libusb_unref_device(selected);
        m_impl->lastError = contractError;
        m_impl->identityEvidence = evidence;
        close();
        return {false, m_impl->lastError, evidence};
    }

    status = libusb_open(selected, &m_impl->handle);
    evidence.insert(QStringLiteral("bus_number"),
                    libusb_get_bus_number(selected));
    evidence.insert(QStringLiteral("device_address"),
                    libusb_get_device_address(selected));
    evidence.insert(QStringLiteral("device_bcd"),
                    usbHex(selectedDescriptor.bcdDevice));
    libusb_unref_device(selected);
    if (status != LIBUSB_SUCCESS || m_impl->handle == nullptr) {
        m_impl->lastError = QStringLiteral("打开目标 USB 失败：%1。")
                                .arg(libusbError(status));
        m_impl->identityEvidence = evidence;
        close();
        return {false, m_impl->lastError, evidence};
    }

    int currentConfiguration = 0;
    status = libusb_get_configuration(m_impl->handle,
                                      &currentConfiguration);
    if (status != LIBUSB_SUCCESS) {
        m_impl->lastError = QStringLiteral("读取活动 USB configuration 失败：%1。")
                                .arg(libusbError(status));
        m_impl->identityEvidence = evidence;
        close();
        return {false, m_impl->lastError, evidence};
    }
    if (currentConfiguration != identity.configuration) {
        status = libusb_set_configuration(m_impl->handle,
                                          identity.configuration);
        if (status != LIBUSB_SUCCESS) {
            m_impl->lastError = QStringLiteral("切换 USB configuration 失败：%1。")
                                    .arg(libusbError(status));
            m_impl->identityEvidence = evidence;
            close();
            return {false, m_impl->lastError, evidence};
        }
    }

    (void)libusb_set_auto_detach_kernel_driver(m_impl->handle, 1);
    status = libusb_claim_interface(m_impl->handle,
                                    identity.interfaceNumber);
    if (status != LIBUSB_SUCCESS) {
        m_impl->lastError = QStringLiteral("独占 USB interface 失败：%1。")
                                .arg(libusbError(status));
        m_impl->identityEvidence = evidence;
        close();
        return {false, m_impl->lastError, evidence};
    }
    m_impl->interfaceClaimed = true;
    m_impl->transfer = std::make_unique<UsbTransferEngine>(
        identity.outEndpoint, identity.inEndpoint,
        [this](quint8 endpoint, unsigned char *data, int length,
               int *transferred, unsigned timeoutMs) {
            if (m_impl->handle == nullptr) {
                if (transferred != nullptr) {
                    *transferred = 0;
                }
                return UsbBackendResult::Disconnected;
            }
            const int status = libusb_bulk_transfer(
                m_impl->handle, endpoint, data, length, transferred,
                timeoutMs);
            return normalizedResult(status);
        });
    ++m_impl->successfulOpens;
    evidence.insert(QStringLiteral("real_usb_opened"), true);
    evidence.insert(QStringLiteral("driver_contract"),
                    QStringLiteral("WinUSB verified"));
    m_impl->identityEvidence = evidence;
    m_impl->lastError.clear();
    return {true, QStringLiteral("目标 USB 身份及 bulk endpoint 合同已确认。"),
            evidence};
}

void LibusbBackend::close()
{
    if (!m_impl) {
        return;
    }
    m_impl->transfer.reset();
    if (m_impl->handle != nullptr) {
        if (m_impl->interfaceClaimed) {
            (void)libusb_release_interface(
                m_impl->handle, m_impl->identity.interfaceNumber);
        }
        libusb_close(m_impl->handle);
    }
    m_impl->handle = nullptr;
    m_impl->interfaceClaimed = false;
    if (m_impl->context != nullptr) {
        libusb_exit(m_impl->context);
    }
    m_impl->context = nullptr;
}

bool LibusbBackend::isOpen() const
{
    return m_impl && m_impl->handle != nullptr
        && m_impl->interfaceClaimed && m_impl->transfer != nullptr;
}

UsbTransferResult LibusbBackend::writeAll(
    const QByteArray &bytes, unsigned timeoutMs)
{
    if (!isOpen()) {
        return {UsbTransferStatus::Disconnected, 0, {},
                QStringLiteral("真实 USB 尚未打开。")};
    }
    return m_impl->transfer->writeAll(bytes, timeoutMs);
}

UsbTransferResult LibusbBackend::readSome(
    int maximumBytes, unsigned timeoutMs)
{
    if (!isOpen()) {
        return {UsbTransferStatus::Disconnected, 0, {},
                QStringLiteral("真实 USB 尚未打开。")};
    }
    return m_impl->transfer->readSome(maximumBytes, timeoutMs);
}

UsbTransferResult LibusbBackend::readExact(int bytes, unsigned timeoutMs)
{
    if (!isOpen() || bytes <= 0
        || bytes > UsbTransferEngine::kMaximumTransferBytes) {
        return {UsbTransferStatus::ContractError, 0, {},
                QStringLiteral("USB 精确读取参数无效或设备未打开。")};
    }
    QByteArray result;
    result.reserve(bytes);
    while (result.size() < bytes) {
        const UsbTransferResult chunk = readSome(bytes - result.size(), timeoutMs);
        if (!chunk.success()) {
            UsbTransferResult failure = chunk;
            failure.transferred = result.size() + chunk.transferred;
            failure.data = result + chunk.data;
            return failure;
        }
        result.append(chunk.data);
    }
    return {UsbTransferStatus::Success, result.size(), result, {}};
}

UsbTransferStats LibusbBackend::transferStats() const
{
    return m_impl && m_impl->transfer
        ? m_impl->transfer->stats() : UsbTransferStats {};
}

QJsonObject LibusbBackend::evidence() const
{
    QJsonObject result = m_impl ? m_impl->identityEvidence : QJsonObject {};
    result.insert(QStringLiteral("open_attempts"),
                  static_cast<double>(m_impl ? m_impl->openAttempts : 0U));
    result.insert(QStringLiteral("successful_opens"),
                  static_cast<double>(m_impl ? m_impl->successfulOpens : 0U));
    result.insert(QStringLiteral("is_open"), isOpen());
    if (m_impl != nullptr) {
        result.insert(QStringLiteral("last_error"), m_impl->lastError);
    }
    const UsbTransferStats stats = transferStats();
    result.insert(QStringLiteral("write_operations"),
                  static_cast<double>(stats.writeOperations));
    result.insert(QStringLiteral("read_operations"),
                  static_cast<double>(stats.readOperations));
    result.insert(QStringLiteral("backend_calls"),
                  static_cast<double>(stats.backendCalls));
    result.insert(QStringLiteral("bytes_written"),
                  static_cast<double>(stats.bytesWritten));
    result.insert(QStringLiteral("bytes_read"),
                  static_cast<double>(stats.bytesRead));
    result.insert(QStringLiteral("timeouts"),
                  static_cast<double>(stats.timeouts));
    result.insert(QStringLiteral("disconnects"),
                  static_cast<double>(stats.disconnects));
    result.insert(QStringLiteral("io_errors"),
                  static_cast<double>(stats.ioErrors));
    return result;
}

} // namespace ucm
