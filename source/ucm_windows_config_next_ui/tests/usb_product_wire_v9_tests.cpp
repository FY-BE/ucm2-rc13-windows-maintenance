#include "usb_product_wire_v9.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <QtEndian>
#include <cstring>
#include <limits>
using namespace ucm::productv9;
namespace
{
int failures = 0, checks = 0;
void expect(bool ok, const QString &name)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        QTextStream(stderr) << "FAIL " << name << '\n';
    }
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
void pf64(QByteArray &b, int o, double v)
{
    quint64 bits = 0;
    static_assert(sizeof(bits) == sizeof(v));
    std::memcpy(&bits, &v, sizeof(bits));
    p64(b, o, bits);
}
void seal(QByteArray &b, int o, int n = 0)
{
    p32(b, o, 0);
    p32(b, o, crc32(n ? b.left(n) : b));
}
QByteArray publication()
{
    QByteArray b(256, 0);
    p32(b, 0, 0x31524355);
    p16(b, 4, 3);
    p16(b, 6, 256);
    for (int o = 16; o < 64; o += 8)
        p64(b, o, o == 24 ? 1000 : quint64(o));
    p32(b, 76, 9);
    p64(b, 224, 1);
    for (int o = 232; o < 256; o += 4)
        p32(b, o, 1);
    seal(b, 8);
    return b;
}
QByteArray input()
{
    QByteArray b(360, 0);
    p32(b, 0, 0x31524955);
    p16(b, 4, 1);
    p16(b, 6, 360);
    p32(b, 12, 1);
    for (int o = 16; o < 64; o += 8)
        p64(b, o, o == 24 ? 1000 : quint64(o));
    p64(b, 64, 40);
    p64(b, 72, 900);
    p64(b, 80, 950);
    p32(b, 232, 1);
    seal(b, 8);
    return b;
}
QByteArray diagnostics()
{
    const QByteArray result = publication();
    QByteArray b(kResultDiagnosticsBytesV1, 0);
    p32(b, 0, kResultDiagnosticsTokenV1);
    p16(b, 4, 1);
    p16(b, 6, kResultDiagnosticsBytesV1);
    p32(b, 12, kResultDiagnosticsValidV1
        | kResultDiagnosticsLowLoadBiasInvalidV1
        | kResultDiagnosticsPlcStaleV1
        | kResultDiagnosticsPredictionActiveV1
        | (0x5U << 8) | (0xaU << 12) | (0x2U << 16));
    for (int o = 16; o <= 48; o += 8)
        p64(b, o, qFromLittleEndian<quint64>(result.constData() + o));
    p32(b, 56, 0x0f);
    p32(b, 60, 20000);
    for (int rod = 0; rod < 4; ++rod) {
        pf64(b, 64 + rod * 8, 100.25 + rod);
        p32(b, 96 + rod * 4, 0);
        p32(b, 128 + rod * 4, rod == 0 ? 12U : rod == 1 ? 18U : 24U);
    }
    p32(b, 112, 0);
    p32(b, 116, 3);
    p32(b, 120, 4);
    p32(b, 124, 24);
    p32(b, 144, 21845);
    p32(b, 148, 50);
    p32(b, 152, 2);
    seal(b, 8);
    return b;
}
QByteArray state(Domain d)
{
    QByteArray b(352, 0);
    p32(b, 0, d == Domain::DeviceModel ? 0x32434455 : 0x31504955);
    p32(b, 4, d == Domain::DeviceModel ? 2 : 1);
    p32(b, 8, 352);
    p32(b, 12, 1);
    p32(b, 20, 8);
    for (int o = 32; o <= 56; o += 8)
        p64(b, o, quint64(o));
    p32(b, 104, d == Domain::DeviceModel ? 2 : 1);
    p32(b, 108, 1);
    p32(b, 112, 1);
    p32(b, 116, d == Domain::DeviceModel ? 31 : 0);
    b.replace(120, 32, QByteArray(32, 's'));
    seal(b, 344);
    return b;
}
QByteArray document(Domain d)
{
    QByteArray b(256, 0);
    p32(b, 0, d == Domain::DeviceModel ? 0x32434455 : 0x31504955);
    p32(b, 4, d == Domain::DeviceModel ? 2 : 1);
    p32(b, 8, 256);
    p32(b, 12, 256);
    p32(b, 20, 1);
    for (int o = 24; o <= 48; o += 8)
        p64(b, o, quint64(o + 8));
    p32(b, 72, d == Domain::DeviceModel ? 2 : 1);
    p32(b, 76, 1);
    p32(b, 80, 1);
    p32(b, 84, d == Domain::DeviceModel ? 31 : 0);
    b.replace(88, 32, QByteArray(32, 's'));
    seal(b, 188);
    return b;
}
QByteArray policyDocument()
{
    const QByteArray json =
        R"({"schema_version":1,"configuration_generation":7,"system_package_id_sha256":"101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f","system_input_policy_id_sha256":"e5ac4cdf9ceff86657a3397d18fd05a439c06c61260204fc4e84193e9fc8c975","plc_interlock_policy":"PLC_OPTIONAL_INTERLOCK","plc_state_encoding":"PROTOCOL_A_3_STATE"})";
    auto b = document(Domain::InputPolicy);
    b.append(json);
    p32(b, 12, b.size());
    p32(b, 16, 1);
    p64(b, 56, 7);
    p32(b, 76, 7);
    p32(b, 80, 3);
    p32(b, 184, json.size());
    b.replace(88, 32, QByteArray::fromHex("101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f"));
    b.replace(120, 32, QByteArray::fromHex("e5ac4cdf9ceff86657a3397d18fd05a439c06c61260204fc4e84193e9fc8c975"));
    b.replace(152, 32, QCryptographicHash::hash(json, QCryptographicHash::Sha256));
    seal(b, 188);
    return b;
}
void goldenRequests()
{
    QFile f(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath() +
            "/fixtures/arm_product_v9/UCM_WINDOWS_USB_GOLDEN_V1.json");
    expect(f.open(QIODevice::ReadOnly), "ARM golden fixture opens");
    auto all = QJsonDocument::fromJson(f.readAll()).object()["vectors"].toArray();
    int matched = 0;
    for (auto item : all)
    {
        auto j = item.toObject();
        const auto name = j["name"].toString();
        if (!name.contains("query_state"))
            continue;
        auto expected = QByteArray::fromHex(j["payload_hex"].toString().toLatin1());
        auto id = qFromLittleEndian<quint64>(expected.constData() + 40);
        expect(encodeQuery(name.startsWith("device") ? Domain::DeviceModel : Domain::InputPolicy, 4, id) == expected,
               name);
        ++matched;
    }
    expect(matched == 2, "both ARM query fixtures matched");
}
QByteArray fixture(const QString &name)
{
    QFile f(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath() + "/fixtures/arm_product_v9/" + name);
    expect(f.open(QIODevice::ReadOnly), name + " opens");
    return f.readAll();
}
void armResponses()
{
    QString error;
    for (auto d : {Domain::DeviceModel, Domain::InputPolicy})
    {
        const auto prefix = d == Domain::DeviceModel ? QString("device_model") : QString("input_policy");
        State s;
        expect(decodeState(fixture(prefix + "_state.bin"), d, &s, &error), prefix + " ARM state " + error);
        for (const QString kind : {"active", "startup"})
        {
            Document doc;
            expect(decodeDocument(fixture(prefix + "_" + kind + ".bin"), d, &doc, &error),
                   prefix + " ARM " + kind + " " + error);
            expect(documentMatchesState(doc, s), prefix + " " + kind + " state match");
        }
        const auto json = fixture(prefix == "device_model" ? "device_model.json" : "input_policy.json");
        QByteArray id;
        expect(computeDocumentIdentity(d, json, &id, &error), prefix + " draft canonical " + error);
        expect(id == s.activeId, prefix + " independent ARM canonical match");
        const auto object = QJsonDocument::fromJson(json).object();
        const auto serialized = serializeDocument(d, object, &error);
        expect(!serialized.isEmpty(), prefix + " serializer accepts current schema " + error);
        QByteArray serializedId;
        expect(computeDocumentIdentity(d, serialized, &serializedId, &error) && serializedId == s.activeId,
               prefix + " serializer preserves ARM semantic identity");
        auto duplicate = json;
        duplicate.insert(1, "\"schema_version\":1,");
        expect(!computeDocumentIdentity(d, duplicate, &id), "duplicate JSON key rejected");
        duplicate = json;
        duplicate.insert(1, "\"schema_versi\\u006fn\":1,");
        expect(!computeDocumentIdentity(d, duplicate, &id), "escaped duplicate JSON key rejected");
        auto invalid = json;
        invalid.insert(1, QByteArray("\"bad") + char(0xff) + "\":0,");
        expect(!computeDocumentIdentity(d, invalid, &id), "invalid UTF8 rejected");
        invalid = json;
        invalid.replace("\"configuration_generation\":7", "\"configuration_generation\":9223372036854775808");
        expect(!computeDocumentIdentity(d, invalid, &id),
               "unsupported unsigned JSON generation rejected without rounding");
    }
    Publication p;
    InputStatus i;
    expect(decodeInputStatus(fixture("input_status.bin"), &i, &error), "ARM status " + error);
    for (const QString name : {"publication_valid.bin", "publication_invalid.bin"})
    {
        expect(decodePublication(fixture(name), &p, &error), name + " decode " + error);
        expect(samePublication(p.identity, i.identity), name + " six-field ARM match");
    }
    expect(decodePublication(fixture("publication_maintenance.bin"), &p, &error),
           "ARM maintenance INVALID accepted " + error);
    expect(!p.valid && !p.identity.frameCounter, "maintenance has no captured frame");
    expect(!samePublication(p.identity, i.identity), "maintenance cannot borrow prior sidecar");
    LogCatalog c;
    expect(decodeLogCatalog(fixture("log_catalog.bin"), &c, &error), "ARM log catalog " + error);
    LogChunk chunk;
    if (!c.entries.isEmpty())
        expect(decodeLogChunk(fixture("log_chunk.bin"), c.entries[0], 0, 680, &chunk, &error),
               "ARM log chunk " + error);
    LogCatalog native;
    expect(decodeLogCatalog(fixture("log_catalog_native.bin"), &native, &error),
           "R2S ARM native log catalog " + error);
    expect(native.entries.size() == 1 && native.totalEntries == 1,
           "R2S ARM native catalog count");
    if (native.entries.size() == 1)
    {
        const auto &entry = native.entries[0];
        expect(entry.kind == 2 && entry.name == "result-20260913-1205.url3"
                   && entry.totalBytes == 17 * 256,
               "R2S ARM native entry identity");
        expect(encodeLogReadRequest(entry, 0, 65536, &error) == fixture("log_read_request_native.bin"),
               "Windows log read request matches ARM request " + error);
        LogChunk first, last;
        const bool firstOk = decodeLogChunk(fixture("log_chunk_native_first.bin"), entry, 0, 65536,
                                            &first, &error);
        expect(firstOk && first.more && first.data.size() == 4096,
               "Windows decodes ARM first native chunk " + error);
        const bool lastOk = decodeLogChunk(fixture("log_chunk_native_last.bin"), entry, 4096, 65536,
                                           &last, &error);
        expect(lastOk && !last.more && last.data.size() == 256,
               "Windows decodes ARM terminal native chunk " + error);
        if (firstOk && lastOk)
            expect(first.data + last.data == fixture("log_payload_native.bin"),
                   "Windows reconstructs exact ARM native log bytes");
    }
}
void bodyReferenceSchema2()
{
    const auto json = fixture("device_model.json");
    const auto object = QJsonDocument::fromJson(json).object();
    QByteArray identity;
    QString error;
    expect(computeDocumentIdentity(Domain::DeviceModel, json, &identity, &error) &&
               identity.toHex() == "42f9022dc3e0fd80358ce9148896d7aef64850e928495128d0cb6ff1d5b4d5b3",
           "DCI2 512-byte identity matches ARM independent golden");
    const auto serialized = serializeDocument(Domain::DeviceModel, object, &error);
    expect(validateDeviceModelGeometry(object, &error), "ARM schema2 reference geometry is locally valid");
    for (const auto &key : {"l_total_mm", "l_b_mm", "l_d_mm", "l_e_mm", "abeq_mm2", "ac_mm2", "adeq_mm2",
                            "thread_root_diameter_mm", "fixed_mold_thickness_mm"})
    {
        auto invalidGeometry = object;
        invalidGeometry[key] = 0;
        expect(!validateDeviceModelGeometry(invalidGeometry), QString::fromLatin1(key) + " zero geometry rejected");
        expect(serializeDocument(Domain::DeviceModel, invalidGeometry).isEmpty(), "serializer gates invalid geometry");
    }
    auto invalidGeometry = object;
    invalidGeometry["body_reference_mm"] = object["l_total_mm"];
    expect(!validateDeviceModelGeometry(invalidGeometry), "reference A must remain positive");
    invalidGeometry = object;
    invalidGeometry["fixed_mold_thickness_mm"] =
        object["mold_reference_mm"].toDouble() + object["l_total_mm"].toDouble();
    expect(!validateDeviceModelGeometry(invalidGeometry), "fixed-mold A must remain positive");
    invalidGeometry = object;
    invalidGeometry["mold_reference_mm"] = 5000;
    expect(!validateDeviceModelGeometry(invalidGeometry), "fixed-mold C must remain positive");
    invalidGeometry = object;
    invalidGeometry["rod_diameter_mm"] = QJsonArray{90, 0, 90, 90};
    expect(!validateDeviceModelGeometry(invalidGeometry), "every rod diameter must be positive");
    for (const auto &key : {"phi_b", "phi_d", "kmat_unified"})
    {
        invalidGeometry = object;
        invalidGeometry[key] = QJsonValue::Null;
        expect(!validateDeviceModelGeometry(invalidGeometry), "missing finite force parameter rejected");
    }
    const QStringList orderedKeys{"schema_version",
                                  "configuration_generation",
                                  "system_package_id_sha256",
                                  "device_model_config_id_sha256",
                                  "device_model_id",
                                  "applicability_id",
                                  "pack_id",
                                  "model_name",
                                  "applicability_name",
                                  "kmat_unified",
                                  "coupling_bias_ns",
                                  "l_total_mm",
                                  "l_b_mm",
                                  "l_d_mm",
                                  "l_e_mm",
                                  "abeq_mm2",
                                  "ac_mm2",
                                  "adeq_mm2",
                                  "phi_b",
                                  "phi_d",
                                  "rod_diameter_mm",
                                  "thread_root_diameter_mm",
                                  "mold_source_mode",
                                  "fixed_mold_thickness_mm",
                                  "geometry_model",
                                  "mold_reference_mm",
                                  "body_reference_mm",
                                  "min_ncc_peak",
                                  "open_stable_confirm_frames"};
    qsizetype previous = -1;
    for (const auto &key : orderedKeys)
    {
        const auto position = serialized.indexOf('"' + key.toUtf8() + "\":");
        expect(position > previous, "ARM fixed serializer key order: " + key);
        previous = position;
    }
    auto rejected = [&](QJsonObject altered, const QString &label) {
        const auto bytes = QJsonDocument(altered).toJson(QJsonDocument::Compact);
        expect(!computeDocumentIdentity(Domain::DeviceModel, bytes, &identity), label + " identity rejected");
        expect(serializeDocument(Domain::DeviceModel, altered).isEmpty(), label + " serialization rejected");
    };
    auto altered = object;
    altered["schema_version"] = 1;
    rejected(altered, "legacy schema 1");
    for (const auto &key : {"a_segment_offset_mm", "mold_to_a_end_scale", "mold_to_a_end_offset_mm",
                            "mold_to_a_end_mapping_id", "verified", "unknown_field"})
    {
        altered = object;
        altered[key] = 1;
        rejected(altered, QString::fromLatin1(key));
    }
    altered = object;
    altered["geometry_model"] = "LEGACY_A_END";
    rejected(altered, "non-BODY geometry");
    for (const auto &key : {"mold_reference_mm", "body_reference_mm"})
    {
        altered = object;
        altered.remove(key);
        rejected(altered, QString::fromLatin1(key) + " absent");
        altered = object;
        altered[key] = 0;
        rejected(altered, QString::fromLatin1(key) + " zero");
    }
    altered = object;
    altered["pack_id"] = "requires\\escape";
    rejected(altered, "ARM identity string escaping unsupported");
    // Even with repaired payload SHA and CRC, an alphabetically serialized
    // response is not an object admitted by the ARM fixed-order decoder.
    auto wire = fixture("device_model_active.bin").left(256);
    const auto alphabetic = QJsonDocument(object).toJson(QJsonDocument::Compact);
    wire.append(alphabetic);
    p32(wire, 12, quint32(wire.size()));
    p32(wire, 184, quint32(alphabetic.size()));
    wire.replace(152, 32, QCryptographicHash::hash(alphabetic, QCryptographicHash::Sha256));
    seal(wire, 188);
    Document document;
    expect(!decodeDocument(wire, Domain::DeviceModel, &document), "alphabetic external DeviceModel JSON rejected");
}

