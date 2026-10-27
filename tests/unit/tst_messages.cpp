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
        QTest::newRow("welcome with guests")
            << Message(Welcome{1, QStringLiteral("S"), QStringLiteral("1"), 65536, 15, true});
        QTest::newRow("authenticate account") << Message(
            Authenticate{Authenticate::Mode::Account, QStringLiteral("alice"), QStringLiteral("p \u00e4ss\nword")});
        QTest::newRow("authenticate guest")
            << Message(Authenticate{Authenticate::Mode::Guest, QStringLiteral("Nova"), QString()});
        QTest::newRow("auth result") << Message(AuthResult{7, QStringLiteral("alice"), Role::Admin});
        const ChannelInfo root{kRootChannelId, 0, QString(), QString(), true, false, 0};
        const ChannelInfo lobby{1000, kRootChannelId, QStringLiteral("Lobby"), QStringLiteral("Hi"), true, true, 10};
        const UserInfo alice{7, QStringLiteral("alice"), Role::User, 1000};
        QTest::newRow("snapshot") << Message(StateSnapshot{{root, lobby}, {alice}});
        QTest::newRow("empty snapshot") << Message(StateSnapshot{});
        QTest::newRow("channel added") << Message(ChannelAdded{lobby});
        QTest::newRow("channel removed") << Message(ChannelRemoved{1000});
        QTest::newRow("join") << Message(JoinChannel{5, 1000, QStringLiteral("secret")});
        QTest::newRow("join without password") << Message(JoinChannel{5, 1000, QString()});
        QTest::newRow("create") << Message(CreateChannel{6, kRootChannelId, QStringLiteral("Games"),
                                                         QStringLiteral("Topic"), true, QStringLiteral("pw"), 25});
        QTest::newRow("delete") << Message(DeleteChannel{8, 1000});
        QTest::newRow("done with result") << Message(RequestDone{6, 0x80000000u});
        QTest::newRow("done") << Message(RequestDone{5, std::nullopt});
        QTest::newRow("user joined") << Message(UserJoined{alice});
        QTest::newRow("user left") << Message(UserLeft{7});
        QTest::newRow("user moved") << Message(UserMoved{7, kRootChannelId});
        QTest::newRow("send text") << Message(
            SendText{9, TextTarget::User, 3, QStringLiteral("line one\nline two\t!")});
        // Emoji sequences use the zero-width joiner, which must stay allowed in chat.
        const QString family = QString::fromUtf8("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9");
        QTest::newRow("text message") << Message(TextMessage{TextTarget::Server, 1, 0, family, 1790000000000});
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
        QTest::newRow("auth unknown mode")
            << typeOf(MessageType::Authenticate) << cbor({{1, 2}, {2, QStringLiteral("x")}});
        QTest::newRow("auth account without password")
            << typeOf(MessageType::Authenticate) << cbor({{1, 0}, {2, QStringLiteral("alice")}});
        QTest::newRow("auth name too long")
            << typeOf(MessageType::Authenticate)
            << cbor({{1, 1}, {2, QString(kMaxDisplayNameLength + 1, QLatin1Char('n'))}});
        QTest::newRow("auth name with spaces around")
            << typeOf(MessageType::Authenticate) << cbor({{1, 1}, {2, QStringLiteral(" Nova")}});
        QTest::newRow("auth result user 0")
            << typeOf(MessageType::AuthResult) << cbor({{1, 0}, {2, QStringLiteral("x")}, {3, 1}});
        QTest::newRow("unknown role") << typeOf(MessageType::UserJoined)
                                      << cbor({{1, 5}, {2, QStringLiteral("x")}, {3, 3}, {4, 1}});
        const auto channel = [](qint64 id, qint64 parent, const QString &name) {
            return QCborMap{{1, id}, {2, parent}, {3, name}, {4, QString()}, {5, true}, {6, false}, {7, 0}};
        };
        QTest::newRow("channel id 0") << typeOf(MessageType::ChannelAdded) << cbor(channel(0, 1, QStringLiteral("x")));
        QTest::newRow("root with parent") << typeOf(MessageType::ChannelAdded) << cbor(channel(1, 5, QString()));
        QTest::newRow("channel without parent")
            << typeOf(MessageType::ChannelAdded) << cbor(channel(1000, 0, QStringLiteral("x")));
        QTest::newRow("own parent") << typeOf(MessageType::ChannelAdded)
                                    << cbor(channel(1000, 1000, QStringLiteral("x")));
        QTest::newRow("unnamed channel") << typeOf(MessageType::ChannelAdded) << cbor(channel(1000, 1, QString()));
        QTest::newRow("snapshot entry not a map")
            << typeOf(MessageType::StateSnapshot) << cbor({{1, QCborArray{1}}, {2, QCborArray{}}});
        QTest::newRow("snapshot bad channel")
            << typeOf(MessageType::StateSnapshot)
            << cbor({{1, QCborArray{channel(0, 1, QStringLiteral("x"))}}, {2, QCborArray{}}});
        QTest::newRow("snapshot missing users") << typeOf(MessageType::StateSnapshot) << cbor({{1, QCborArray{}}});
        const auto text = [](const QString &t) { return QCborMap{{1, 1}, {2, 0}, {3, 1}, {4, t}}; };
        QTest::newRow("blank chat") << typeOf(MessageType::SendText) << cbor(text(QStringLiteral(" \n ")));
        QTest::newRow("chat bidi override")
            << typeOf(MessageType::SendText)
            << cbor(text(QStringLiteral("invoice") + QChar(0x202E) + QStringLiteral("fdp.exe")));
        QTest::newRow("chat bidi isolate")
            << typeOf(MessageType::SendText) << cbor(text(QStringLiteral("a") + QChar(0x2067) + QStringLiteral("b")));
        QTest::newRow("chat escape character")
            << typeOf(MessageType::SendText) << cbor(text(QStringLiteral("\x1b[2J")));
        QTest::newRow("chat too long") << typeOf(MessageType::SendText)
                                       << cbor(text(QString(kMaxTextLength + 1, QLatin1Char('x'))));
        QTest::newRow("unknown text target")
            << typeOf(MessageType::SendText) << cbor({{1, 1}, {2, 3}, {3, 1}, {4, QStringLiteral("x")}});
        QTest::newRow("boolean as integer")
            << typeOf(MessageType::CreateChannel) << cbor({{1, 1}, {2, 1}, {3, QStringLiteral("x")}, {5, 1}});
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

    void directionsAreDisjointWhereTheyShouldBe()
    {
        QVERIFY(isClientToServer(MessageType::SendText) && !isServerToClient(MessageType::SendText));
        QVERIFY(isServerToClient(MessageType::TextMessage) && !isClientToServer(MessageType::TextMessage));
        QVERIFY(!isClientToServer(MessageType::StateSnapshot));
        QVERIFY(isClientToServer(MessageType::Ping) && isServerToClient(MessageType::Ping));
    }

    void oversizedMessagesAreNotEncoded()
    {
        StateSnapshot big;
        for (quint32 i = 0; i < 5000; ++i) {
            big.users.append(UserInfo{i + 1, QString(32, QLatin1Char('n')), Role::User, kRootChannelId});
        }
        QVERIFY(encodeMessage(big, 1024).isEmpty());
        QVERIFY(!encodeMessage(big).isEmpty());
    }

    void encodedHelloIsCompact()
    {
        // Integer keys keep control messages small.
        QVERIFY(payloadOf(Hello{{1, 1}, QStringLiteral("OpenTalk"), QStringLiteral("0.2.0")}).size() < 32);
    }
};

QTEST_GUILESS_MAIN(TestMessages)
#include "tst_messages.moc"
