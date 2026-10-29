// SPDX-License-Identifier: GPL-3.0-only
// End-to-end Phase 3 tests: a real server with a database, several real clients over
// TLS, accounts, presence, channels, chat and reconnection.

#include "opentalk/client/Connection.h"
#include "opentalk/security/PasswordHash.h"
#include "opentalk/security/Tls.h"
#include "opentalk/server/Server.h"
#include "opentalk/server/ServerDatabase.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace opentalk;
using namespace opentalk::protocol;

namespace {

const QString kDataDir = QStringLiteral(OPENTALK_TEST_DATA_DIR);
constexpr int kWaitMs = 10'000;

/// A client::Connection plus a record of what it saw.
struct TestClient {
    client::Connection connection;
    QList<TextMessage> texts;
    QList<client::DisconnectInfo> disconnects;
    QHash<quint64, std::optional<Error>> results;
    QHash<quint64, std::optional<quint32>> resultIds;
    int connectedCount = 0;

    TestClient()
    {
        QObject::connect(&connection, &client::Connection::textReceived,
                         [this](const TextMessage &m) { texts.append(m); });
        QObject::connect(&connection, &client::Connection::disconnected,
                         [this](const client::DisconnectInfo &i) { disconnects.append(i); });
        QObject::connect(&connection, &client::Connection::connected, [this] { ++connectedCount; });
        QObject::connect(&connection, &client::Connection::requestFinished,
                         [this](quint64 id, const std::optional<Error> &e, std::optional<quint32> r) {
                             results.insert(id, e);
                             resultIds.insert(id, r);
                         });
    }

    const client::ClientState &state() const { return connection.serverState(); }

    /// Waits for a request's outcome. Returns nullopt on success, else the error code.
    std::optional<ErrorCode> wait(quint64 id)
    {
        if (id == 0 || !QTest::qWaitFor([&] { return results.contains(id); }, kWaitMs)) {
            return ErrorCode::Timeout;
        }
        const std::optional<Error> e = results.value(id);
        return e ? std::optional<ErrorCode>(e->code) : std::nullopt;
    }
};

#define EXPECT_OK(expr) QCOMPARE((expr), std::optional<ErrorCode>())
#define EXPECT_ERROR(expr, code) QVERIFY((expr) == std::optional<ErrorCode>(code))

} // namespace

class TestChatSession : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QSslConfiguration m_tls;
    std::unique_ptr<server::ServerDatabase> m_db;
    std::unique_ptr<server::Server> m_server;
    quint16 m_port = 0;

    void startServer(bool allowGuests = true, quint16 port = 0)
    {
        server::ServerConfig config;
        config.address = QHostAddress::LocalHost;
        config.port = port;
        config.tls = m_tls;
        config.session.serverName = QStringLiteral("Chat Test");
        config.allowGuests = allowGuests;
        config.database = m_db.get();
        config.passwordCost = security::PasswordHashCost::minimumForTests();
        config.shutdownGraceMs = 1'000;
        m_server = std::make_unique<server::Server>(config);
        QString error;
        QVERIFY2(m_server->start(&error), qPrintable(error));
        m_port = m_server->serverPort();
    }

    void stopServer()
    {
        if (m_server && m_server->isListening()) {
            QSignalSpy stopped(m_server.get(), &server::Server::stopped);
            m_server->stop();
            QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, kWaitMs);
        }
        m_server.reset();
    }

    client::ConnectOptions options(const QString &name, bool guest = false, const QString &password = {}) const
    {
        client::ConnectOptions o;
        o.host = QStringLiteral("127.0.0.1");
        o.port = m_port;
        o.trust = security::ServerTrust::pinned(m_server->certificateFingerprint());
        o.credentials.mode = guest ? client::Credentials::Mode::Guest : client::Credentials::Mode::Account;
        o.credentials.name = name;
        o.credentials.password = password.isNull() ? name + QStringLiteral("-password") : password;
        o.autoReconnect = false;
        return o;
    }

    bool login(TestClient &c, const QString &name, bool guest = false)
    {
        c.connection.connectToServer(options(name, guest));
        return QTest::qWaitFor([&] { return c.connectedCount > 0 || !c.disconnects.isEmpty(); }, kWaitMs) &&
               c.connectedCount > 0;
    }

