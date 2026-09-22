#include "engineer_access.h"

#include <QCryptographicHash>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>

namespace {
constexpr auto kPinSaltKey = "access/engineer_pin_salt_v1";
constexpr auto kPinHashKey = "access/engineer_pin_hash_v1";
constexpr auto kPinSchemaKey = "access/engineer_pin_schema";
constexpr int kPinSchema = 1;
constexpr int kPbkdf2Iterations = 120000;
constexpr int kDerivedBytes = 32;
constexpr int kSaltBytes = 16;
}

EngineerAccess::EngineerAccess(QObject *parent)
    : QObject(parent)
    , m_settings(std::make_unique<QSettings>())
{
}

EngineerAccess::EngineerAccess(const QString &settingsFile, QObject *parent)
    : QObject(parent)
    , m_settings(std::make_unique<QSettings>(settingsFile,
                                             QSettings::IniFormat))
{
}

EngineerAccess::~EngineerAccess() = default;

bool EngineerAccess::pinConfigured() const
{
    return m_settings
        && m_settings->value(QString::fromLatin1(kPinSchemaKey)).toInt()
               == kPinSchema
        && !m_settings->value(QString::fromLatin1(kPinSaltKey))
                .toByteArray().isEmpty()
        && !m_settings->value(QString::fromLatin1(kPinHashKey))
                .toByteArray().isEmpty();
}

bool EngineerAccess::validPin(const QString &pin)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9]{6,12}$"));
    return pattern.match(pin).hasMatch();
}

QByteArray EngineerAccess::deriveHash(const QString &pin,
                                      const QByteArray &salt)
{
    return QPasswordDigestor::deriveKeyPbkdf2(
        QCryptographicHash::Sha256, pin.toUtf8(), salt,
        kPbkdf2Iterations, kDerivedBytes);
}

bool EngineerAccess::constantTimeEqual(const QByteArray &left,
                                       const QByteArray &right)
{
    if (left.size() != right.size()) return false;
    unsigned char difference = 0;
    for (qsizetype index = 0; index < left.size(); ++index) {
        difference |= static_cast<unsigned char>(left.at(index))
            ^ static_cast<unsigned char>(right.at(index));
    }
    return difference == 0;
}

void EngineerAccess::setMessage(const QString &message)
{
    m_message = message;
    emit changed();
}

bool EngineerAccess::configurePin(const QString &pin,
                                  const QString &confirmation)
{
    if (pinConfigured()) {
        setMessage(QStringLiteral("工程师 PIN 已设置；请先验证现有 PIN。"));
        return false;
    }
    if (!validPin(pin)) {
        setMessage(QStringLiteral("PIN 必须是 6–12 位数字。"));
        return false;
    }
    if (pin != confirmation) {
        setMessage(QStringLiteral("两次输入的 PIN 不一致。"));
        return false;
    }

    QByteArray salt(kSaltBytes, Qt::Uninitialized);
    QRandomGenerator *generator = QRandomGenerator::system();
    for (int offset = 0; offset < salt.size(); offset += 4) {
        const quint32 value = generator->generate();
        const int remaining = qMin(4, salt.size() - offset);
        for (int byte = 0; byte < remaining; ++byte)
            salt[offset + byte] = static_cast<char>(value >> (byte * 8));
    }
    const QByteArray hash = deriveHash(pin, salt);
    m_settings->setValue(QString::fromLatin1(kPinSchemaKey), kPinSchema);
    m_settings->setValue(QString::fromLatin1(kPinSaltKey),
                         salt.toBase64());
    m_settings->setValue(QString::fromLatin1(kPinHashKey),
                         hash.toBase64());
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError) {
        m_settings->remove(QString::fromLatin1(kPinSchemaKey));
        m_settings->remove(QString::fromLatin1(kPinSaltKey));
        m_settings->remove(QString::fromLatin1(kPinHashKey));
        setMessage(QStringLiteral("PIN 保存失败；未开放工程师模式。"));
        return false;
    }

    m_engineerMode = true;
    setMessage(QStringLiteral("工程师 PIN 已设置，本次已进入工程师模式。"));
    return true;
}

bool EngineerAccess::enterEngineerMode(const QString &pin)
{
    if (!pinConfigured()) {
        setMessage(QStringLiteral("尚未设置工程师 PIN。"));
        return false;
    }
    if (!validPin(pin)) {
        setMessage(QStringLiteral("PIN 格式不正确。"));
        return false;
    }
    const QByteArray salt = QByteArray::fromBase64(
        m_settings->value(QString::fromLatin1(kPinSaltKey)).toByteArray());
    const QByteArray expected = QByteArray::fromBase64(
        m_settings->value(QString::fromLatin1(kPinHashKey)).toByteArray());
    const QByteArray actual = deriveHash(pin, salt);
    if (salt.size() != kSaltBytes || expected.size() != kDerivedBytes
        || !constantTimeEqual(actual, expected)) {
        setMessage(QStringLiteral("PIN 不正确，仍保持客户模式。"));
        return false;
    }

    m_engineerMode = true;
    setMessage(QStringLiteral("工程师模式已开启。"));
    return true;
}

void EngineerAccess::leaveEngineerMode()
{
    if (!m_engineerMode) return;
    m_engineerMode = false;
    setMessage(QStringLiteral("已返回客户模式；工程配置和诊断入口已锁定。"));
}
