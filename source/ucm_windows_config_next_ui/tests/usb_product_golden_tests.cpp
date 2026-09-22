#include "usb_wire_v1.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <QtEndian>

// Frozen ARM request bytes are independent of the Windows encoder. Read u64
// identities from the wire, never via a JSON double (some exceed 2^53).
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QFile input(QString::fromUtf8(UCM_PRODUCT_GOLDEN_PATH));
    if (!input.open(QIODevice::ReadOnly)) return 1;
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(input.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) return 2;
    const auto root = document.object();
    if (root.value("protocol_revision").toInt() != 9) return 3;
    const auto vectors = root.value("vectors").toArray();
    if (vectors.size() != 7) return 4;
    int failures = 0;
    for (const auto &value : vectors) {
        const auto item = value.toObject();
        const QByteArray expected = QByteArray::fromHex(item.value("frame_hex").toString().toLatin1());
        if (expected.size() < 40) return 5;
        const auto *wire = reinterpret_cast<const uchar *>(expected.constData());
        const auto sequence = qFromLittleEndian<quint64>(wire + 16);
        const auto transaction = qFromLittleEndian<quint64>(wire + 24);
        QString error;
        const auto encoded = ucm::encodeUsbFrameV1(
            static_cast<ucm::UsbMessageTypeV1>(item.value("message_type").toInt()),
            0, sequence, transaction,
            QByteArray::fromHex(item.value("payload_hex").toString().toLatin1()), &error);
        const bool passed = encoded == expected;
        QTextStream(stdout) << (passed ? "PASS " : "FAIL ") << item.value("name").toString() << '\n';
        if (!passed) ++failures;
    }
    return failures == 0 ? 0 : 6;
}