void bodyReferenceSchema3Correction()
{
    auto object = QJsonDocument::fromJson(fixture("device_model.json")).object();
    object["schema_version"] = 3;
    object["device_model_config_id_sha256"] = QString(64, QLatin1Char('0'));
    object["force_correction_knot_count"] = 3;
    object["force_correction_input_n"] = QJsonArray{0, 10000, 20000, 0, 0, 0, 0, 0};
    object["force_correction_output_n"] = QJsonArray{0, 11000, 25000, 0, 0, 0, 0, 0};
    QString error;
    QByteArray identity;
    auto serialized = serializeDocument(Domain::DeviceModel, object, &error);
    expect(!serialized.isEmpty() &&
               computeDocumentIdentity(Domain::DeviceModel, serialized, &identity, &error),
           "schema3 correction identity computes " + error);
    expect(identity.toHex() == "b789e9d29851d8ad46c52365f686b01e19c2de24d007dcc19b8a94304a4e7c49",
           "schema3 648-byte identity matches independent ARM result");
    object["device_model_config_id_sha256"] = QString::fromLatin1(identity.toHex());
    serialized = serializeDocument(Domain::DeviceModel, object, &error);
    QByteArray verified;
    expect(!serialized.isEmpty() &&
               computeDocumentIdentity(Domain::DeviceModel, serialized, &verified, &error) &&
               verified == identity,
           "schema3 correction serializes in ARM field order " + error);

    auto rejected = object;
    rejected["force_correction_input_n"] = QJsonArray{0, 20000, 10000, 0, 0, 0, 0, 0};
    expect(serializeDocument(Domain::DeviceModel, rejected).isEmpty(),
           "non-monotone schema3 correction rejected");
    rejected = object;
    rejected["force_correction_output_n"] = QJsonArray{1, 11000, 25000, 0, 0, 0, 0, 0};
    expect(serializeDocument(Domain::DeviceModel, rejected).isEmpty(),
           "non-zero correction origin rejected");
    rejected = object;
    rejected["force_correction_input_n"] = QJsonArray{0, 10000, 20000, 1, 0, 0, 0, 0};
    expect(serializeDocument(Domain::DeviceModel, rejected).isEmpty(),
           "non-zero unused correction node rejected");
}

