#include "arm_force_csv.h"
#include <QCoreApplication>
#include <QTextStream>
#include <limits>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
    auto expect = [&](bool ok, const char *name) { if (!ok) { ++failures; QTextStream(stderr) << name << '\n'; } };
    ucm::TelemetrySnapshot s;
    s.generation = std::numeric_limits<quint64>::max();
    s.sessionId = 9007199254740993ULL;
    s.formalForceValid = true; s.formalTotalN = 400.25;
    s.lowLoadBiasInvalid = true; s.biasValidMinTotalForceN = 20000;
    s.forceAvailableMask = 5; s.rod[0].forceN = 100.125; s.rod[2].forceN = -50.5;
    const auto row = ArmForceCsv::measurement(s, 123).trimmed().split(',');
    expect(row.size() == 27 && ArmForceCsv::header().trimmed().split(',').size() == 27, "CSV column contract");
    expect(row[2] == "18446744073709551615" && row[4] == "9007199254740993", "u64 identity exact decimal");
    expect(row[9] == "1" && row[10] == "20000" && row[11] == "0" && row[12].isEmpty(), "low-load imbalance remains explicitly unavailable");
    expect(row[17] == "400.25" && row[18] == "100.125" && row[20] == "-50.5", "raw ARM newtons preserved");
    expect(row[19].isEmpty() && row[21].isEmpty() && row[22] == "0", "missing force stays empty while diagnostic reason remains explicit");
    s.diagnosticFieldsAvailable = false;
    const auto noDiagnostics = ArmForceCsv::measurement(s, 123).trimmed().split(',');
    expect(noDiagnostics[10].isEmpty() && noDiagnostics[22].isEmpty(), "unavailable diagnostic fields remain empty");
    s.formalForceValid = false; s.forceAvailableMask = 0; s.primaryReasonCode = 9; s.message = "invalid";
    const auto invalid = ArmForceCsv::measurement(s, 124).trimmed().split(',');
    expect(invalid[8] == "0" && invalid[16] == "9" && invalid[17].isEmpty() && invalid[18].isEmpty() && invalid[26] == "invalid", "invalid values empty with reason retained");
    const auto gap = ArmForceCsv::gap(QStringLiteral("USB disconnected"), 125).trimmed().split(',');
    expect(gap.size() == 27 && gap[0] == "gap" && gap[17].isEmpty() && gap[26] == "USB disconnected", "gap retains cause without zero force");
    expect(ArmForceCsv::gap(QStringLiteral("bad,\"frame\"\nretry"), 125).endsWith("\"bad,\"\"frame\"\"\nretry\"\n"), "CSV error text escaped");
    return failures ? 1 : 0;
}