private slots:
    void initTestCase()
    {
        qRegisterMetaType<client::DisconnectInfo>();
        QString error;
        QVERIFY2(security::initializeTls(&error), qPrintable(error));
        QVERIFY(security::initializePasswordHashing());
        const auto tls = security::serverTlsConfiguration(kDataDir + QStringLiteral("/test-server.crt"),
                                                          kDataDir + QStringLiteral("/test-server.key"), &error);
        QVERIFY2(tls.has_value(), qPrintable(error));
        m_tls = *tls;
    }

    void init()
    {
        m_db = std::make_unique<server::ServerDatabase>();
        QVERIFY(m_db->open(
            m_dir.filePath(QStringLiteral("chat-%1.db").arg(QString::fromLatin1(QTest::currentTestFunction())))));
        for (const auto &[name, role] :
             {std::pair{QStringLiteral("admin"), Role::Admin}, std::pair{QStringLiteral("alice"), Role::User},
              std::pair{QStringLiteral("bob"), Role::User}}) {
            const auto hash = security::hashPassword(name + QStringLiteral("-password"),
                                                     security::PasswordHashCost::minimumForTests());
            QVERIFY(hash && m_db->createAccount(name, *hash, role));
        }
        startServer();
    }

    void cleanup()
    {
        stopServer();
        m_db.reset();
    }

    void loginAndPresence()
    {
        TestClient alice;
        TestClient bob;
        QVERIFY(login(alice, QStringLiteral("alice")));
        QCOMPARE(alice.state().selfName(), QStringLiteral("alice"));
        QVERIFY(alice.state().selfRole() == Role::User);
        QCOMPARE(alice.state().currentChannelId(), kRootChannelId);
        QVERIFY(login(bob, QStringLiteral("bob")));
        QTRY_COMPARE_WITH_TIMEOUT(alice.state().users().size(), 2, kWaitMs);
        QCOMPARE(bob.state().users().size(), 2);
        QCOMPARE(m_server->onlineUserCount(), 2);

        bob.connection.disconnectFromServer();
        QTRY_COMPARE_WITH_TIMEOUT(alice.state().users().size(), 1, kWaitMs);
    }

    void wrongPasswordAndUnknownAccountLookAlike()
    {
        TestClient a;
        a.connection.connectToServer(options(QStringLiteral("alice"), false, QStringLiteral("nope")));
        QTRY_COMPARE_WITH_TIMEOUT(a.disconnects.size(), 1, kWaitMs);
        TestClient b;
        b.connection.connectToServer(options(QStringLiteral("mallory"), false, QStringLiteral("nope")));
        QTRY_COMPARE_WITH_TIMEOUT(b.disconnects.size(), 1, kWaitMs);
        QVERIFY(a.disconnects.first().reason == client::DisconnectInfo::Reason::LoginFailed);
        QVERIFY(a.disconnects.first().protocolCode == ErrorCode::AuthenticationFailed);
        QCOMPARE(a.disconnects.first().detail, b.disconnects.first().detail);
    }

    void guestRules()
    {
        TestClient guest;
        QVERIFY(login(guest, QStringLiteral("Nova"), true));
        QVERIFY(guest.state().selfRole() == Role::Guest);
        TestClient impostor;
        impostor.connection.connectToServer(options(QStringLiteral("Admin"), true));
        QTRY_COMPARE_WITH_TIMEOUT(impostor.disconnects.size(), 1, kWaitMs);
        QVERIFY(impostor.disconnects.first().protocolCode == ErrorCode::NameInUse);

        stopServer();
        startServer(false);
        TestClient refused;
        refused.connection.connectToServer(options(QStringLiteral("Nova"), true));
        QTRY_COMPARE_WITH_TIMEOUT(refused.disconnects.size(), 1, kWaitMs);
        QVERIFY(refused.disconnects.first().reason == client::DisconnectInfo::Reason::LoginFailed);
    }

    void channelsAndChat()
    {
        TestClient alice;
        TestClient bob;
        TestClient carol;
        QVERIFY(login(alice, QStringLiteral("alice")));
        QVERIFY(login(bob, QStringLiteral("bob")));
        QVERIFY(login(carol, QStringLiteral("Carol"), true));

        CreateChannel create;
        create.name = QStringLiteral("Book club");
        create.topic = QStringLiteral("This month: Dune");
        const quint64 req = alice.connection.createChannel(create);
        EXPECT_OK(alice.wait(req));
        const quint32 club = alice.resultIds.value(req).value_or(0);
        QVERIFY(club != 0);
        QCOMPARE(alice.state().currentChannelId(), club);
        QTRY_VERIFY_WITH_TIMEOUT(bob.state().channel(club).has_value(), kWaitMs);
        QCOMPARE(bob.state().channel(club)->topic, QStringLiteral("This month: Dune"));
        QCOMPARE(bob.state().channelPath(club, QStringLiteral("Server")), QStringLiteral("Server / Book club"));

        EXPECT_OK(bob.wait(bob.connection.joinChannel(club)));
        QTRY_COMPARE_WITH_TIMEOUT(alice.state().usersInChannel(club).size(), 2, kWaitMs);

        EXPECT_OK(alice.wait(alice.connection.sendText(TextTarget::Channel, club, QStringLiteral("Chapter 1?"))));
        QTRY_COMPARE_WITH_TIMEOUT(bob.texts.size(), 1, kWaitMs);
        QCOMPARE(bob.texts.first().text, QStringLiteral("Chapter 1?"));
        QCOMPARE(bob.texts.first().fromUserId, alice.state().selfId());
        QVERIFY(bob.texts.first().timestampMs > 0);

        // Carol, in the root, sees neither the message nor can she post into the club.
        QTest::qWait(100);
        QVERIFY(carol.texts.isEmpty());
        EXPECT_ERROR(carol.wait(carol.connection.sendText(TextTarget::Channel, club, QStringLiteral("hi"))),
                     ErrorCode::NotAuthorized);

        // A direct message reaches only its recipient (and is echoed to the sender).
        EXPECT_OK(
            carol.wait(carol.connection.sendText(TextTarget::User, bob.state().selfId(), QStringLiteral("psst, Bob"))));
        QTRY_VERIFY_WITH_TIMEOUT(bob.texts.size() == 2, kWaitMs);
        QVERIFY(bob.texts.last().target == TextTarget::User);
        QCOMPARE(alice.texts.size(), 1);

        // Guests cannot create channels; ordinary users cannot make them persistent.
        EXPECT_ERROR(carol.wait(carol.connection.createChannel(
                         CreateChannel{0, kRootChannelId, QStringLiteral("x"), {}, false, {}, 0})),
                     ErrorCode::NotAuthorized);
        EXPECT_ERROR(bob.wait(bob.connection.createChannel(
                         CreateChannel{0, kRootChannelId, QStringLiteral("y"), {}, true, {}, 0})),
                     ErrorCode::NotAuthorized);

        // When the last person leaves, the temporary channel disappears for everyone.
        alice.connection.disconnectFromServer();
        EXPECT_OK(bob.wait(bob.connection.joinChannel(kRootChannelId)));
        QTRY_VERIFY_WITH_TIMEOUT(!carol.state().channel(club).has_value(), kWaitMs);
    }

    void adminManagesPersistentChannels()
    {
        TestClient admin;
        TestClient alice;
        QVERIFY(login(admin, QStringLiteral("admin")));
        QVERIFY(login(alice, QStringLiteral("alice")));
        CreateChannel create{0, kRootChannelId, QStringLiteral("Staff"), {}, true, QStringLiteral("letmein"), 0};
        const quint64 req = admin.connection.createChannel(create);
        EXPECT_OK(admin.wait(req));
        const quint32 staff = admin.resultIds.value(req).value_or(0);
        QTRY_VERIFY_WITH_TIMEOUT(alice.state().channel(staff).has_value(), kWaitMs);
        QVERIFY(alice.state().channel(staff)->hasPassword);
        EXPECT_ERROR(alice.wait(alice.connection.joinChannel(staff, QStringLiteral("guess"))),
                     ErrorCode::WrongChannelPassword);
        EXPECT_OK(alice.wait(alice.connection.joinChannel(staff, QStringLiteral("letmein"))));

        EXPECT_ERROR(alice.wait(alice.connection.deleteChannel(staff)), ErrorCode::NotAuthorized);
        EXPECT_OK(admin.wait(admin.connection.deleteChannel(staff)));
        QTRY_COMPARE_WITH_TIMEOUT(alice.state().currentChannelId(), kRootChannelId, kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(!alice.state().channel(staff).has_value(), kWaitMs);
    }

    void reconnectsAfterServerRestart()
    {
        TestClient alice;
        client::ConnectOptions o = options(QStringLiteral("alice"));
        o.autoReconnect = true;
        QSignalSpy lost(&alice.connection, &client::Connection::connectionLost);
        alice.connection.connectToServer(o);
        QTRY_COMPARE_WITH_TIMEOUT(alice.connectedCount, 1, kWaitMs);

        const quint16 port = m_port;
        stopServer();
        QTRY_VERIFY_WITH_TIMEOUT(lost.count() >= 1, kWaitMs);
        QVERIFY(alice.connection.state() == client::ConnectionState::Reconnecting ||
                alice.connection.state() == client::ConnectionState::Connecting);
        QVERIFY(alice.disconnects.isEmpty()); // not given up

        startServer(true, port);
        QTRY_COMPARE_WITH_TIMEOUT(alice.connectedCount, 2, 20'000);
        QCOMPARE(alice.state().selfName(), QStringLiteral("alice"));

        alice.connection.disconnectFromServer();
        QTRY_COMPARE_WITH_TIMEOUT(alice.disconnects.size(), 1, kWaitMs);
        QVERIFY(alice.disconnects.first().reason == client::DisconnectInfo::Reason::UserRequested);
    }

    void userCanStopReconnecting()
    {
        TestClient alice;
        client::ConnectOptions o = options(QStringLiteral("alice"));
        o.autoReconnect = true;
        alice.connection.connectToServer(o);
        QTRY_COMPARE_WITH_TIMEOUT(alice.connectedCount, 1, kWaitMs);
        stopServer();
        QTRY_VERIFY_WITH_TIMEOUT(alice.connection.state() == client::ConnectionState::Reconnecting, kWaitMs);
        alice.connection.disconnectFromServer();
        QCOMPARE(alice.disconnects.size(), 1);
        QVERIFY(alice.connection.state() == client::ConnectionState::Disconnected);
        QTest::qWait(1500); // no further attempts
        QVERIFY(alice.connection.state() == client::ConnectionState::Disconnected);
    }
};

QTEST_GUILESS_MAIN(TestChatSession)
#include "tst_chat_session.moc"
