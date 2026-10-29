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
        QTest::newRow("auth result") << Message(
            AuthResult{7, QStringLiteral("alice"), Role::Admin, {1, 2, 33}, {30, 32}});
        const ChannelInfo root{kRootChannelId, 0, QString(), QString(), true, false, 0};
        const ChannelInfo lobby{1000, kRootChannelId, QStringLiteral("Lobby"), QStringLiteral("Hi"), true, true, 10};
        const UserInfo alice{7, QStringLiteral("alice"), Role::User, 1000, 0, QStringLiteral("back at 5")};
        const RoleInfo moderator{4, QStringLiteral("Moderator"), 50, {1, 33}, true};
        QTest::newRow("snapshot") << Message(StateSnapshot{{root, lobby}, {alice}, {moderator}});
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
        QTest::newRow("audio setup") << Message(
            AudioSetup{10350, 0xdeadbeef, QByteArray(32, '\x01'), QByteArray(32, '\x02'), 128000});
        QTest::newRow("audio setup without udp")
            << Message(AudioSetup{0, 1, QByteArray(32, '\x03'), QByteArray(32, '\x04'), 64000});
        QTest::newRow("audio from client")
            << Message(AudioData{AudioStream::Voice, 65535, 0, QByteArray(kMaxAudioPayloadBytes, '\x7f')});
        QTest::newRow("audio from server") << Message(AudioData{AudioStream::SystemAudio, 1, 42, QByteArray("x")});

        // Moderation and administration (docs/protocol/moderation.md).
        const UserInfo muted{8, QStringLiteral("bob"), Role::Moderator, 1000, VoiceMutedFlag | BotFlag, QString()};
        QTest::newRow("user with flags") << Message(UserJoined{muted});
        QTest::newRow("channel updated") << Message(ChannelUpdated{lobby});
        QTest::newRow("edit channel") << Message(
            EditChannel{11, 1000, QStringLiteral("Lobby 2"), QStringLiteral("New"), 5, true, QStringLiteral("pw")});
        QTest::newRow("edit channel removing password")
            << Message(EditChannel{11, 1000, QStringLiteral("Lobby"), QString(), 0, true, QString()});
        QTest::newRow("user updated") << Message(UserUpdated{8, 105, TextMutedFlag | RestrictedFlag, QString()});
        QTest::newRow("user updated with status")
            << Message(UserUpdated{8, 1, 0, QStringLiteral("In a meeting \u2013 back soon")});
        QTest::newRow("set status") << Message(SetStatus{12, QStringLiteral("away")});
        QTest::newRow("clear status") << Message(SetStatus{13, QString()});
        QTest::newRow("roles changed") << Message(
            RolesChanged{{moderator, RoleInfo{105, QStringLiteral("Greeter"), 20, {31}, false}}});
        QTest::newRow("capabilities changed") << Message(CapabilitiesChanged{{1, 10, 11}, {33}});
        SanctionInfo sanction{12,
                              5,
                              1000,
                              QStringLiteral("troll"),
                              QStringLiteral("troll"),
                              QStringLiteral("10.0.0.5"),
                              QStringLiteral("mod"),
                              QStringLiteral("spam"),
                              1790000000000,
                              1790003600000,
                              2,
                              QStringLiteral("boss"),
                              QStringLiteral("apologised")};
        QTest::newRow("sanction notice") << Message(SanctionNotice{sanction});
        QTest::newRow("guest sanction notice") << Message(SanctionNotice{
            SanctionInfo{13, 0, 0, QStringLiteral("Visitor"), {}, {}, QStringLiteral("mod"), {}, 1, 0, 0, {}, {}}});
        QTest::newRow("move user") << Message(MoveUser{12, 8, 1000});
        QTest::newRow("apply sanction to someone online")
            << Message(ApplySanction{13, 8, QString(), 3, 1000, 3600, QStringLiteral("shouting"), false});
        QTest::newRow("apply sanction to an account")
            << Message(ApplySanction{14, 0, QStringLiteral("troll"), 5, 0, 0, QStringLiteral("spam"), true});
        QTest::newRow("revoke sanction") << Message(RevokeSanction{15, 12, QStringLiteral("fair enough")});
        QTest::newRow("list request") << Message(ListRequest{16, ListKind::Appeals, true});
        ListResult sanctions{17, ListKind::Sanctions, {sanction}, {}, {}, {}, {}};
        QTest::newRow("list of sanctions") << Message(sanctions);
        QTest::newRow("list of reports") << Message(
            ListResult{17,
                       ListKind::Reports,
                       {},
                       {ReportInfo{1, QStringLiteral("alice"), QStringLiteral("troll"), QStringLiteral("troll"),
                                   QStringLiteral("rude"), 5, 1, QStringLiteral("mod"), QStringLiteral("done")}},
                       {},
                       {},
                       {}});
        QTest::newRow("list of appeals") << Message(
            ListResult{17,
                       ListKind::Appeals,
                       {},
                       {},
                       {AppealInfo{2, 12, QStringLiteral("troll"), QStringLiteral("sorry"), 6, 3,
                                   QStringLiteral("boss"), QStringLiteral("ok")}},
                       {},
                       {}});
        QTest::newRow("list of accounts")
            << Message(ListResult{17,
                                  ListKind::Accounts,
                                  {},
                                  {},
                                  {},
                                  {AccountInfo{QStringLiteral("alice"), 1, false, false},
                                   AccountInfo{QStringLiteral("greeter"), 105, true, true}},
                                  {}});
        QTest::newRow("list of rules") << Message(
            ListResult{17,
                       ListKind::Rules,
                       {},
                       {},
                       {},
                       {},
                       {RuleInfo{3, 1000, true, QStringLiteral("alice"), 0, 33, true},
                        RuleInfo{4, 0, false, QString(), 1, 20, false}}});
        QTest::newRow("set user role") << Message(SetUserRole{18, QStringLiteral("alice"), 4});
        QTest::newRow("report user") << Message(ReportUser{19, 8, QStringLiteral("rude")});
        QTest::newRow("resolve report") << Message(ResolveReport{20, 1, 2, QStringLiteral("not a problem")});
        QTest::newRow("submit appeal") << Message(SubmitAppeal{21, 12, QStringLiteral("I was joking")});
        QTest::newRow("decide appeal") << Message(DecideAppeal{22, 2, 2, QStringLiteral("shorter"), 600});
        QTest::newRow("read audit") << Message(ReadAudit{23, 500, 100});
        QTest::newRow("audit page") << Message(
            AuditPage{23,
                      {AuditEntryInfo{7, 1790000000000, QStringLiteral("mod"), QStringLiteral("sanction.ban"),
                                      QStringLiteral("troll"), 0, QStringLiteral("spam"), QStringLiteral("sanction=12"),
                                      QByteArray(32, '\x09')}},
                      false,
                      5});
        QTest::newRow("create account") << Message(
            CreateAccount{24, QStringLiteral("newbie"), QStringLiteral("long password"), 1, false});
        QTest::newRow("create bot account")
            << Message(CreateAccount{24, QStringLiteral("greeter"), QStringLiteral("long password"), 1, true});
        QTest::newRow("delete account") << Message(DeleteAccount{25, QStringLiteral("newbie")});
        QTest::newRow("set password") << Message(
            SetAccountPassword{26, QStringLiteral("alice"), QStringLiteral("new one!")});
        QTest::newRow("save role") << Message(SaveRole{27, RoleInfo{0, QStringLiteral("Helper"), 30, {31, 32}, false}});
        QTest::newRow("delete role") << Message(DeleteRole{28, 105});
        QTest::newRow("add rule") << Message(AddRule{29, RuleInfo{0, 1000, false, QString(), 1, 20, false}});
        QTest::newRow("delete rule") << Message(DeleteRule{30, 3});
        const ServerSettingsInfo settings{
            QStringLiteral("My server"),       QStringLiteral("Welcome!"), true, 50, 64000, 7,
            QStringLiteral("mods@example.org")};
        QTest::newRow("read settings") << Message(ServerSettingsRequest{31, std::nullopt});
        QTest::newRow("change settings") << Message(ServerSettingsRequest{31, settings});
        QTest::newRow("settings result") << Message(ServerSettingsResult{31, settings});
        QTest::newRow("get stats") << Message(GetServerStats{32});
        QTest::newRow("stats result") << Message(
            ServerStatsResult{32, ServerStatsInfo{3600, 4, 5, 6, 700, 2100, 3, 1, 2, 1, 40, true}});
        QTest::newRow("transfer ownership") << Message(TransferOwnership{33, QStringLiteral("alice")});
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
        // Any role ID is valid (servers define their own roles), but it must be a number.
        QTest::newRow("negative role") << typeOf(MessageType::UserJoined)
                                       << cbor({{1, 5}, {2, QStringLiteral("x")}, {3, -1}, {4, 1}});
        QTest::newRow("role as text") << typeOf(MessageType::UserJoined)
                                      << cbor({{1, 5}, {2, QStringLiteral("x")}, {3, QStringLiteral("admin")}, {4, 1}});
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
        const auto setup = [](qsizetype keyLength) {
            return QCborMap{{1, 1}, {2, 1}, {3, QByteArray(keyLength, 'k')}, {4, QByteArray(32, 'k')}, {5, 1}};
        };
        QTest::newRow("short audio key") << typeOf(MessageType::AudioSetup) << cbor(setup(31));
        QTest::newRow("long audio key") << typeOf(MessageType::AudioSetup) << cbor(setup(33));
        QTest::newRow("audio key as text")
            << typeOf(MessageType::AudioSetup)
            << cbor({{1, 1}, {2, 1}, {3, QString(32, QLatin1Char('k'))}, {4, QByteArray(32, 'k')}, {5, 1}});
        const auto audio = [](qint64 stream, const QByteArray &payload) {
            return QCborMap{{1, stream}, {2, 1}, {3, payload}};
        };
        QTest::newRow("empty audio") << typeOf(MessageType::AudioData) << cbor(audio(0, QByteArray()));
        QTest::newRow("audio too big") << typeOf(MessageType::AudioData)
                                       << cbor(audio(0, QByteArray(kMaxAudioPayloadBytes + 1, 'a')));
        QTest::newRow("unknown audio stream") << typeOf(MessageType::AudioData) << cbor(audio(3, "a"));
        QTest::newRow("audio sequence too big")
            << typeOf(MessageType::AudioData) << cbor({{1, 0}, {2, 65536}, {3, QByteArray("a")}});
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
        QVERIFY(isServerToClient(MessageType::AudioSetup) && !isClientToServer(MessageType::AudioSetup));
        QVERIFY(isClientToServer(MessageType::AudioData) && isServerToClient(MessageType::AudioData));
    }

    void oversizedMessagesAreNotEncoded()
    {
        StateSnapshot big;
        for (quint32 i = 0; i < 5000; ++i) {
            big.users.append(UserInfo{i + 1, QString(32, QLatin1Char('n')), Role::User, kRootChannelId, 0, QString()});
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
