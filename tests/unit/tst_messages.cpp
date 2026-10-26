// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/protocol/Frame.h"
#include "opentalk/protocol/Messages.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QTest>

using namespace opentalk::protocol;

namespace {

/// Encodes `message` and returns only the CBOR payload (without the frame header).
QByteArray payloadOf(const Message &message)
{
    return encodeMessage(message).mid(kFrameHeaderSize);
}

quint16 typeOf(MessageType type)
{
    return static_cast<quint16>(type);
}

bool isMalformed(const MessageDecodeResult &result)
{
    const auto *failure = std::get_if<MessageDecodeFailure>(&result);
    return failure != nullptr && failure->code == ErrorCode::MalformedMessage;
}

QByteArray cbor(const QCborMap &map)
{
    return map.toCborValue().toCbor();
}

QCborMap validHello()
{
    return {{1, 1}, {2, 1}, {3, QStringLiteral("Client")}, {4, QStringLiteral("1.0")}};
}

} // namespace

class TestMessages : public QObject {
    Q_OBJECT

private slots:
    void roundTrip_data()
    {
        QTest::addColumn<Message>("message");
        QTest::newRow("hello") << Message(Hello{{1, 3}, QStringLiteral("OpenTalk"), QStringLiteral("0.2.0")});
        QTest::newRow("welcome") << Message(
            Welcome{1, QStringLiteral("Café Talk ☕"), QStringLiteral("0.2.0"), 65536, 15});
        QTest::newRow("ping") << Message(Ping{42});
        QTest::newRow("pong") << Message(Pong{0x7fffffffffffffffULL});
        QTest::newRow("goodbye") << Message(Goodbye{ErrorCode::ServerShuttingDown});
        QTest::newRow("error") << Message(Error{ErrorCode::RateLimited, 7, QStringLiteral("slow down")});
        QTest::newRow("error without request id")
            << Message(Error{ErrorCode::ProtocolViolation, std::nullopt, QString()});
    }

    void roundTrip()
    {
        QFETCH(Message, message);
        const QByteArray frame = encodeMessage(message);
        FrameDecoder decoder;
        QList<Frame> frames;
        QVERIFY(decoder.feed(frame, frames) == DecodeError::None);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames.first().type, static_cast<quint16>(messageType(message)));
        const MessageDecodeResult result = decodeMessage(frames.first().type, frames.first().payload);
        QVERIFY(std::holds_alternative<Message>(result));
        QVERIFY(std::get<Message>(result) == message);
    }

    void unknownTypeIsReportedAsUnknown()
    {
        const MessageDecodeResult result = decodeMessage(0x0999, cbor({}));
        const auto *failure = std::get_if<MessageDecodeFailure>(&result);
        QVERIFY(failure != nullptr);
        QVERIFY(failure->code == ErrorCode::UnknownMessage);
    }

    void unknownKeysAreIgnored()
    {
        QCborMap map = validHello();
        map.insert(99, QStringLiteral("from the future"));
        QVERIFY(std::holds_alternative<Message>(decodeMessage(typeOf(MessageType::Hello), cbor(map))));
    }

    void rejectsInvalidPayloads_data()
    {
        QTest::addColumn<quint16>("type");
        QTest::addColumn<QByteArray>("payload");
        const quint16 hello = typeOf(MessageType::Hello);

        QTest::newRow("empty payload") << hello << QByteArray();
        QTest::newRow("garbage") << hello << QByteArray("\xff\xff\xff", 3);
        QTest::newRow("not a map") << hello << QCborValue(QStringLiteral("hi")).toCbor();
        QTest::newRow("array") << hello << QCborValue(QCborArray{1, 2}).toCbor();
        QTest::newRow("trailing bytes") << hello << cbor(validHello()) + QByteArray("\x00", 1);
        QTest::newRow("truncated") << hello << cbor(validHello()).chopped(2);

        QCborMap m = validHello();
        m.remove(3);
        QTest::newRow("missing name") << hello << cbor(m);
        m = validHello();
        m.insert(1, QStringLiteral("1"));
        QTest::newRow("version as text") << hello << cbor(m);
        m = validHello();
        m.insert(2, 70000);
        QTest::newRow("version out of range") << hello << cbor(m);
        m = validHello();
        m.insert(1, -1);
        QTest::newRow("negative version") << hello << cbor(m);
        m = validHello();
        m.insert(1, 3);
        QTest::newRow("min above max") << hello << cbor(m);
        m = validHello();
        m.insert(1, 0);
        m.insert(2, 0);
        QTest::newRow("version zero") << hello << cbor(m);
        m = validHello();
        m.insert(3, QString(kMaxNameLength + 1, QLatin1Char('a')));
        QTest::newRow("name too long") << hello << cbor(m);
        m = validHello();
        m.insert(3, QStringLiteral("evil\nname"));
        QTest::newRow("control character") << hello << cbor(m);
        m = validHello();
        m.insert(3, QStringLiteral("rtl") + QChar(0x202E) + QStringLiteral("override"));
        QTest::newRow("bidi override") << hello << cbor(m);
        m = validHello();
        m.insert(3, QStringLiteral("   "));
        QTest::newRow("blank name") << hello << cbor(m);

        QTest::newRow("welcome tiny payload limit")
            << typeOf(MessageType::Welcome)
            << cbor({{1, 1}, {2, QStringLiteral("S")}, {3, QStringLiteral("1")}, {4, 10}, {5, 15}});
        QTest::newRow("welcome zero heartbeat")
            << typeOf(MessageType::Welcome)
            << cbor({{1, 1}, {2, QStringLiteral("S")}, {3, QStringLiteral("1")}, {4, 65536}, {5, 0}});
        QTest::newRow("ping float token") << typeOf(MessageType::Ping) << cbor({{1, 1.5}});
        QTest::newRow("ping huge token") << typeOf(MessageType::Ping)
                                         << cbor({{1, QCborValue(static_cast<double>(1e30))}});
        QTest::newRow("error detail too long")
            << typeOf(MessageType::Error) << cbor({{1, 1}, {3, QString(kMaxDetailLength + 1, QLatin1Char('x'))}});
    }

    void rejectsInvalidPayloads()
    {
        QFETCH(quint16, type);
        QFETCH(QByteArray, payload);
        QVERIFY(isMalformed(decodeMessage(type, payload)));
    }

    void unknownErrorCodesAreMappedNotRejected()
    {
        const MessageDecodeResult result = decodeMessage(typeOf(MessageType::Goodbye), cbor({{1, 999}}));
        QVERIFY(std::holds_alternative<Message>(result));
        QVERIFY(std::get<Goodbye>(std::get<Message>(result)).reason == ErrorCode::InternalError);
    }

    void encodedHelloIsCompact()
    {
        // Integer keys keep control messages small.
        QVERIFY(payloadOf(Hello{{1, 1}, QStringLiteral("OpenTalk"), QStringLiteral("0.2.0")}).size() < 32);
    }
};

QTEST_GUILESS_MAIN(TestMessages)
#include "tst_messages.moc"
