#include "engineer_access.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>

namespace {
int failures = 0;

void expect(bool condition, const char *message)
{
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    expect(temporary.isValid(), "temporary settings directory exists");
    const QString settingsPath = temporary.filePath(QStringLiteral("access.ini"));

    EngineerAccess access(settingsPath);
    expect(!access.engineerMode(), "customer mode is always the startup mode");
    expect(!access.pinConfigured(), "fresh install has no PIN");
    expect(!access.configurePin(QStringLiteral("12345"),
                                QStringLiteral("12345")),
           "short PIN is rejected");
    expect(!access.configurePin(QStringLiteral("secret"),
                                QStringLiteral("secret")),
           "non-numeric PIN is rejected");
    expect(!access.configurePin(QStringLiteral("123456"),
                                QStringLiteral("654321")),
           "mismatched confirmation is rejected");
    expect(access.configurePin(QStringLiteral("314159"),
                               QStringLiteral("314159")),
           "valid PIN is accepted");
    expect(access.pinConfigured() && access.engineerMode(),
           "PIN setup enters engineer mode");
    expect(!access.configurePin(QStringLiteral("271828"),
                                QStringLiteral("271828")),
           "configured PIN cannot be silently replaced");
    access.leaveEngineerMode();
    expect(!access.engineerMode(), "leaving returns to customer mode");
    expect(!access.enterEngineerMode(QStringLiteral("271828")),
           "wrong PIN is rejected");
    expect(access.enterEngineerMode(QStringLiteral("314159")),
           "correct PIN enters engineer mode");

    QSettings stored(settingsPath, QSettings::IniFormat);
    for (const QString &key : stored.allKeys()) {
        expect(!stored.value(key).toString().contains(
                   QStringLiteral("314159")),
               "plain PIN is absent from every stored setting");
    }

    EngineerAccess restarted(settingsPath);
    expect(restarted.pinConfigured(), "PIN survives application restart");
    expect(!restarted.engineerMode(),
           "application restart still defaults to customer mode");
    expect(restarted.enterEngineerMode(QStringLiteral("314159")),
           "persisted PIN verifies after restart");

    if (failures == 0)
        std::puts("engineer access tests PASS");
    return failures == 0 ? 0 : 1;
}
