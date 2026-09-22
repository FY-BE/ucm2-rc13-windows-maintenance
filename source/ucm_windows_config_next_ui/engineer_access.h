#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <memory>

class QSettings;

class EngineerAccess final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool engineerMode READ engineerMode NOTIFY changed)
    Q_PROPERTY(bool pinConfigured READ pinConfigured NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)

public:
    explicit EngineerAccess(QObject *parent = nullptr);
    explicit EngineerAccess(const QString &settingsFile,
                            QObject *parent = nullptr);
    ~EngineerAccess() override;

    bool engineerMode() const { return m_engineerMode; }
    bool pinConfigured() const;
    QString message() const { return m_message; }

    bool configurePin(const QString &pin, const QString &confirmation);
    bool enterEngineerMode(const QString &pin);
    void leaveEngineerMode();

signals:
    void changed();

private:
    static bool validPin(const QString &pin);
    static QByteArray deriveHash(const QString &pin,
                                 const QByteArray &salt);
    static bool constantTimeEqual(const QByteArray &left,
                                  const QByteArray &right);
    void setMessage(const QString &message);

    std::unique_ptr<QSettings> m_settings;
    bool m_engineerMode = false;
    QString m_message = QStringLiteral(
        "客户模式：工程配置和诊断入口已锁定。");
};
