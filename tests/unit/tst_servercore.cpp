// SPDX-License-Identifier: GPL-3.0-only
// ServerCore rules tested directly: no sockets, a real SQLite database in a temporary
// folder, real (but minimum-cost) Argon2id hashing, and a fake clock.

#include "opentalk/security/PasswordHash.h"
#include "opentalk/server/PasswordWorker.h"
#include "opentalk/server/ServerCore.h"
#include "opentalk/server/ServerDatabase.h"

#include <QTemporaryDir>
#include <QTest>

using namespace opentalk;
using namespace opentalk::protocol;
using namespace opentalk::server;

namespace {

constexpr qint64 kWallClock = 1'790'000'000'000;

struct Harness {
    QTemporaryDir dir;
    ServerDatabase db;
    PasswordWorker worker{security::PasswordHashCost::minimumForTests()};
    qint64 now = 0;
    std::unique_ptr<ServerCore> core;
    QHash<quint32, QList<Message>> sent;
    QHash<quint32, std::pair<ErrorCode, QString>> closed;

    explicit Harness(ServerCore::Config config = guestsAllowed())
    {
        QVERIFY(db.open(dir.filePath(QStringLiteral("core.db"))));
        addAccount(QStringLiteral("admin"), Role::Admin);
        addAccount(QStringLiteral("alice"), Role::User);
        addAccount(QStringLiteral("bob"), Role::User);
        start(config);
    }

    static ServerCore::Config guestsAllowed()
    {
        ServerCore::Config c;
        c.allowGuests = true;
        return c;
    }

    void addAccount(const QString &name, Role role)
    {
        const auto hash =
            security::hashPassword(name + QStringLiteral("-pw"), security::PasswordHashCost::minimumForTests());
        QVERIFY(hash && db.createAccount(name, *hash, role));
    }

    void start(ServerCore::Config config)
    {
        core = std::make_unique<ServerCore>(config, &db, worker, [this] { return now; }, [] { return kWallClock; });
        QObject::connect(core.get(), &ServerCore::sendTo, [this](quint32 id, const Message &m) { sent[id].append(m); });
        QObject::connect(core.get(), &ServerCore::closeSession, [this](quint32 id, ErrorCode code, const QString &d) {
            closed.insert(id, {code, d});
            core->sessionEnded(id); // what the network layer does after closing
        });
        QVERIFY(core->loadChannels());
    }

    void send(quint32 id, const Message &m) { core->handleMessage(id, m); }

    /// Starts a session and logs in. Returns true once AuthResult arrived.
    bool login(quint32 id, const QString &name, bool guest = false, const QString &password = {},
               const QHostAddress &address = QHostAddress(QStringLiteral("192.0.2.1")))
    {
        core->sessionStarted(id, address);
        const QString pw = password.isNull() ? name + QStringLiteral("-pw") : password;
        send(id, Authenticate{guest ? Authenticate::Mode::Guest : Authenticate::Mode::Account, name, pw});
        return QTest::qWaitFor([&] { return has<AuthResult>(id) || closed.contains(id); }, 5000) && has<AuthResult>(id);
    }

    template <typename T> QList<T> all(quint32 id) const
    {
        QList<T> result;
        for (const Message &m : sent.value(id)) {
            if (const auto *t = std::get_if<T>(&m)) {
                result.append(*t);
            }
        }
        return result;
    }
    template <typename T> bool has(quint32 id) const { return !all<T>(id).isEmpty(); }
    template <typename T> std::optional<T> last(quint32 id) const
    {
        const QList<T> list = all<T>(id);
        return list.isEmpty() ? std::nullopt : std::optional<T>(list.last());
    }

    /// Waits for the answer to `requestId`: nullopt on success, else the error code.
    std::optional<ErrorCode> answer(quint32 id, quint64 requestId)
    {
        std::optional<ErrorCode> result;
        bool done = false;
        [[maybe_unused]] const bool finished = QTest::qWaitFor(
            [&] {
                for (const Message &m : sent.value(id)) {
                    if (const auto *d = std::get_if<RequestDone>(&m); d && d->requestId == requestId) {
                        done = true;
                    } else if (const auto *e = std::get_if<Error>(&m); e && e->requestId == requestId) {
                        done = true;
                        result = e->code;
                    }
                }
                return done;
            },
            5000);
        if (!done) {
            return ErrorCode::Timeout;
        }
        return result;
    }