void gw1850rFrozenStructureCalibration()
{
    auto gw = QJsonDocument::fromJson(fixture("device_model.json")).object();
    gw["schema_version"] = 3;
    gw["device_model_config_id_sha256"] = QString(64, QLatin1Char('0'));
    gw["device_model_id"] = 37;
    gw["model_name"] = "GW1850R";
    gw["pack_id"] = "GW1850R-R2S";
    gw["geometry_model"] = "GW_DRAWING_FE_ENGINEERING_V1";
    gw["l_total_mm"] = 5230;
    gw["l_b_mm"] = 0;
    gw["l_d_mm"] = 0;
    gw["l_e_mm"] = 0;
    gw["abeq_mm2"] = 53816.963;
    gw["ac_mm2"] = 61575.216;
    gw["adeq_mm2"] = 53816.963;
    gw["phi_b"] = 0;
    gw["phi_d"] = 0;
    gw["rod_diameter_mm"] = QJsonArray{280, 280, 280, 280};
    gw["thread_root_diameter_mm"] = 250;
    gw["fixed_mold_thickness_mm"] = 662;
    gw["mold_reference_mm"] = 662;
    gw["body_reference_mm"] = 0;
    gw["force_correction_knot_count"] = 3;
    gw["force_correction_input_n"] = QJsonArray{0, 10000, 20000, 0, 0, 0, 0, 0};
    gw["force_correction_output_n"] = QJsonArray{0, 10500, 21500, 0, 0, 0, 0, 0};
    QString error;
    QByteArray identity;
    auto serialized = serializeDocument(Domain::DeviceModel, gw, &error);
    expect(!serialized.isEmpty() && validateDeviceModelGeometry(gw, &error) &&
               computeDocumentIdentity(Domain::DeviceModel, serialized, &identity, &error),
           "GW1850R frozen structure accepts calibration fields " + error);
    gw["device_model_config_id_sha256"] = QString::fromLatin1(identity.toHex());
    serialized = serializeDocument(Domain::DeviceModel, gw, &error);
    expect(!serialized.isEmpty(), "GW1850R identity-bearing document serializes " + error);

    auto changed = gw;
    changed["ac_mm2"] = 61575.0;
    expect(!validateDeviceModelGeometry(changed, &error) &&
               serializeDocument(Domain::DeviceModel, changed, &error).isEmpty(),
           "GW1850R structural area cannot be edited");
    changed = gw;
    changed["geometry_model"] = "BODY_REFERENCE_V1";
    expect(!validateDeviceModelGeometry(changed, &error),
           "GW1850R engineering identity cannot be replaced");
}
} // namespace
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QString error;
    goldenRequests();
    armResponses();
    bodyReferenceSchema2();
    bodyReferenceSchema3Correction();
    gw1850rFrozenStructureCalibration();
    expect(encodeQuery(Domain::DeviceModel, 1, 1, &error).isEmpty(), "write operation rejected");
    expect(encodeQuery(Domain::InputPolicy, 4, 0).isEmpty(), "zero request identity rejected");
    for (auto d : {Domain::DeviceModel, Domain::InputPolicy})
    {
        State s;
        Document doc;
        auto b = state(d);
        expect(decodeState(b, d, &s, &error), "state valid " + error);
        auto db = document(d);
        expect(decodeDocument(db, d, &doc, &error), "absent document valid " + error);
        expect(documentMatchesState(doc, s), "absent current document matches state");
        db[216] = 1;
        seal(db, 188);
        expect(!decodeDocument(db, d, &doc), "document reserved rejected after CRC repair");
        b[348] = 1;
        seal(b, 344);
        expect(!decodeState(b, d, &s), "state reserved rejected after CRC repair");
        expect(s.bootId == 0, "decode failure clears stale state");
    }
    Document doc;
    auto pd = policyDocument();
    expect(decodeDocument(pd, Domain::InputPolicy, &doc, &error), "ARM policy canonical golden identity " + error);
    pd[120] = char(pd[120] ^ 1);
    seal(pd, 188);
    expect(!decodeDocument(pd, Domain::InputPolicy, &doc), "canonical identity rejected with repaired CRC");
    pd = policyDocument();
    pd[pd.size() - 3] = 'X';
    seal(pd, 188);
    expect(!decodeDocument(pd, Domain::InputPolicy, &doc), "payload SHA rejected with repaired CRC");
    Publication pub;
    InputStatus status;
    auto pb = publication();
    auto ib = input();
    expect(decodePublication(pb, &pub, &error), "publication valid " + error);
    expect(!pub.valid && !pub.formalTotalAvailable, "invalid ARM result remains invalid");
    Publication qualityPub;
    auto qualityPublication = pb;
    p32(qualityPublication, 12,
        kPublicationPredictionActiveV3 | (0x5U << 8)
            | (0xaU << 12) | (0x2U << 16));
    seal(qualityPublication, 8);
    expect(decodePublication(qualityPublication, &qualityPub, &error)
        && qualityPub.predictionMask == 0x5
        && qualityPub.qualityDegradedMask == 0xa
        && qualityPub.commonTrendOutlierMask == 0x2,
        "publication v3 accepts defined quality masks");
    Publication rejectedPub;
    auto unknownPublication = pb;
    p32(unknownPublication, 12, 1U << 7);
    seal(unknownPublication, 8);
    expect(!decodePublication(unknownPublication, &rejectedPub),
        "publication v3 rejects undefined flag gaps");
    auto inconsistentPrediction = qualityPublication;
    p32(inconsistentPrediction, 12,
        qFromLittleEndian<quint32>(inconsistentPrediction.constData() + 12)
            & ~kPublicationPredictionActiveV3);
    seal(inconsistentPrediction, 8);
    expect(!decodePublication(inconsistentPrediction, &rejectedPub),
        "publication prediction summary must match its rod mask");
    expect(decodeInputStatus(ib, &status, &error), "input sidecar valid " + error);
    expect(samePublication(pub.identity, status.identity), "six-field match");
    ResultDiagnostics diag;
    auto diagnosticWire = diagnostics();
    expect(decodeResultDiagnostics(diagnosticWire, &diag, &error),
           "revision9 RESULT_DIAGNOSTICS decodes " + error);
    expect(resultDiagnosticsMatchesPublication(diag, pub.identity)
        && diag.strainMicrostrain[0] == 100.25
        && diag.biasValidMinTotalForceN == 20000
        && (diag.flags & kResultDiagnosticsLowLoadBiasInvalidV1)
        && diag.predictionMask == 0x5
        && diag.qualityDegradedMask == 0xa
        && diag.commonTrendOutlierMask == 0x2
        && diag.actualBurstCycles == 2,
        "RESULT_DIAGNOSTICS carries paired strain, quality masks and AGC state");
    auto changedDiagnostics = diagnosticWire;
    p64(changedDiagnostics, 40, diag.sequence + 1);
    seal(changedDiagnostics, 8);
    expect(decodeResultDiagnostics(changedDiagnostics, &diag)
        && !resultDiagnosticsMatchesPublication(diag, pub.identity),
        "RESULT_DIAGNOSTICS mismatched sequence is never joined");
    auto invalidDiagnostics = diagnosticWire;
    p32(invalidDiagnostics, 156, 1);
    seal(invalidDiagnostics, 8);
    expect(!decodeResultDiagnostics(invalidDiagnostics, &diag),
           "RESULT_DIAGNOSTICS reserved bytes are strict");
    invalidDiagnostics = diagnosticWire;
    p32(invalidDiagnostics, 12,
        qFromLittleEndian<quint32>(invalidDiagnostics.constData() + 12)
            | (1U << 4));
    seal(invalidDiagnostics, 8);
    expect(!decodeResultDiagnostics(invalidDiagnostics, &diag),
           "RESULT_DIAGNOSTICS rejects undefined flag gap bits");
    invalidDiagnostics = diagnosticWire;
    p32(invalidDiagnostics, 12,
        qFromLittleEndian<quint32>(invalidDiagnostics.constData() + 12)
            & ~kResultDiagnosticsPredictionActiveV1);
    seal(invalidDiagnostics, 8);
    expect(!decodeResultDiagnostics(invalidDiagnostics, &diag),
           "RESULT_DIAGNOSTICS prediction summary must match its rod mask");
    invalidDiagnostics = diagnosticWire;
    p32(invalidDiagnostics, 120, 12);
    seal(invalidDiagnostics, 8);
    expect(!decodeResultDiagnostics(invalidDiagnostics, &diag),
           "RESULT_DIAGNOSTICS rejects unknown AGC reason");
    for (int o : {16, 24, 32, 40, 48, 56})
    {
        auto changed = pb;
        p64(changed, o, qFromLittleEndian<quint64>(changed.constData() + o) + 1);
        seal(changed, 8);
        Publication other;
        expect(decodePublication(changed, &other), "independent publication valid");
        expect(!samePublication(other.identity, status.identity),
               "pair rejects changed identity offset " + QString::number(o));
    }
    for (int o : {0, 4, 6, 8, 12, 344, 356, 260})
    {
        auto changed = ib;
        changed[o] = char(changed[o] ^ 128);
        if (o != 8)
            seal(changed, 8);
        expect(!decodeInputStatus(changed, &status), "sidecar malformed offset " + QString::number(o));
    }
    for (bool geometry : {false, true})
    {
        auto sidecar = ib;
        p32(sidecar, 12, 1 | 4 | (geometry ? 8 : 0));
        p64(sidecar, 176, 1); // fixed mold instance; fixed source has no live sample clock
        p64(sidecar, 200, 500000);
        p64(sidecar, 208, 500000);
        p64(sidecar, 224, geometry ? 280000 : 0);
        p32(sidecar, 236, 3);
        p32(sidecar, 240, 1);
        p32(sidecar, 252, 1);
        p32(sidecar, 256, geometry ? 1 : 0);
        seal(sidecar, 8);
        expect(decodeInputStatus(sidecar, &status), "RIS1 zero historical A-end slot accepted for both geometry flags");
        for (quint64 reserved : {quint64(1), quint64(UINT64_MAX)})
        {
            auto invalidSidecar = sidecar;
            p64(invalidSidecar, 216, reserved);
            seal(invalidSidecar, 8);
            expect(!decodeInputStatus(invalidSidecar, &status),
                   "RIS1 nonzero reserved A-end slot rejected after CRC repair");
        }
    }
    auto bound = ib;
    p32(bound, 12, 1 | 16 | 128);
    p64(bound, 96, 1);
    p64(bound, 104, 1);
    bound.replace(144, 32, QByteArray(32, 1));
    p64(bound, 296, 950);
    p32(bound, 304, 1);
    p32(bound, 308, 1);
    p32(bound, 332, 1);
    p32(bound, 340, 1);
    seal(bound, 8);
    expect(decodeInputStatus(bound, &status, &error), "ARM-only policy projection " + error);
    p32(bound, 336, 1);
    seal(bound, 8);
    expect(!decodeInputStatus(bound, &status), "forged PLC participation rejected");
    LogEntry entry{"result-20260909-001.urs2", 5, 123, 680, 100, 999};
    auto rq = encodeLogReadRequest(entry, 0, 680, &error);
    expect(rq.size() == 96, "log request valid " + error);
    expect(encodeLogCatalogRequest().size() == 32, "catalog request size");
    QByteArray catalog(3136, 0);
    p32(catalog, 0, 0x32434c55);
    p16(catalog, 4, 2);
    p16(catalog, 6, 3136);
    p64(catalog, 16, 1);
    p32(catalog, 24, 1);
    p32(catalog, 28, 1);
    p32(catalog, 64, 1);
    p32(catalog, 68, 5);
    p64(catalog, 72, 680);
    p64(catalog, 80, 100);
    p64(catalog, 88, 999);
    p32(catalog, 96, 123);
    catalog.replace(104, entry.name.size(), entry.name.toLatin1());
    seal(catalog, 8);
    LogCatalog logs;
    expect(decodeLogCatalog(catalog, &logs, &error), "catalog valid " + error);
    p32(catalog, 64, 2);
    p64(catalog, 72, 512);
    catalog.replace(104, 32, QByteArray(32, 0));
    const QByteArray nativeName("result-20260909-1205.url3");
    catalog.replace(104, nativeName.size(), nativeName);
    seal(catalog, 8);
    expect(decodeLogCatalog(catalog, &logs, &error) && logs.entries.size() == 1
        && logs.entries[0].kind == 2, "R2S URL3 catalog valid " + error);
    p32(catalog, 64, 1);
    seal(catalog, 8);
    expect(!decodeLogCatalog(catalog, &logs), "URL3 cannot masquerade as URS2");
    p32(catalog, 64, 2);
    seal(catalog, 8);
    catalog[160] = 1;
    seal(catalog, 8);
    expect(!decodeLogCatalog(catalog, &logs), "unused catalog slot rejected");
    QByteArray cb(744, 0);
    p32(cb, 0, 0x32524c55);
    p16(cb, 4, 2);
    p16(cb, 6, 64);
    p64(cb, 16, 999);
    p64(cb, 32, 680);
    p32(cb, 40, 680);
    p32(cb, 44, 123);
    seal(cb, 8, 64);
    LogChunk chunk;
    expect(decodeLogChunk(cb, entry, 0, 680, &chunk, &error), "log chunk valid " + error);
    expect(chunk.data.size() == 680 && !chunk.more, "log end detected");
    expect(!decodeLogChunk(cb, entry, 1, 680, &chunk), "chunk offset mismatch rejected");
    p64(cb, 16, 1000);
    seal(cb, 8, 64);
    expect(!decodeLogChunk(cb, entry, 0, 680, &chunk), "chunk snapshot mismatch rejected");
    QTextStream(stdout) << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}

