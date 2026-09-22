#include "config_session.h"
#include "usb_functionfs_transport.h"
#include "usb_extended_wire_v2.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTextStream>
#include <QThread>
#include <memory>
#include <stdexcept>

using namespace ucm;
static QJsonObject parameters(QJsonObject document)
{
    for (auto key : {"configuration_generation", "device_model_config_id_sha256", "system_package_id_sha256"})
        document.remove(QString::fromLatin1(key));
    return document;
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QJsonObject report{{"tool", "UCM_CALIBRATION_WRITE_READBACK_HIL_V1"},
        {"credit", "real USB parameter write/readback only; temporary test coefficients are not certified calibration"}};
    if (!app.arguments().contains("--execute-real-usb")) {
        report.insert("passed", false); report.insert("error", "Explicit real USB execution flag required; device not opened.");
        QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Compact) << '\n'; return 2;
    }
    ConfigurationSession session(std::make_unique<UsbFunctionfsTransport>());
    QJsonArray steps;
    QJsonObject originalActive, originalStartup;
    bool changed = false, restored = false;
    auto require = [](bool condition, const QString &message) {
        if (!condition) throw std::runtime_error(message.toStdString());
    };
    auto read = [&]() {
        auto state = session.readProductState();
        require(state.success && state.deviceModel["available"].toBool(), state.message);
        return state.deviceModel;
    };
    auto send = [&](int operation, QJsonObject document) {
        require(session.renewControlLease().success, "USB host lease renewal failed");
        const auto device = read();
        const auto state = device["state"].toObject();
        const auto authority = session.readControlAuthorityState();
        require(authority.success && usbHostManagedStoppedV2(authority.state), "Hardware safe stop not confirmed");
        productv9::ConfigWriteRequest request;
        request.domain = productv9::Domain::DeviceModel;
        request.operation = operation;
        request.bootId = state["bootId"].toString().toULongLong();
        request.authorityGeneration = state["authorityGeneration"].toString().toULongLong();
        request.sessionId = state["sessionId"].toString().toULongLong();
        request.requestId = (QRandomGenerator::global()->generate64() >> 1) + 1;
        request.entryId = request.requestId;
        request.expectedCurrentId = QByteArray::fromHex(state["activeId"].toString().toLatin1());
        request.systemSha256 = QByteArray::fromHex(state["systemSha256"].toString().toLatin1());
        request.createdMonotonicNs = authority.state.publishedMonotonicNs;
        request.expiresMonotonicNs = request.createdMonotonicNs + 5000000000ULL;
        if (operation == 2) {
            request.resultId = request.expectedCurrentId;
            request.payloadDigest = QByteArray::fromHex(state["activePayloadDigest"].toString().toLatin1());
        } else {
            const auto generation = state["activeGeneration"].toString().toULongLong();
            document["configuration_generation"] = qint64(generation + 1);
            document["system_package_id_sha256"] = QString::fromLatin1(request.systemSha256.toHex());
            QString error;
            request.json = productv9::serializeDocument(request.domain, document, &error);
            require(!request.json.isEmpty(), error);
            require(productv9::computeDocumentIdentity(request.domain, request.json, &request.resultId, &error), error);
            document["device_model_config_id_sha256"] = QString::fromLatin1(request.resultId.toHex());
            request.json = productv9::serializeDocument(request.domain, document, &error);
            request.payloadDigest = QCryptographicHash::hash(request.json, QCryptographicHash::Sha256);
        }
        auto result = session.submitProductConfiguration(request, true);
        for (int i = 0; result.ok && result.outcome == "pending" && i < 80; ++i) {
            QThread::msleep(50);
            if (i % 20 == 0) require(session.renewControlLease().success, "Lease expired during write");
            result = session.pollProductConfiguration(request.requestId + quint64(i + 1));
        }
        steps.append(QJsonObject{{"operation", operation}, {"transactionId", QString::number(request.entryId)},
            {"ok", result.ok}, {"outcome", result.outcome}, {"message", result.message}, {"receipt", result.fields}});
        require(result.ok && result.outcome == "succeeded", result.message.isEmpty() ? "Configuration did not succeed" : result.message);
        return read();
    };
    auto restore = [&]() {
        if (!changed) { restored = true; return; }
        send(1, originalStartup);
        send(2, {});
        send(1, originalActive);
        const auto result = read();
        require(parameters(result["active_document"].toObject()) == parameters(originalActive)
            && parameters(result["startup_document"].toObject()) == parameters(originalStartup), "Original parameters restoration readback failed");
        report.insert("restored_readback", result);
        restored = true;
    };
    int exitCode = 1;
    try {
        require(session.transportInfo().armReceiverContacted, "USB revision9 receiver not reachable");
        const auto discovery = session.usbExtendedDiscovery();
        report.insert("arm_build_id", discovery.capabilities.buildId);
        require(discovery.capabilities.buildId == "ucm-r2s-400-internal", "Unexpected ARM candidate; no write permitted");
        const auto initial = read();
        report.insert("before", initial);
        originalActive = initial["active_document"].toObject();
        originalStartup = initial["startup_document"].toObject();
        require(!originalActive.isEmpty() && !originalStartup.isEmpty(), "Active/startup configuration unavailable");
        const auto takeover = session.switchControlMode(kUsbControlModeHostManagedV2);
        require(takeover.success && usbHostManagedStoppedV2(takeover.state), "HOST_MANAGED safe-stop takeover failed");
        auto test = originalActive;
        test["schema_version"] = 4;
        test["device_model_id"] = 37;
        test["model_name"] = "GW1850R";
        test["pack_id"] = "GW1850R-R2S";
        test["geometry_model"] = "GW_DRAWING_FE_ENGINEERING_V1";
        test["l_total_mm"] = 5230.; test["l_b_mm"] = 0.; test["l_d_mm"] = 0.; test["l_e_mm"] = 0.;
        test["abeq_mm2"] = 53816.963; test["ac_mm2"] = 61575.216; test["adeq_mm2"] = 53816.963;
        test["phi_b"] = 0.; test["phi_d"] = 0.; test["rod_diameter_mm"] = QJsonArray{280.,280.,280.,280.};
        test["thread_root_diameter_mm"] = 250.; test["fixed_mold_thickness_mm"] = 662.; test["mold_reference_mm"] = 662.; test["body_reference_mm"] = 0.;
        test["l_a_mm"] = 0.; test["path_model"] = "NOMINAL_AXIAL_BOUNDARIES_V1"; test["tooth_pitch_mm"] = 30.; test["tooth_crest_width_mm"] = 13.; test["near_boundary_mm"] = 210.; test["far_boundary_intercept_mm"] = 2020.; test["tooth_start_mm"] = 2650.; test["mold_coefficient"] = 1.; test["connection_model"] = "INTEGRATED";
        test["force_correction_knot_count"] = 0;
        test["force_correction_input_n"] = QJsonArray{0.,0.,0.,0.,0.,0.,0.,0.};
        test["force_correction_output_n"] = QJsonArray{0.,0.,0.,0.,0.,0.,0.,0.};
        const auto validated = send(3, test);
        require(parameters(validated["active_document"].toObject()) == parameters(originalActive), "Validate changed active coefficients");
        changed = true;
        const auto applied = send(1, test);
        require(parameters(applied["active_document"].toObject()) == parameters(test), "Apply active parameter readback mismatch");
        report.insert("applied_readback", applied);
        const auto saved = send(2, {});
        require(parameters(saved["active_document"].toObject()) == parameters(test)
            && parameters(saved["startup_document"].toObject()) == parameters(test), "SaveStartup active/startup readback mismatch");
        report.insert("saved_readback", saved);
        require(session.switchControlMode(kUsbControlModeAutonomousV2).success, "Unable to restore autonomous authority");
        report.insert("passed", true);
        exitCode = 0;
    } catch (const std::exception &error) {
        report.insert("passed", false); report.insert("error", QString::fromUtf8(error.what()));
        try { restore(); if (restored) session.switchControlMode(kUsbControlModeAutonomousV2); }
        catch (const std::exception &rollback) { report.insert("restoration_error", QString::fromUtf8(rollback.what())); }
    }
    report.insert("original_parameters_restored", restored);
    report.insert("steps", steps);
    report.insert("readUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Compact) << '\n';
    return exitCode;
}
