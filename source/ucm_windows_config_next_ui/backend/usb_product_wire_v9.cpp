#include "usb_product_wire_v9.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <limits>

namespace ucm::productv9
{
namespace
{
quint16 u16(const QByteArray &b, int o)
{
    return qFromLittleEndian<quint16>(b.constData() + o);
}
quint32 u32(const QByteArray &b, int o)
{
    return qFromLittleEndian<quint32>(b.constData() + o);
}
quint64 u64(const QByteArray &b, int o)
{
    return qFromLittleEndian<quint64>(b.constData() + o);
}
void p16(QByteArray &b, int o, quint16 v)
{
    qToLittleEndian(v, b.data() + o);
}
void p32(QByteArray &b, int o, quint32 v)
{
    qToLittleEndian(v, b.data() + o);
}
void p64(QByteArray &b, int o, quint64 v)
{
    qToLittleEndian(v, b.data() + o);
}
double f64(const QByteArray &b, int o)
{
    const auto v = u64(b, o);
    double d;
    std::memcpy(&d, &v, 8);
    return d;
}
bool zero(const QByteArray &b, int o, int n)
{
    for (int i = o; i < o + n; ++i)
        if (b[i])
            return false;
    return true;
}
bool fail(QString *e, const char *s)
{
    if (e)
        *e = QString::fromLatin1(s);
    return false;
}
bool checksum(const QByteArray &b, int o, int n = 0, bool nonzero = true)
{
    QByteArray c = n ? b.left(n) : b;
    const auto value = u32(c, o);
    p32(c, o, 0);
    return (!nonzero || value) && value == crc32(c);
}
void seal(QByteArray &b, int o)
{
    p32(b, o, 0);
    p32(b, o, crc32(b));
}
quint32 token(Domain d)
{
    return d == Domain::DeviceModel ? 0x32434455U : 0x31504955U;
}
quint32 abi(Domain d)
{
    return d == Domain::DeviceModel ? 2 : 1;
}
bool objectHeader(const QByteArray &b, int size, quint32 magic, quint16 version, bool nonzeroCrc = true)
{
    return b.size() == size && u32(b, 0) == magic && u16(b, 4) == version && u16(b, 6) == size &&
           checksum(b, 8, 0, nonzeroCrc);
}
void idField(QJsonObject &j, const char *key, quint64 v)
{
    j[QString::fromLatin1(key)] = QString::number(v);
}
PublicationIdentity identity(const QByteArray &b)
{
    return {u64(b, 16), u64(b, 24), u64(b, 32), u64(b, 40), u64(b, 48), u64(b, 56)};
}
QJsonObject identityJson(const PublicationIdentity &i)
{
    QJsonObject j;
    idField(j, "publisherGeneration", i.publisherGeneration);
    idField(j, "publishedMonotonicNs", i.publishedMonotonicNs);
    idField(j, "sessionId", i.sessionId);
    idField(j, "sequence", i.sequence);
    idField(j, "frameCounter", i.frameCounter);
    idField(j, "captureRequestId", i.captureRequestId);
    return j;
}
bool identityValid(const PublicationIdentity &i)
{
    return i.publisherGeneration && i.publishedMonotonicNs && i.sessionId && i.sequence && i.captureRequestId;
}
QByteArray digest(const QByteArray &b)
{
    return QCryptographicHash::hash(b, QCryptographicHash::Sha256);
}
int enumText(const QJsonObject &j, const char *key, const QStringList &values)
{
    return values.indexOf(j[QString::fromLatin1(key)].toString()) + 1;
}
QStringList documentKeys(Domain domain, int schemaVersion)
{
    if (domain == Domain::InputPolicy)
        return {"schema_version",           "configuration_generation",
                "system_package_id_sha256", "system_input_policy_id_sha256",
                "plc_interlock_policy",     "plc_state_encoding"};
    QStringList keys{"schema_version",
            "configuration_generation",
            "system_package_id_sha256",
            "device_model_config_id_sha256",
            "device_model_id",
            "applicability_id",
            "pack_id",
            "model_name",
            "applicability_name",
            "kmat_unified",
            "coupling_bias_ns"};
    if (schemaVersion == 3)
        keys << "force_correction_knot_count"
             << "force_correction_input_n"
             << "force_correction_output_n";
    keys << "l_total_mm"
         << "l_b_mm"
         << "l_d_mm"
         << "l_e_mm"
         << "abeq_mm2"
         << "ac_mm2"
         << "adeq_mm2"
         << "phi_b"
         << "phi_d"
         << "rod_diameter_mm"
         << "thread_root_diameter_mm"
         << "mold_source_mode"
         << "fixed_mold_thickness_mm"
         << "geometry_model"
         << "mold_reference_mm"
         << "body_reference_mm"
         << "min_ncc_peak"
         << "open_stable_confirm_frames";
    return keys;
}
bool exactKeys(const QJsonObject &object, Domain domain)
{
    const auto expected = documentKeys(domain, object["schema_version"].toInt());
    if (object.size() != expected.size())
        return false;
    for (const auto &key : expected)
        if (!object.contains(key))
            return false;
    return true;
}
bool strictJson(const QByteArray &json, QJsonObject *out, QStringList *orderedKeys = nullptr)
{
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString decoded = utf8.decode(json);
    Q_UNUSED(decoded);
    if (utf8.hasError())
        return false;
    QJsonParseError pe;
    const auto doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    QSet<QString> keys;
    int depth = 0;
    for (qsizetype i = 0; i < json.size(); ++i)
    {
        if (json[i] == '{')
        {
            if (++depth > 1)
                return false;
        }
        else if (json[i] == '}')
            --depth;
        else if (json[i] == '"')
        {
            const auto start = i;
            ++i;
            while (i < json.size() && json[i] != '"')
            {
                if (json[i] == '\\')
                    ++i;
                ++i;
            }
            if (i >= json.size())
                return false;
            auto next = i + 1;
            while (next < json.size() &&
                   (json[next] == ' ' || json[next] == '\t' || json[next] == '\r' || json[next] == '\n'))
                ++next;
            if (next < json.size() && json[next] == ':')
            {
                const auto key =
                    QJsonDocument::fromJson("[" + json.mid(start, i - start + 1) + "]").array().at(0).toString();
                if (keys.contains(key))
                    return false;
                keys.insert(key);
                if (orderedKeys)
                    orderedKeys->append(key);
                if (key == "configuration_generation")
                {
                    auto end = next + 1;
                    while (end < json.size() && json[end] != ',' && json[end] != '}')
                        ++end;
                    const auto text = json.mid(next + 1, end - next - 1).trimmed();
                    bool ok = false;
                    const auto value = text.toULongLong(&ok);
                    if (!ok || text.isEmpty() || text[0] < '1' || text[0] > '9' || value > quint64(INT64_MAX))
                        return false;
                    for (char c : text)
                        if (c < '0' || c > '9')
                            return false;
                }
            }
        }
    }
    *out = doc.object();
    return keys.size() == out->size();
}
// Canonical binary identity is deliberately independent of JSON key ordering.
bool canonicalIdentity(const QJsonObject &j, Domain d, const Document &doc, QByteArray *out)
{
    const auto generation = j["configuration_generation"].toInteger(-1);
    const int schemaVersion = j["schema_version"].toInt();
    if (!exactKeys(j, d) || generation <= 0 || quint64(generation) != doc.generation ||
        (d == Domain::DeviceModel ? (schemaVersion != 2 && schemaVersion != 3)
                                  : schemaVersion != 1) || doc.systemSha256.size() != 32 ||
        zero(doc.systemSha256, 0, 32) ||
        j["system_package_id_sha256"].toString().toLatin1() != doc.systemSha256.toHex())
        return false;
    const auto declaredId =
        j[d == Domain::DeviceModel ? "device_model_config_id_sha256" : "system_input_policy_id_sha256"]
            .toString()
            .toLatin1();
    if (declaredId.size() != 64)
        return false;
    for (char c : declaredId)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    QByteArray b(d == Domain::DeviceModel ? (schemaVersion == 3 ? 648 : 512) : 84, 0);
    if (d == Domain::InputPolicy)
    {
        const int interlock =
            enumText(j, "plc_interlock_policy", {"ARM_ONLY", "PLC_OPTIONAL_INTERLOCK", "PLC_REQUIRED_INTERLOCK"});
        const int encoding = enumText(j, "plc_state_encoding", {"PROTOCOL_A_3_STATE", "OPEN_PERMIT_BOOLEAN"});
        if (!interlock || !encoding || j.size() != 6)
            return false;
        b.replace(0, 23, "UCM:SystemInputPolicyV1");
        p32(b, 32, 1);
        p64(b, 36, quint64(generation));
        b.replace(44, 32, doc.systemSha256);
        p32(b, 76, interlock);
        p32(b, 80, encoding);
    }
    else
    {
        const auto model = j["device_model_id"].toInteger();
        const auto applicable = j["applicability_id"].toInteger();
        const auto frames = j["open_stable_confirm_frames"].toInteger();
        if (model <= 0 || model > UINT32_MAX || applicable <= 0 || applicable > UINT32_MAX || frames <= 0 ||
            frames > UINT32_MAX || !validateDeviceModelGeometry(j))
            return false;
        p32(b, 0, 0x32494344U);
        p32(b, 4, quint32(schemaVersion));
        p32(b, 8, quint32(schemaVersion));
        p64(b, 16, quint64(generation));
        b.replace(24, 32, doc.systemSha256);
        p32(b, 56, quint32(model));
        p32(b, 60, quint32(applicable));
        int o = 64;
        for (const char *key : {"pack_id", "model_name", "applicability_name", "geometry_model"})
        {
            const auto s = j[QString::fromLatin1(key)].toString().toUtf8();
            if (s.isEmpty() || s.size() >= 64 || s.contains('\0'))
                return false;
            for (unsigned char c : s)
                if (c < 0x20 || c == '\\' || c == '"')
                    return false;
            b.replace(o, s.size(), s);
            o += 64;
        }
        auto mode = j["mold_source_mode"].toString();
        if (mode == "AUTO")
            mode = "AUTO_RS485_FIRST";
        const int source =
            QStringList{"AUTO_RS485_FIRST", "RS485_ONLY", "ETHERCAT_ONLY", "MODEL_DEFAULT_ONLY", "AUTO_ETHERCAT_FIRST"}
                .indexOf(mode) +
            1;
        if (!source)
            return false;
        p32(b, o, source);
        o += 4;
        p32(b, o, quint32(frames));
        o += 4;
        auto number = [&](const QJsonValue &v) {
            if (!v.isDouble() || !std::isfinite(v.toDouble()))
                return false;
            double x = v.toDouble();
            quint64 bits;
            std::memcpy(&bits, &x, 8);
            p64(b, o, bits);
            o += 8;
            return true;
        };
        auto array = [&](const char *key) {
            auto a = j[QString::fromLatin1(key)].toArray();
            if (a.size() != 4)
                return false;
            for (const auto v : a)
                if (!number(v))
                    return false;
            return true;
        };
        if (!number(j["kmat_unified"]) || !array("coupling_bias_ns"))
            return false;
        for (const char *key : {"l_total_mm", "l_b_mm", "l_d_mm", "l_e_mm", "abeq_mm2", "ac_mm2", "adeq_mm2", "phi_b",
                                "phi_d", "fixed_mold_thickness_mm"})
            if (!number(j[QString::fromLatin1(key)]))
                return false;
        if (!array("rod_diameter_mm"))
            return false;
        for (const char *key : {"thread_root_diameter_mm", "mold_reference_mm", "body_reference_mm", "min_ncc_peak"})
            if (!number(j[QString::fromLatin1(key)]))
                return false;
        if (o != 512)
            return false;
        if (schemaVersion == 3)
        {
            const auto knotCount = j["force_correction_knot_count"].toInteger(-1);
            const auto inputs = j["force_correction_input_n"].toArray();
            const auto outputs = j["force_correction_output_n"].toArray();
            if (knotCount < 0 || knotCount > 8 || knotCount == 1 ||
                inputs.size() != 8 || outputs.size() != 8)
                return false;
            p32(b, o, quint32(knotCount));
            o += 4;
            p32(b, o, 0);
            o += 4;
            double previousInput = 0.0, previousOutput = 0.0;
            for (int index = 0; index < 8; ++index)
            {
                if (!number(inputs[index]))
                    return false;
                const double value = inputs[index].toDouble();
                if (std::abs(value) > 1e9 ||
                    (index < knotCount && ((index == 0 && value != 0.0) ||
                     (index > 0 && value <= previousInput))) ||
                    (index >= knotCount && value != 0.0))
                    return false;
                previousInput = value;
            }
            for (int index = 0; index < 8; ++index)
            {
                if (!number(outputs[index]))
                    return false;
                const double value = outputs[index].toDouble();
                if (std::abs(value) > 1e9 ||
                    (index < knotCount && ((index == 0 && value != 0.0) ||
                     (index > 0 && value <= previousOutput))) ||
                    (index >= knotCount && value != 0.0))
                    return false;
                previousOutput = value;
            }
            if (o != 648)
                return false;
        }
    }
    *out = digest(b);
    return true;
}
bool logName(const QString &name, quint32 kind)
{
    static const QRegularExpression legacy("^result-[0-9]{8}-[0-9]{3}\\.urs[12]$");
    static const QRegularExpression native("^result-[0-9]{8}-[0-9]{4}\\.url3$");
    return (kind == 1 && legacy.match(name).hasMatch())
        || (kind == 2 && native.match(name).hasMatch());
}
bool logEntryValid(const LogEntry &e)
{
    const auto mut = e.flags & 6U;
    const quint64 record = e.kind == 2 ? 256 : (e.name.endsWith(".urs1") ? 320 : 680);
    return logName(e.name, e.kind) && !(e.flags & ~7U) && (e.flags & 1U) && (mut == 2 || mut == 4) && e.totalBytes &&
           e.totalBytes <= 16777216 && e.totalBytes % record == 0 && e.modifiedTimeNs && e.snapshotId &&
           e.fileIdentityCrc32;
}
} // namespace
quint32 crc32(const QByteArray &b)
{
    quint32 c = 0xffffffffU;
    for (unsigned char x : b)
    {
        c ^= x;
        for (int k = 0; k < 8; ++k)
            c = (c >> 1) ^ ((c & 1) ? 0xedb88320U : 0);
    }
    return ~c;
}
bool computeDocumentIdentity(Domain d, const QByteArray &json, QByteArray *out, QString *e)
{
    if (e)
        e->clear();
    if (!out)
        return fail(e, "Null identity output");
    out->clear();
    QJsonObject j;
    if (json.size() > (d == Domain::DeviceModel ? 16384 : 4096) || !strictJson(json, &j))
        return fail(e, "Invalid UTF-8, duplicate key, or unsupported generation in JSON");
    const auto package = j["system_package_id_sha256"].toString().toLatin1();
    if (package.size() != 64)
        return fail(e, "Invalid package identity");
    for (char c : package)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return fail(e, "Invalid package identity hex");
    Document doc;
    doc.generation = quint64(j["configuration_generation"].toInteger());
    doc.systemSha256 = QByteArray::fromHex(package);
    if (!canonicalIdentity(j, d, doc, out))
        return fail(e, "Invalid canonical document fields");
    return true;
}
bool validateDeviceModelGeometry(const QJsonObject &object, QString *error)
{
    if (error)
        error->clear();
    const int schemaVersion = object["schema_version"].toInt();
    if (schemaVersion != 2 && schemaVersion != 3)
        return fail(error, "DeviceModel requires schema 2 or 3");
    const auto finite = [&](const char *key) {
        const auto value = object[QString::fromLatin1(key)];
        return value.isDouble() && std::isfinite(value.toDouble());
    };
    if (!finite("kmat_unified") || object["kmat_unified"].toDouble() < 1e-18 ||
        object["kmat_unified"].toDouble() > 1e-10)
        return fail(error, "Kmat must be finite, positive, and within the ARM range");
    const auto biases = object["coupling_bias_ns"].toArray();
    if (biases.size() != 4)
        return fail(error, "Four channel coupling residuals are required");
    for (const auto &value : biases)
        if (!value.isDouble() || !std::isfinite(value.toDouble()) ||
            std::abs(value.toDouble()) > 1e6)
            return fail(error, "Channel coupling residual must be finite and bounded");
    const bool gw = object["device_model_id"].toInteger() == 37;
    if (gw)
    {
        const auto exact = [&](const char *key, double expected, double tolerance = 1e-6) {
            return finite(key) && std::abs(object[QString::fromLatin1(key)].toDouble() - expected) <= tolerance;
        };
        if (schemaVersion != 3 || object["model_name"].toString() != "GW1850R" ||
            object["pack_id"].toString() != "GW1850R-R2S" ||
            object["geometry_model"].toString() != "GW_DRAWING_FE_ENGINEERING_V1" ||
            !exact("l_total_mm", 5230.0) || !exact("l_b_mm", 0.0) ||
            !exact("l_d_mm", 0.0) || !exact("l_e_mm", 0.0) ||
            !exact("abeq_mm2", 53816.963, 1e-3) ||
            !exact("ac_mm2", 61575.216, 1e-3) ||
            !exact("adeq_mm2", 53816.963, 1e-3) ||
            !exact("phi_b", 0.0) || !exact("phi_d", 0.0) ||
            !exact("thread_root_diameter_mm", 250.0) ||
            !exact("mold_reference_mm", 662.0) ||
            !exact("fixed_mold_thickness_mm", 662.0) ||
            !exact("body_reference_mm", 0.0))
            return fail(error, "GW1850R structure must match the frozen drawing/FE engineering profile");
        const auto rods = object["rod_diameter_mm"].toArray();
        if (rods.size() != 4)
            return fail(error, "GW1850R requires four rod diameters");
        for (const auto &value : rods)
            if (!value.isDouble() || std::abs(value.toDouble() - 280.0) > 1e-6)
                return fail(error, "GW1850R rod diameter is frozen at 280 mm");
    }
    else if (object["geometry_model"].toString() != "BODY_REFERENCE_V1")
        return fail(error, "A001/DE168 require BODY_REFERENCE_V1 geometry");
    if (!gw)
    {
    for (const char *key :
         {"l_total_mm", "l_b_mm", "l_d_mm", "l_e_mm", "abeq_mm2", "ac_mm2", "adeq_mm2", "thread_root_diameter_mm",
          "mold_reference_mm", "body_reference_mm", "fixed_mold_thickness_mm"})
    {
        const auto value = object[QString::fromLatin1(key)];
        if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() <= 0)
            return fail(error, "Geometry dimensions and areas must be finite and positive");
    }
    const auto rods = object["rod_diameter_mm"].toArray();
    if (rods.size() != 4)
        return fail(error, "Four positive rod diameters are required");
    for (const auto &value : rods)
        if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() <= 0)
            return fail(error, "Rod diameter must be finite and positive");
    for (const char *key : {"phi_b", "phi_d"})
    {
        const auto value = object[QString::fromLatin1(key)];
        if (!value.isDouble() || !std::isfinite(value.toDouble()))
            return fail(error, "Force parameter must be finite");
    }
    const double remainder = object["l_total_mm"].toDouble() - object["l_b_mm"].toDouble() -
                             object["l_d_mm"].toDouble() - object["l_e_mm"].toDouble();
    const double referenceC = object["body_reference_mm"].toDouble();
    const double fixedC =
        referenceC + (object["fixed_mold_thickness_mm"].toDouble() - object["mold_reference_mm"].toDouble());
    if (!std::isfinite(remainder) || !std::isfinite(fixedC) || fixedC <= 0 || !std::isfinite(remainder - referenceC) ||
        remainder - referenceC <= 0 || !std::isfinite(remainder - fixedC) || remainder - fixedC <= 0)
        return fail(error, "Reference and fixed-mold geometry require positive resolved A and C segments");
    }
    if (schemaVersion == 3)
    {
        const auto knotCount = object["force_correction_knot_count"].toInteger(-1);
        const auto inputs = object["force_correction_input_n"].toArray();
        const auto outputs = object["force_correction_output_n"].toArray();
        if (knotCount < 0 || knotCount > 8 || knotCount == 1 ||
            inputs.size() != 8 || outputs.size() != 8)
            return fail(error, "Force correction requires zero or 2-8 fixed-array knots");
        for (int index = 0; index < 8; ++index)
        {
            if (!inputs[index].isDouble() || !outputs[index].isDouble() ||
                !std::isfinite(inputs[index].toDouble()) ||
                !std::isfinite(outputs[index].toDouble()) ||
                std::abs(inputs[index].toDouble()) > 1e9 ||
                std::abs(outputs[index].toDouble()) > 1e9 ||
                (index < knotCount && ((index == 0 &&
                  (inputs[index].toDouble() != 0.0 || outputs[index].toDouble() != 0.0)) ||
                 (index > 0 && (inputs[index].toDouble() <= inputs[index - 1].toDouble() ||
                                outputs[index].toDouble() <= outputs[index - 1].toDouble())))) ||
                (index >= knotCount &&
                 (inputs[index].toDouble() != 0.0 || outputs[index].toDouble() != 0.0)))
                return fail(error, "Force correction must be finite, zero-anchored, monotone, and zero-padded");
        }
    }
    return true;
}
QByteArray serializeDocument(Domain domain, const QJsonObject &object, QString *error)
{
    if (error)
        error->clear();
    if (!exactKeys(object, domain))
    {
        fail(error, "Document has missing, legacy, or unknown fields");
        return {};
    }
    if (domain == Domain::DeviceModel && !validateDeviceModelGeometry(object, error))
        return {};
    QByteArray bytes("{");
    for (const auto &key : documentKeys(domain, object["schema_version"].toInt()))
    {
        if (bytes.size() > 1)
            bytes += ',';
        const auto encoded = QJsonDocument(QJsonArray{object.value(key)}).toJson(QJsonDocument::Compact);
        bytes += '"' + key.toUtf8() + "\":" + encoded.mid(1, encoded.size() - 2);
    }
    bytes += '}';
    QByteArray identity;
    if (!computeDocumentIdentity(domain, bytes, &identity, error))
        return {};
    return bytes;
}
QByteArray encodeQuery(Domain d, quint32 operation, quint64 requestId, QString *e)
{
    if (e)
        e->clear();
    if (operation < 4 || operation > 6 || !requestId)
    {
        fail(e, "Invalid read-only configuration query");
        return {};
    }
    QByteArray b(256, 0);
    p32(b, 0, token(d));
    p32(b, 4, abi(d));
    p32(b, 8, 256);
    p32(b, 12, 256);
    p32(b, 16, operation);
    p64(b, 40, requestId);
    seal(b, 212);
    return b;
}
bool decodeState(const QByteArray &b, Domain d, State *out, QString *e)
{
    if (e)
        e->clear();
    if (!out)
        return fail(e, "Null state output");
    *out = {};
    if (b.size() != 352 || u32(b, 0) != token(d) || u32(b, 4) != abi(d) || u32(b, 8) != 352 || !zero(b, 348, 4) ||
        !checksum(b, 344) || (u32(b, 20) & ~15U))
        return fail(e, "Invalid configuration state header/CRC/reserved");
    State s;
    s.bootId = u64(b, 32);
    s.authorityGeneration = u64(b, 40);
    s.sessionId = u64(b, 48);
    s.responseRequestId = u64(b, 56);
    s.systemSha256 = b.mid(120, 32);
    s.activeGeneration = u64(b, 80);
    s.startupGeneration = u64(b, 88);
    s.startupCommitSequence = u64(b, 96);
    s.activeValid = u32(b, 20) & 2;
    s.startupValid = u32(b, 20) & 4;
    s.maintenanceWait = u32(b, 20) & 8;
    s.activeId = b.mid(216, 32);
    s.activeDigest = b.mid(248, 32);
    s.startupId = b.mid(280, 32);
    s.startupDigest = b.mid(312, 32);
    const quint32 kind = u32(b, 12);
    const bool candidate = u32(b, 20) & 1;
    if (kind < 1 || kind > 7 || u32(b, 24) > 8 || u32(b, 28) > 7 || s.maintenanceWait == s.activeValid ||
        candidate == zero(b, 152, 32) || candidate == zero(b, 184, 32) || s.activeValid != (s.activeGeneration != 0) ||
        s.activeValid == zero(b, 216, 32) || s.activeValid == zero(b, 248, 32) ||
        s.startupValid != (s.startupGeneration != 0) || s.startupValid != (s.startupCommitSequence != 0) ||
        s.startupValid == zero(b, 280, 32) || s.startupValid == zero(b, 312, 32) ||
        ((kind == 2 || kind == 4 || kind == 5) && !s.activeValid) || (kind == 3 && !candidate))
        return fail(e, "Inconsistent configuration state kind/identity/validity");
    if (!s.bootId || !s.authorityGeneration || !s.sessionId || !s.responseRequestId || zero(b, 120, 32) ||
        (d == Domain::DeviceModel && u32(b, 104) != 2) ||
        (d == Domain::InputPolicy && (u32(b, 104) != 1 || !u32(b, 108) || (u32(b, 108) & ~7U) || !u32(b, 112) ||
                                      (u32(b, 112) & ~3U) || u32(b, 116))))
        return fail(e, "Invalid configuration state identity/policy");
    idField(s.fields, "bootId", s.bootId);
    idField(s.fields, "authorityGeneration", s.authorityGeneration);
    idField(s.fields, "sessionId", s.sessionId);
    idField(s.fields, "responseRequestId", s.responseRequestId);
    idField(s.fields, "activeGeneration", s.activeGeneration);
    idField(s.fields, "startupGeneration", s.startupGeneration);
    idField(s.fields, "startupCommitSequence", s.startupCommitSequence);
    s.fields["activeValid"] = s.activeValid;
    s.fields["startupValid"] = s.startupValid;
    s.fields["maintenanceWait"] = s.maintenanceWait;
    s.fields["stateKind"] = qint64(u32(b, 12));
    s.fields["lastResult"] = qint32(u32(b, 16));
    s.fields["transactionState"] = qint64(u32(b, 24));
    s.fields["transactionReason"] = qint64(u32(b, 28));
    s.fields["activeId"] = QString::fromLatin1(s.activeId.toHex());
    s.fields["startupId"] = QString::fromLatin1(s.startupId.toHex());
    s.fields["systemSha256"] = QString::fromLatin1(s.systemSha256.toHex());
    s.fields["activePayloadDigest"] = QString::fromLatin1(s.activeDigest.toHex());
    s.fields["startupPayloadDigest"] = QString::fromLatin1(s.startupDigest.toHex());
    if (d == Domain::DeviceModel)
    {
        s.fields["deviceModelId"] = qint64(u32(b, 108));
        s.fields["applicabilityId"] = qint64(u32(b, 112));
        s.fields["allowedMoldSources"] = qint64(u32(b, 116));
    }
    else
    {
        s.fields["allowedPlcInterlockMask"] = qint64(u32(b, 108));
        s.fields["allowedPlcStateEncodingMask"] = qint64(u32(b, 112));
    }
    *out = s;
    return true;
}
bool decodeDocument(const QByteArray &b, Domain d, Document *out, QString *e)
{
    if (e)
        e->clear();
    if (!out)
        return fail(e, "Null document output");
    *out = {};
    const int max = d == Domain::DeviceModel ? 16384 : 4096;
    if (b.size() < 256 || b.size() > 256 + max || u32(b, 0) != token(d) || u32(b, 4) != abi(d) || u32(b, 8) != 256 ||
        u32(b, 12) != quint32(b.size()) || u32(b, 184) != quint32(b.size() - 256) || !zero(b, 192, 64) ||
        !checksum(b, 188) || u32(b, 16) > 1)
        return fail(e, "Invalid document header/CRC/reserved");
    Document s;
    s.present = u32(b, 16);
    s.kind = u32(b, 20);
    s.bootId = u64(b, 24);
    s.authorityGeneration = u64(b, 32);
    s.sessionId = u64(b, 40);
    s.responseRequestId = u64(b, 48);
    s.generation = u64(b, 56);
    s.commitSequence = u64(b, 64);
    s.systemSha256 = b.mid(88, 32);
    s.identitySha256 = b.mid(120, 32);
    s.payloadDigest = b.mid(152, 32);
    s.json = b.mid(256);
    if ((s.kind != 1 && s.kind != 2) || !s.bootId || !s.authorityGeneration || !s.sessionId || !s.responseRequestId ||
        zero(b, 88, 32) || !u32(b, 72) || !u32(b, 76) || !u32(b, 80) ||
        (d == Domain::DeviceModel ? (u32(b, 72) != 2 || !u32(b, 84))
                                  : (u32(b, 72) != 1 || (u32(b, 76) & ~7U) || (u32(b, 80) & ~3U) || u32(b, 84))))
        return fail(e, "Invalid document identity/policy");
    if (!s.present)
    {
        if (s.generation || s.commitSequence || !s.json.isEmpty() || !zero(b, 120, 64))
            return fail(e, "Absent document contains stale configuration");
    }
    else
    {
        if (!s.generation || s.json.isEmpty() || zero(b, 120, 64) ||
            (s.kind == 1 ? s.commitSequence != 0 : s.commitSequence == 0) || digest(s.json) != s.payloadDigest)
            return fail(e, "Document generation/payload digest mismatch");
        QStringList keys;
        if (!strictJson(s.json, &s.configuration, &keys) ||
            (d == Domain::DeviceModel &&
             (keys != documentKeys(d, s.configuration["schema_version"].toInt()) ||
              s.json.contains('\\'))))
            return fail(e, "Invalid UTF-8, duplicate key, or unsupported generation in document JSON");
        QByteArray canonical;
        const auto key = d == Domain::DeviceModel ? "device_model_config_id_sha256" : "system_input_policy_id_sha256";
        if (!canonicalIdentity(s.configuration, d, s, &canonical) || canonical != s.identitySha256 ||
            s.configuration[key].toString().toLatin1() != canonical.toHex())
            return fail(e, "Canonical document identity mismatch");
    }
    idField(s.fields, "bootId", s.bootId);
    idField(s.fields, "authorityGeneration", s.authorityGeneration);
    idField(s.fields, "sessionId", s.sessionId);
    idField(s.fields, "responseRequestId", s.responseRequestId);
    idField(s.fields, "generation", s.generation);
    idField(s.fields, "commitSequence", s.commitSequence);
    s.fields["present"] = s.present;
    s.fields["kind"] = int(s.kind);
    s.fields["identitySha256"] = QString::fromLatin1(s.identitySha256.toHex());
    s.fields["payloadDigest"] = QString::fromLatin1(s.payloadDigest.toHex());
    s.fields["configuration"] = s.configuration;
    *out = s;
    return true;
}
bool documentMatchesState(const Document &d, const State &s)
{
    if (d.bootId != s.bootId || d.authorityGeneration != s.authorityGeneration || d.sessionId != s.sessionId ||
        d.systemSha256 != s.systemSha256)
        return false;
    if (d.kind == 1)
        return d.present == s.activeValid && d.generation == s.activeGeneration && d.identitySha256 == s.activeId &&
               d.payloadDigest == s.activeDigest;
    return d.kind == 2 && d.present == s.startupValid && d.generation == s.startupGeneration &&
           d.commitSequence == s.startupCommitSequence && d.identitySha256 == s.startupId &&
           d.payloadDigest == s.startupDigest;
}
bool samePublication(const PublicationIdentity &a, const PublicationIdentity &b)
{
    return identityValid(a) && identityValid(b) && a.publisherGeneration == b.publisherGeneration &&
           a.publishedMonotonicNs == b.publishedMonotonicNs && a.sessionId == b.sessionId && a.sequence == b.sequence &&
           a.frameCounter == b.frameCounter && a.captureRequestId == b.captureRequestId;
}
bool decodePublication(const QByteArray &b, Publication *out, QString *e)
{
    if (e)
        e->clear();
    if (!out)
        return fail(e, "Null publication output");
    *out = {};
    if (!objectHeader(b, 256, 0x31524355U, 3))
        return fail(e, "Invalid publication header/CRC");
    Publication s;
    s.identity = identity(b);
    s.flags = u32(b, 12);
    s.reason = u32(b, 76);
    if (!identityValid(s.identity)
        || (s.flags & ~kPublicationAllowedFlagsV3)
        || (u32(b, 68) & ~15U) || (u32(b, 72) & ~15U) || s.reason > 10 ||
        !u64(b, 224))
        return fail(e, "Invalid publication identity/flags");
    s.predictionMask = (s.flags & kPublicationPredictionMaskV3) >> 8;
    s.qualityDegradedMask =
        (s.flags & kPublicationQualityDegradedMaskV3) >> 12;
    s.commonTrendOutlierMask =
        (s.flags & kPublicationCommonTrendOutlierMaskV3) >> 16;
    if (((s.flags & kPublicationPredictionActiveV3) != 0U)
            != (s.predictionMask != 0U))
        return fail(e, "Inconsistent publication prediction flags");
    for (int o = 232; o < 256; o += 4)
        if (!u32(b, o))
            return fail(e, "Missing publication context binding");
    for (int o = 80; o < 224; o += 8)
        if (!std::isfinite(f64(b, o)))
            return fail(e, "Non-finite publication value");
    if (((s.flags & 1) && (u32(b, 68) != 15 || s.reason)) || ((s.flags & 2) && u32(b, 72) != 15))
        return fail(e, "Inconsistent publication validity");
    s.valid = s.flags & 1;
    s.forceAvailable = s.flags & 2;
    s.formalTotalAvailable = s.flags & 4;
    s.fields = identityJson(s.identity);
    s.fields["flags"] = int(s.flags);
    s.fields["reason"] = int(s.reason);
    s.fields["valid"] = s.valid;
    s.fields["forceAvailable"] = s.forceAvailable;
    s.fields["formalTotalAvailable"] = s.formalTotalAvailable;
    s.fields["predictionMask"] = int(s.predictionMask);
    s.fields["qualityDegradedMask"] = int(s.qualityDegradedMask);
    s.fields["commonTrendOutlierMask"] = int(s.commonTrendOutlierMask);
    s.fields["profile"] = qint64(u32(b, 64));
    s.fields["measurementValidMask"] = int(u32(b, 68));
    s.fields["forceRodValidMask"] = int(u32(b, 72));
    const char *keys[] = {"t0Sample", "nccDeltaNs", "snrDb", "forceRodN"};
    for (int g = 0; g < 4; ++g)
    {
        QJsonArray a;
        for (int i = 0; i < 4; ++i)
            a.append(f64(b, 80 + 32 * g + 8 * i));
        s.fields[keys[g]] = a;
    }
    // Numeric values are the ARM projection. Consumers must honor validity flags.
    s.fields["formalTotalN"] = f64(b, 208);
    s.fields["formalImbalance"] = f64(b, 216);
    idField(s.fields, "assetGeneration", u64(b, 224));
    *out = s;
    return true;
}
bool decodeInputStatus(const QByteArray &b, InputStatus *out, QString *e)
{
    if (e)
        e->clear();
    if (!out)
        return fail(e, "Null input status output");
    *out = {};
    if (!objectHeader(b, 360, 0x31524955U, 1, false) || !zero(b, 344, 16) || !zero(b, 260, 4) || !zero(b, 216, 8))
        return fail(e, "Invalid input status header/CRC/reserved");
    InputStatus s;
    s.identity = identity(b);
    s.flags = u32(b, 12);
    auto v = [&](int o) { return u32(b, o); };
    auto w = [&](int o) { return u64(b, o); };
    const bool device = s.flags & 2, mold = s.flags & 4, geometry = s.flags & 8, policy = s.flags & 16,
               plc = s.flags & 32;
    if (!identityValid(s.identity) || !s.identity.frameCounter || !(s.flags & 1) || (s.flags & ~255U) ||
        w(64) != s.identity.sequence || !w(72) || w(80) < w(72) || s.identity.publishedMonotonicNs < w(80) ||
        device != (w(88) != 0) || device != !zero(b, 112, 32) || (device && w(88) == UINT64_MAX) || v(232) < 1 ||
        v(232) > 5 || v(248) > 9 || policy != (v(332) != 0) || policy != (w(96) != 0) || policy != !zero(b, 144, 32))
        return fail(e, "Invalid input identity/binding");
    for (int o : {240, 244, 316, 320, 324, 328, 332, 336, 340})
        if (v(o) > 1)
            return fail(e, "Invalid input boolean");
    // Layout: plc starts at 264 after four-byte ABI alignment padding.
    if (v(328) > v(324) || v(316) > v(328) || v(320) > v(316) || plc != (v(328) != 0) || v(336) > v(316) ||
        bool(s.flags & 64) != (v(336) != 0) || bool(s.flags & 128) != (v(340) != 0) || mold != (v(240) != 0))
        return fail(e, "Inconsistent input PLC/mold flags");
    if (mold)
    {
        if (v(236) < 1 || v(236) > 3 || !w(176) || w(176) == UINT64_MAX || w(192) > w(80) || qint64(w(200)) <= 0 ||
            qint64(w(208)) <= 0 || !v(252))
            return fail(e, "Invalid mold source identity");
        if (v(236) == 3 ? (w(184) != 0 || w(192) != 0) : (!w(184) || w(184) == UINT64_MAX || !w(192)))
            return fail(e, "Invalid mold source time");
    }
    else if (v(236) || w(176) || w(184) || w(192) || v(252))
        return fail(e, "Unbound mold contains stale identity");
    if (geometry ? (!mold || !v(256) || qint64(w(224)) <= 0) : v(256) != 0)
        return fail(e, "Invalid geometry binding");
    // The ABI's last ten u32 fields start at 304: interlock, encoding, raw,
    // valid, open, live, available, policy-valid, external-from, permitted.
    if (policy)
    {
        if (w(96) == UINT64_MAX || !w(104) || w(104) == UINT64_MAX || v(304) < 1 || v(304) > 3 || v(308) < 1 ||
            v(308) > 2 || w(296) < w(72))
            return fail(e, "Invalid policy binding");
        if (v(324))
        {
            if (!w(264) || w(264) == UINT64_MAX)
                return fail(e, "Invalid PLC source");
            if (!plc)
            {
                if (w(272) || w(280) || w(288) || v(312) || v(316) || v(320))
                    return fail(e, "Unavailable PLC retains record");
            }
            else
            {
                const bool valid = v(312) <= (v(308) == 1 ? 2U : 1U);
                const bool open = valid && v(312) == (v(308) == 1 ? 0U : 1U);
                if (!w(272) || w(272) == UINT64_MAX || !w(280) || !w(288) || w(280) > w(288) || w(288) > w(296) ||
                    v(312) > 255 || v(316) != valid || v(320) != open)
                    return fail(e, "Invalid PLC raw state/timestamps");
            }
        }
        else if (w(264) || w(272) || w(280) || w(288) || v(312) || v(316) || v(320) || v(328))
            return fail(e, "Disconnected PLC retains record");
        const bool from = v(304) != 1 && v(316);
        const bool permitted = v(304) == 1 || (v(316) ? v(320) != 0 : v(304) == 2);
        if (v(336) != from || v(340) != permitted)
            return fail(e, "PLC interlock policy projection mismatch");
    }
    else if (w(104) || !zero(b, 264, 84))
        return fail(e, "Unbound policy contains stale PLC fields");
    s.fields = identityJson(s.identity);
    s.fields["flags"] = int(s.flags);
    const char *u64keys[] = {"inputCaptureSequence",        "inputCaptureMonotonicNs",
                             "inputBoundMonotonicNs",       "deviceModelConfigurationGeneration",
                             "systemInputPolicyGeneration", "systemInputPolicyInstance"};
    for (int i = 0; i < 6; ++i)
        idField(s.fields, u64keys[i], w(64 + 8 * i));
    s.fields["deviceModelConfigurationSha256"] = QString::fromLatin1(b.mid(112, 32).toHex());
    s.fields["systemInputPolicySha256"] = QString::fromLatin1(b.mid(144, 32).toHex());
    idField(s.fields, "moldSourceInstance", w(176));
    idField(s.fields, "moldSourceUpdateSequence", w(184));
    idField(s.fields, "moldSourceReceivedMonotonicNs", w(192));
    const char *signedKeys[] = {"rawThicknessUm", "effectiveThicknessUm", "tiebarAEndDistanceUm", "derivedLAUm"};
    for (int i = 0; i < 4; ++i)
        s.fields[signedKeys[i]] = QString::number(qint64(w(200 + i * 8)));
    const char *moldKeys[] = {"configuredMoldMode", "actualMoldSource",       "moldValid",       "moldDegraded",
                              "moldReason",         "moldSourceBindingCrc32", "moldMappingCrc32"};
    for (int i = 0; i < 7; ++i)
        s.fields[moldKeys[i]] = qint64(v(232 + 4 * i));
    const char *plc64[] = {"plcSourceInstance", "plcUpdateSequence", "plcSourceMonotonicNs", "plcReceivedMonotonicNs",
                           "plcBoundMonotonicNs"};
    for (int i = 0; i < 5; ++i)
        idField(s.fields, plc64[i], w(264 + 8 * i));
    const char *plc32[] = {
        "plcInterlockPolicy",       "plcStateEncoding",          "plcRawStateCode",    "plcValid",
        "plcOpenPermitted",         "plcConnectionLive",         "plcRecordAvailable", "systemPolicyValid",
        "externalInterlockFromPlc", "externalInterlockPermitted"};
    for (int i = 0; i < 10; ++i)
        s.fields[plc32[i]] = qint64(v(304 + 4 * i));
    *out = s;
    return true;
}
bool decodeResultDiagnostics(const QByteArray &b, ResultDiagnostics *out,
                             QString *e)
{
    if (e) e->clear();
    if (!out) return fail(e, "Null result diagnostics output");
    *out = {};
    if (!objectHeader(b, kResultDiagnosticsBytesV1,
                      kResultDiagnosticsTokenV1, 1)
        || u32(b, 156) != 0U) {
        return fail(e, "Invalid result diagnostics header/CRC/reserved");
    }
    ResultDiagnostics d;
    d.flags = u32(b, 12);
    d.predictionMask = (d.flags & kResultDiagnosticsPredictionMaskV1) >> 8;
    d.qualityDegradedMask =
        (d.flags & kResultDiagnosticsQualityDegradedMaskV1) >> 12;
    d.commonTrendOutlierMask =
        (d.flags & kResultDiagnosticsCommonTrendOutlierMaskV1) >> 16;
    d.publisherGeneration = u64(b, 16);
    d.publishedMonotonicNs = u64(b, 24);
    d.sessionId = u64(b, 32);
    d.sequence = u64(b, 40);
    d.frameCounter = u64(b, 48);
    d.rodValidMask = u32(b, 56);
    d.biasValidMinTotalForceN = u32(b, 60);
    for (int rod = 0; rod < 4; ++rod) {
        d.strainMicrostrain[rod] = f64(b, 64 + rod * 8);
        d.invalidReason[rod] = u32(b, 96 + rod * 4);
        d.actualLnaDb[rod] = u32(b, 128 + rod * 4);
    }
    d.plcState = u32(b, 112);
    d.agcState = u32(b, 116);
    d.agcReason = u32(b, 120);
    d.actualPgaDb = u32(b, 124);
    d.actualVcntlDac = u32(b, 144);
    d.actualHvVolts = u32(b, 148);
    d.actualBurstCycles = u32(b, 152);
    if ((d.flags & ~kResultDiagnosticsAllowedFlagsV1) != 0U
        || (d.flags & kResultDiagnosticsValidV1) == 0U
        || (((d.flags & kResultDiagnosticsPredictionActiveV1) != 0U)
            != (d.predictionMask != 0U))
        || d.publisherGeneration == 0U || d.publishedMonotonicNs == 0U
        || d.sessionId == 0U || d.sequence == 0U
        || (d.rodValidMask & ~0x0FU) != 0U
        || d.biasValidMinTotalForceN == 0U
        || d.agcState > 12U || d.agcReason > 11U
        || (d.actualPgaDb != 24U && d.actualPgaDb != 30U)
        || d.actualVcntlDac > 65535U
        || d.actualHvVolts < 50U || d.actualHvVolts > 250U
        || (d.actualHvVolts % 5U) != 0U
        || d.actualBurstCycles < 1U || d.actualBurstCycles > 8U) {
        return fail(e, "Invalid result diagnostics identity/range");
    }
    for (int rod = 0; rod < 4; ++rod) {
        if (!std::isfinite(d.strainMicrostrain[rod])
            || d.invalidReason[rod] > 4U
            || (((d.rodValidMask & (1U << rod)) != 0U)
                != (d.invalidReason[rod] == 0U))
            || (d.actualLnaDb[rod] != 12U && d.actualLnaDb[rod] != 18U
                && d.actualLnaDb[rod] != 24U)) {
            return fail(e, "Invalid result diagnostics rod value");
        }
    }
    if ((d.flags & kResultDiagnosticsPlcStaleV1) != 0U
        && d.plcState != 0U) {
        return fail(e, "Stale PLC diagnostics must clear plc_state");
    }
    *out = d;
    return true;
}
bool resultDiagnosticsMatchesPublication(const ResultDiagnostics &d,
                                         const PublicationIdentity &p)
{
    return d.publisherGeneration == p.publisherGeneration
        && d.sessionId == p.sessionId && d.sequence == p.sequence
        && d.frameCounter == p.frameCounter;
}
QByteArray encodeLogCatalogRequest(quint32 maximum, QString *e)
{
    if (e)
        e->clear();
    if (!maximum || maximum > 32)
    {
        fail(e, "Invalid catalog maximum");
        return {};
    }
    QByteArray b(32, 0);
    p32(b, 0, 0x32434c55U);
    p16(b, 4, 2);
    p16(b, 6, 32);
    p32(b, 16, maximum);
    seal(b, 8);
    return b;
}
bool decodeLogCatalog(const QByteArray &b, LogCatalog *out, QString *e)
{
    if (e)
        e->clear();
    if (!out)
        return fail(e, "Null catalog output");
    *out = {};
    if (!objectHeader(b, 3136, 0x32434c55U, 2, false) || u32(b, 12) || !u64(b, 16) || u32(b, 24) > 32 ||
        u32(b, 28) < u32(b, 24) || !zero(b, 32, 32))
        return fail(e, "Invalid log catalog header/CRC/reserved");
    LogCatalog c;
    c.snapshotId = u64(b, 16);
    c.totalEntries = u32(b, 28);
    for (quint32 i = 0; i < u32(b, 24); ++i)
    {
        const int o = 64 + int(i) * 96;
        const auto name = b.mid(o + 40, 32);
        const auto nul = name.indexOf('\0');
        const auto kind = u32(b, o);
        if ((kind != 1 && kind != 2) || !zero(b, o + 36, 4) || !zero(b, o + 72, 24) || nul < 0)
            return fail(e, "Invalid log entry reserved/name");
        LogEntry entry;
        entry.name = QString::fromLatin1(name.left(nul));
        entry.flags = u32(b, o + 4);
        entry.totalBytes = u64(b, o + 8);
        entry.modifiedTimeNs = u64(b, o + 16);
        entry.snapshotId = u64(b, o + 24);
        entry.fileIdentityCrc32 = u32(b, o + 32);
        entry.kind = kind;
        if (!logEntryValid(entry))
            return fail(e, "Invalid log entry identity/size");
        for (const auto &previous : c.entries)
            if (previous.name == entry.name)
                return fail(e, "Duplicate log catalog entry");
        c.entries.append(entry);
    }
    if (!zero(b, 64 + int(u32(b, 24)) * 96, (32 - int(u32(b, 24))) * 96))
        return fail(e, "Unused log entries are nonzero");
    *out = c;
    return true;
}
QByteArray encodeLogReadRequest(const LogEntry &entry, quint64 offset, quint32 maximum, QString *e)
{
    if (e)
        e->clear();
    if (!logEntryValid(entry) || offset > entry.totalBytes || !maximum || maximum > 65536)
    {
        fail(e, "Invalid log read identity/range");
        return {};
    }
    QByteArray b(96, 0);
    p32(b, 0, 0x32524c55U);
    p16(b, 4, 2);
    p16(b, 6, 96);
    p64(b, 16, entry.snapshotId);
    p64(b, 24, offset);
    p32(b, 32, maximum);
    p32(b, 36, entry.fileIdentityCrc32);
    auto name = entry.name.toLatin1();
    b.replace(40, name.size(), name);
    seal(b, 8);
    return b;
}
bool decodeLogChunk(const QByteArray &b, const LogEntry &entry, quint64 expectedOffset, quint32 maximum, LogChunk *out,
                    QString *e)
{
    if (e)
        e->clear();
    if (!out)
        return fail(e, "Null chunk output");
    *out = {};
    if (b.size() < 64 || b.size() > 65600 || u32(b, 0) != 0x32524c55U || u16(b, 4) != 2 || u16(b, 6) != 64 ||
        !checksum(b, 8, 64, false) || (u32(b, 12) & ~1U) || !zero(b, 48, 16))
        return fail(e, "Invalid log chunk header/CRC/reserved");
    const auto bytes = u32(b, 40);
    const auto offset = u64(b, 24);
    const auto total = u64(b, 32);
    if (!logEntryValid(entry) || !maximum || maximum > 65536 || u64(b, 16) != entry.snapshotId ||
        u32(b, 44) != entry.fileIdentityCrc32 || total != entry.totalBytes || offset != expectedOffset ||
        offset > total || bytes > total - offset || bytes > maximum || b.size() != 64 + qint64(bytes) ||
        bool(u32(b, 12) & 1) != (offset + bytes < total) || (!bytes && offset < total))
        return fail(e, "Log chunk identity/range mismatch");
    out->offset = offset;
    out->totalBytes = total;
    out->more = u32(b, 12) & 1;
    out->data = b.mid(64);
    return true;
}
} // namespace ucm::productv9