    quint32 createdChannel(quint32 id, quint64 requestId) const
    {
        for (const Message &m : sent.value(id)) {
            if (const auto *d = std::get_if<RequestDone>(&m); d && d->requestId == requestId) {
                return d->resultId.value_or(0);
            }
        }
        return 0;
    }
};

#define EXPECT_OK(expr) QCOMPARE((expr), std::optional<ErrorCode>())
#define EXPECT_ERROR(expr, code) QVERIFY((expr) == std::optional<ErrorCode>(code))

} // namespace

class TestServerCore : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QVERIFY(security::initializePasswordHashing()); }

    // ----------------------------------------------------------------------- login

    void guestLoginAndPresence()
    {
        Harness h;
        QVERIFY(h.login(1, QStringLiteral("Nova"), true));
        const AuthResult result = *h.last<AuthResult>(1);
        QCOMPARE(result.userId, quint32(1));
        QVERIFY(result.role == Role::Guest);
        const StateSnapshot snapshot = *h.last<StateSnapshot>(1);
        QCOMPARE(snapshot.channels.first().id, kRootChannelId);
        QCOMPARE(snapshot.users.size(), 1);

        QVERIFY(h.login(2, QStringLiteral("alice")));
        QCOMPARE(h.last<UserJoined>(1)->user.name, QStringLiteral("alice"));
        QCOMPARE(h.last<StateSnapshot>(2)->users.size(), 2);
        QVERIFY(!h.has<UserJoined>(2)); // nobody is told about themselves

        h.core->sessionEnded(2);
        QCOMPARE(h.last<UserLeft>(1)->userId, quint32(2));
        QCOMPARE(h.core->onlineUserCount(), 1);
    }

    void guestsCanBeDisabled()
    {
        ServerCore::Config config;
        config.allowGuests = false;
        Harness h(config);
        QVERIFY(!h.login(1, QStringLiteral("Nova"), true));
        QVERIFY(h.closed.value(1).first == ErrorCode::AuthenticationFailed);
    }

    void guestNamesCannotImpersonate()
    {
        Harness h;
        QVERIFY(!h.login(1, QStringLiteral("ADMIN"), true)); // an account name, any case
        QVERIFY(h.closed.value(1).first == ErrorCode::NameInUse);
        QVERIFY(h.login(2, QStringLiteral("Nova"), true));
        QVERIFY(!h.login(3, QStringLiteral("nova"), true)); // already online
        QVERIFY(h.closed.value(3).first == ErrorCode::NameInUse);
    }

    void accountLogin()
    {
        Harness h;
        QVERIFY(h.login(1, QStringLiteral("ADMIN"), false, QStringLiteral("admin-pw"))); // usernames ignore case
        QVERIFY(h.last<AuthResult>(1)->role == Role::Admin);
        QCOMPARE(h.last<AuthResult>(1)->displayName, QStringLiteral("admin"));
    }

    void failedLoginsLookTheSame()
    {
        Harness h;
        QVERIFY(!h.login(1, QStringLiteral("alice"), false, QStringLiteral("wrong")));
        QVERIFY(!h.login(2, QStringLiteral("nobody"), false, QStringLiteral("whatever")));
        QVERIFY(h.closed.value(1).first == ErrorCode::AuthenticationFailed);
        QVERIFY(h.closed.value(1) == h.closed.value(2)); // no hint whether the account exists
    }

    void repeatedFailuresAreThrottledPerAddress()
    {
        Harness h;
        const QHostAddress attacker(QStringLiteral("198.51.100.7"));
        for (quint32 i = 1; i <= 10; ++i) {
            QVERIFY(!h.login(i, QStringLiteral("alice"), false, QStringLiteral("guess"), attacker));
        }
        // Even the right password is refused from that address for now...
        QVERIFY(!h.login(11, QStringLiteral("alice"), false, QString(), attacker));
        QVERIFY(h.closed.value(11).second.contains(QStringLiteral("too many")));
        // ...but other addresses are unaffected...
        QVERIFY(h.login(12, QStringLiteral("alice")));
        // ...and the block expires.
        h.now += 11 * 60 * 1000;
        QVERIFY(h.login(13, QStringLiteral("alice"), false, QString(), attacker));
    }

    void onlyLoginIsAllowedFirst()
    {
        Harness h;
        h.core->sessionStarted(1, QHostAddress::LocalHost);
        h.send(1, JoinChannel{1, kRootChannelId, {}});
        QVERIFY(h.closed.value(1).first == ErrorCode::ProtocolViolation);
    }

    void disconnectDuringLoginIsHarmless()
    {
        Harness h;
        h.core->sessionStarted(1, QHostAddress::LocalHost);
        h.send(1, Authenticate{Authenticate::Mode::Account, QStringLiteral("alice"), QStringLiteral("alice-pw")});
        h.core->sessionEnded(1);
        QTest::qWait(200); // let the password check finish
        QVERIFY(!h.has<AuthResult>(1));
        QCOMPARE(h.core->onlineUserCount(), 0);
    }

    // -------------------------------------------------------------------- channels

    void guestsCannotCreateChannels()
    {
        Harness h;
        QVERIFY(h.login(1, QStringLiteral("Nova"), true));
        h.send(1, CreateChannel{7, kRootChannelId, QStringLiteral("Mine"), {}, false, {}, 0});
        EXPECT_ERROR(h.answer(1, 7), ErrorCode::NotAuthorized);
    }

    void temporaryChannelLifecycle()
    {
        Harness h;
        QVERIFY(h.login(1, QStringLiteral("alice")));
        QVERIFY(h.login(2, QStringLiteral("bob")));
        h.send(1, CreateChannel{1, kRootChannelId, QStringLiteral("Games"), QStringLiteral("Fun"), false, {}, 0});
        EXPECT_OK(h.answer(1, 1));
        const quint32 id = h.createdChannel(1, 1);
        QVERIFY(id >= 0x80000000u);
        QCOMPARE(h.last<ChannelAdded>(2)->channel.name, QStringLiteral("Games"));
        QCOMPARE(h.last<UserMoved>(2)->channelId, id); // the creator moved in

        h.send(2, JoinChannel{2, id, {}});
        EXPECT_OK(h.answer(2, 2));
        h.send(1, JoinChannel{3, kRootChannelId, {}});
        EXPECT_OK(h.answer(1, 3));
        QVERIFY(!h.has<ChannelRemoved>(1)); // bob is still inside
        h.core->sessionEnded(2);            // last one out
        QCOMPARE(h.last<ChannelRemoved>(1)->channelId, id);
    }

    void temporaryChannelRules()
    {
        Harness h;
        QVERIFY(h.login(1, QStringLiteral("alice")));
        h.send(1, CreateChannel{1, kRootChannelId, QStringLiteral("A"), {}, false, {}, 0});
        EXPECT_OK(h.answer(1, 1));
        const quint32 a = h.createdChannel(1, 1);
        h.send(1, CreateChannel{2, a, QStringLiteral("Child"), {}, false, {}, 0});
        EXPECT_ERROR(h.answer(1, 2), ErrorCode::InvalidRequest); // no children under temporary
        h.send(1, CreateChannel{3, kRootChannelId, QStringLiteral("a"), {}, false, {}, 0});
        EXPECT_ERROR(h.answer(1, 3), ErrorCode::NameInUse); // sibling names ignore case
        h.send(1, CreateChannel{4, 424242, QStringLiteral("X"), {}, false, {}, 0});
        EXPECT_ERROR(h.answer(1, 4), ErrorCode::NotFound);
        h.send(1, CreateChannel{5, kRootChannelId, QStringLiteral("Big"), {}, false, {}, 5000});
        EXPECT_ERROR(h.answer(1, 5), ErrorCode::InvalidRequest);
        h.send(1, CreateChannel{6, kRootChannelId, QStringLiteral("B"), {}, false, {}, 0});
        EXPECT_OK(h.answer(1, 6));
        h.send(1, CreateChannel{7, kRootChannelId, QStringLiteral("C"), {}, false, {}, 0});
        EXPECT_OK(h.answer(1, 7));
        // alice's earlier channels vanished as she moved on, so she owns only "C" now.
        QCOMPARE(h.core->channels().size(), 2);
    }

    void persistentChannelsNeedAdminAndSurviveRestart()
    {
        Harness h;
        QVERIFY(h.login(1, QStringLiteral("alice")));
        h.send(1, CreateChannel{1, kRootChannelId, QStringLiteral("Lobby"), {}, true, {}, 0});
        EXPECT_ERROR(h.answer(1, 1), ErrorCode::NotAuthorized);

        QVERIFY(h.login(2, QStringLiteral("admin")));
        h.send(2, CreateChannel{2, kRootChannelId, QStringLiteral("Lobby"), QStringLiteral("Welcome"), true, {}, 0});
        EXPECT_OK(h.answer(2, 2));
        const quint32 lobby = h.createdChannel(2, 2);
        QCOMPARE(lobby, quint32(1000));
        QVERIFY(!h.has<UserMoved>(2)); // creating a persistent channel does not move you
        h.send(2, CreateChannel{3, lobby, QStringLiteral("Corner"), {}, true, {}, 0});
        EXPECT_OK(h.answer(2, 3));

        h.start(Harness::guestsAllowed()); // a restarted server
        QVERIFY(h.login(3, QStringLiteral("bob")));
        const StateSnapshot snapshot = *h.last<StateSnapshot>(3);
        QCOMPARE(snapshot.channels.size(), 3);
        QCOMPARE(snapshot.channels[1].name, QStringLiteral("Lobby"));
        QCOMPARE(snapshot.channels[2].parentId, lobby);
    }

    void channelPasswords()
    {
        Harness h;
        QVERIFY(h.login(1, QStringLiteral("admin")));
        h.send(1,
               CreateChannel{1, kRootChannelId, QStringLiteral("Secret"), {}, true, QStringLiteral("open sesame"), 0});
        EXPECT_OK(h.answer(1, 1));
        const quint32 id = h.createdChannel(1, 1);
        QVERIFY(h.last<ChannelAdded>(1)->channel.hasPassword);

        QVERIFY(h.login(2, QStringLiteral("alice")));
        h.send(2, JoinChannel{1, id, {}});
        EXPECT_ERROR(h.answer(2, 1), ErrorCode::WrongChannelPassword);
        h.send(2, JoinChannel{2, id, QStringLiteral("open says me")});
        EXPECT_ERROR(h.answer(2, 2), ErrorCode::WrongChannelPassword);
        h.send(2, JoinChannel{3, id, QStringLiteral("open sesame")});
        EXPECT_OK(h.answer(2, 3));

        h.send(1, JoinChannel{2, id, {}}); // administrators are not asked
        EXPECT_OK(h.answer(1, 2));
    }

    void fullChannelsRefuseUsersButNotAdmins()
    {
        Harness h;
        QVERIFY(h.login(1, QStringLiteral("admin")));
        h.send(1, CreateChannel{1, kRootChannelId, QStringLiteral("Duo"), {}, true, {}, 1});
        EXPECT_OK(h.answer(1, 1));
        const quint32 id = h.createdChannel(1, 1);
        QVERIFY(h.login(2, QStringLiteral("alice")));
        QVERIFY(h.login(3, QStringLiteral("bob")));
        h.send(2, JoinChannel{1, id, {}});
        EXPECT_OK(h.answer(2, 1));
        h.send(3, JoinChannel{1, id, {}});
        EXPECT_ERROR(h.answer(3, 1), ErrorCode::ChannelFull);
        h.send(1, JoinChannel{9, id, {}});
        EXPECT_OK(h.answer(1, 9));
    }

    void deletingChannels()
    {
        Harness h;
        QVERIFY(h.login(1, QStringLiteral("admin")));
        QVERIFY(h.login(2, QStringLiteral("alice")));
        h.send(1, CreateChannel{1, kRootChannelId, QStringLiteral("Room"), {}, true, {}, 0});
        EXPECT_OK(h.answer(1, 1));
        const quint32 room = h.createdChannel(1, 1);
        h.send(1, CreateChannel{2, room, QStringLiteral("Sub"), {}, true, {}, 0});
        EXPECT_OK(h.answer(1, 2));
        const quint32 sub = h.createdChannel(1, 2);
        h.send(2, JoinChannel{1, sub, {}});
        EXPECT_OK(h.answer(2, 1));

        h.send(2, DeleteChannel{2, sub});
        EXPECT_ERROR(h.answer(2, 2), ErrorCode::NotAuthorized);
        h.send(1, DeleteChannel{3, kRootChannelId});
        EXPECT_ERROR(h.answer(1, 3), ErrorCode::InvalidRequest);
        h.send(1, DeleteChannel{4, room});
        EXPECT_ERROR(h.answer(1, 4), ErrorCode::InvalidRequest); // has a sub-channel
        h.send(1, DeleteChannel{5, sub});
        EXPECT_OK(h.answer(1, 5));
        QCOMPARE(h.last<UserMoved>(2)->channelId, kRootChannelId); // alice was moved out first
        QCOMPARE(h.last<ChannelRemoved>(2)->channelId, sub);
        QCOMPARE(h.db.channels().size(), 1);
    }

    // ------------------------------------------------------------------------ text

    void channelTextReachesOnlyThatChannel()
    {
        Harness h;
        QVERIFY(h.login(1, QStringLiteral("alice")));
        QVERIFY(h.login(2, QStringLiteral("bob")));
        QVERIFY(h.login(3, QStringLiteral("Nova"), true));
        h.send(1, CreateChannel{1, kRootChannelId, QStringLiteral("Pair"), {}, false, {}, 0});
        EXPECT_OK(h.answer(1, 1));
        const quint32 pair = h.createdChannel(1, 1);
        h.send(2, JoinChannel{1, pair, {}});
        EXPECT_OK(h.answer(2, 1));

        h.send(1, SendText{2, TextTarget::Channel, pair, QStringLiteral("hello pair")});
        EXPECT_OK(h.answer(1, 2));
        QCOMPARE(h.last<TextMessage>(2)->text, QStringLiteral("hello pair"));
        QCOMPARE(h.last<TextMessage>(2)->fromUserId, quint32(1));
        QCOMPARE(h.last<TextMessage>(2)->timestampMs, kWallClock);
        QVERIFY(h.has<TextMessage>(1));  // echo to the sender
        QVERIFY(!h.has<TextMessage>(3)); // Nova is elsewhere

        h.send(3, SendText{1, TextTarget::Channel, pair, QStringLiteral("let me in")});
        EXPECT_ERROR(h.answer(3, 1), ErrorCode::NotAuthorized);
    }

    void directMessages()
    {
        Harness h;
        QVERIFY(h.login(1, QStringLiteral("alice")));
        QVERIFY(h.login(2, QStringLiteral("bob")));
        QVERIFY(h.login(3, QStringLiteral("Nova"), true));
        h.send(1, SendText{1, TextTarget::User, 2, QStringLiteral("psst")});
        EXPECT_OK(h.answer(1, 1));
        QVERIFY(h.last<TextMessage>(2)->target == TextTarget::User);
        QVERIFY(h.has<TextMessage>(1));
        QVERIFY(!h.has<TextMessage>(3));
        h.send(1, SendText{2, TextTarget::User, 99, QStringLiteral("anyone?")});
        EXPECT_ERROR(h.answer(1, 2), ErrorCode::NotFound);
        h.send(1, SendText{3, TextTarget::User, 1, QStringLiteral("me")});
        EXPECT_ERROR(h.answer(1, 3), ErrorCode::NotFound);
    }

    void serverWideMessagesAreForAdmins()
    {
        Harness h;
        QVERIFY(h.login(1, QStringLiteral("admin")));
        QVERIFY(h.login(2, QStringLiteral("alice")));
        h.send(2, SendText{1, TextTarget::Server, 0, QStringLiteral("everyone!")});
        EXPECT_ERROR(h.answer(2, 1), ErrorCode::NotAuthorized);
        h.send(1, SendText{2, TextTarget::Server, 0, QStringLiteral("Maintenance at noon")});
        EXPECT_OK(h.answer(1, 2));
        QVERIFY(h.last<TextMessage>(2)->target == TextTarget::Server);
    }

    void chatFloodingIsRateLimited()
    {
        Harness h;
        QVERIFY(h.login(1, QStringLiteral("alice")));
        for (quint64 i = 1; i <= 5; ++i) {
            h.send(1, SendText{i, TextTarget::Channel, kRootChannelId, QStringLiteral("spam")});
            EXPECT_OK(h.answer(1, i));
        }
        h.send(1, SendText{6, TextTarget::Channel, kRootChannelId, QStringLiteral("spam")});
        EXPECT_ERROR(h.answer(1, 6), ErrorCode::RateLimited);
        h.now += 1000;
        h.send(1, SendText{7, TextTarget::Channel, kRootChannelId, QStringLiteral("calm")});
        EXPECT_OK(h.answer(1, 7));
    }
};

QTEST_GUILESS_MAIN(TestServerCore)
#include "tst_servercore.moc"
