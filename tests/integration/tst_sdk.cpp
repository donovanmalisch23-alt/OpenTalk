// SPDX-License-Identifier: GPL-3.0-only
// The SDK's channel and moderation calls (SDK 0.5) against a real server: a moderator bot
// creates a channel, moves someone into it, mutes and unmutes them, and a user without
// the rights is refused. (deleteChannel needs an administrator; only its refusal is tested.)

#include "opentalk/protocol/Messages.h"
#include "opentalk/sdk/Client.h"
#include "opentalk/security/PasswordHash.h"
#include "opentalk/security/Tls.h"
#include "opentalk/server/Server.h"
#include "opentalk/server/ServerDatabase.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <memory>
#include <optional>

using namespace opentalk;

namespace {

const QString kDataDir = QStringLiteral(OPENTALK_TEST_DATA_DIR);
constexpr int kWaitMs = 15'000;

} // namespace

class TestSdk : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    std::unique_ptr<server::ServerDatabase> m_db;
    std::unique_ptr<server::Server> m_server;

    std::unique_ptr<sdk::Client> connect(const QString &name)
    {
        auto client = std::make_unique<sdk::Client>();
        client->setApplication(QStringLiteral("tst_sdk"), QStringLiteral("1"));
        QSignalSpy connected(client.get(), &sdk::Client::connected);
        sdk::ServerAddress address;
        address.host = QStringLiteral("127.0.0.1");
        address.port = m_server->serverPort();
        sdk::ServerIdentity identity;
        identity.pinnedFingerprint = m_server->certificateFingerprint();
        sdk::Login login;
        login.mode = sdk::Login::Mode::Account;
        login.name = name;
        login.password = name + QStringLiteral("-password");
        client->connectToServer(address, identity, login);
        if (!connected.wait(kWaitMs)) {
            return nullptr;
        }
        return client;
    }

    static std::optional<sdk::User> find(const sdk::Client &client, const QString &name)
    {
        for (const sdk::User &user : client.users()) {
            if (user.name == name) {
                return user;
            }
        }
        return std::nullopt;
    }

    /// Waits for a request to finish; returns whether it succeeded, or nullopt on time-out.
    static std::optional<bool> waitFor(sdk::Client &client, quint64 requestId, QString *error = nullptr,
                                       quint64 *resultId = nullptr)
    {
        if (requestId == 0) {
            return std::nullopt;
        }
        std::optional<bool> outcome;
        const auto finished = QObject::connect(&client, &sdk::Client::requestFinished, &client,
                                               [&](quint64 id, bool ok, const QString &message) {
                                                   if (id == requestId) {
                                                       outcome = ok;
                                                       if (error != nullptr) {
                                                           *error = message;
                                                       }
                                                   }
                                               });
        const auto result =
            QObject::connect(&client, &sdk::Client::requestResult, &client, [&](quint64 id, quint64 value) {
                if (id == requestId && resultId != nullptr) {
                    *resultId = value;
                }
            });
        static_cast<void>(QTest::qWaitFor([&] { return outcome.has_value(); }, kWaitMs));
        QObject::disconnect(finished);
        QObject::disconnect(result);
        return outcome;
    }

private slots:
    void initTestCase()
    {
        QString error;
        QVERIFY2(sdk::Client::initialize(&error), qPrintable(error));
        QVERIFY(security::initializePasswordHashing());
        const auto tls = security::serverTlsConfiguration(kDataDir + QStringLiteral("/test-server.crt"),
                                                          kDataDir + QStringLiteral("/test-server.key"), &error);
        QVERIFY2(tls.has_value(), qPrintable(error));
        m_db = std::make_unique<server::ServerDatabase>();
        QVERIFY(m_db->open(m_dir.filePath(QStringLiteral("sdk.db"))));
        const QList<QPair<QString, protocol::Role>> accounts = {{QStringLiteral("modbot"), protocol::Role::Moderator},
                                                                {QStringLiteral("alice"), protocol::Role::User}};
        for (const auto &[name, role] : accounts) {
            const auto hash = security::hashPassword(name + QStringLiteral("-password"),
                                                     security::PasswordHashCost::minimumForTests());
            QVERIFY(hash && m_db->createAccount(name, *hash, role));
        }
        QVERIFY(m_db->setBot(QStringLiteral("modbot"), true));
        server::ServerConfig config;
        config.address = QHostAddress::LocalHost;
        config.port = 0;
        config.tls = *tls;
        config.session.serverName = QStringLiteral("SDK test");
        config.database = m_db.get();
        config.passwordCost = security::PasswordHashCost::minimumForTests();
        config.shutdownGraceMs = 1'000;
        m_server = std::make_unique<server::Server>(config);
        QVERIFY2(m_server->start(&error), qPrintable(error));
    }

    void cleanupTestCase()
    {
        QSignalSpy stopped(m_server.get(), &server::Server::stopped);
        m_server->stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, kWaitMs);
        m_server.reset();
        m_db.reset();
    }

    void aModeratorBotCanManageChannelsAndPeople()
    {
        auto bot = connect(QStringLiteral("modbot"));
        auto alice = connect(QStringLiteral("alice"));
        QVERIFY(bot && alice);
        QTRY_VERIFY_WITH_TIMEOUT(find(*bot, QStringLiteral("alice")).has_value(), kWaitMs);
        const quint32 aliceId = find(*bot, QStringLiteral("alice"))->id;

        // A channel, then alice moved into it.
        quint64 channelId = 0;
        QCOMPARE(waitFor(*bot, bot->createChannel(QStringLiteral("Quiet room")), nullptr, &channelId), true);
        QVERIFY(channelId != 0);
        QTRY_VERIFY_WITH_TIMEOUT(find(*bot, QStringLiteral("alice")).has_value(), kWaitMs);
        QCOMPARE(waitFor(*bot, bot->moveUser(aliceId, static_cast<quint32>(channelId))), true);
        QTRY_COMPARE_WITH_TIMEOUT(alice->self().channelId, static_cast<quint32>(channelId), kWaitMs);

        // Muted for a minute, which everyone sees, then lifted.
        QCOMPARE(bot->applySanction(aliceId, sdk::Sanction::Mute, QStringLiteral("  ")), quint64{0}); // no reason
        quint64 sanctionId = 0;
        QCOMPARE(waitFor(*bot, bot->applySanction(aliceId, sdk::Sanction::Mute, QStringLiteral("testing"), 60), nullptr,
                         &sanctionId),
                 true);
        QVERIFY(sanctionId != 0);
        QTRY_VERIFY_WITH_TIMEOUT(find(*bot, QStringLiteral("alice"))->voiceMuted, kWaitMs);
        QVERIFY(find(*bot, QStringLiteral("alice"))->textMuted);
        QTRY_VERIFY_WITH_TIMEOUT(alice->self().voiceMuted, kWaitMs);
        QCOMPARE(waitFor(*bot, bot->liftSanction(sanctionId, QStringLiteral("done"))), true);
        QTRY_VERIFY_WITH_TIMEOUT(!find(*bot, QStringLiteral("alice"))->voiceMuted, kWaitMs);

        // A registered user may do none of this.
        QString error;
        const quint32 botId = bot->self().id;
        QCOMPARE(waitFor(*alice, alice->applySanction(botId, sdk::Sanction::Kick, QStringLiteral("no")), &error),
                 false);
        QVERIFY(!error.isEmpty());
        QCOMPARE(waitFor(*alice, alice->moveUser(botId, static_cast<quint32>(channelId))), false);

        QCOMPARE(waitFor(*alice, alice->deleteChannel(static_cast<quint32>(channelId))), false);

        // The temporary channel goes away when alice, the last one in it, leaves.
        QCOMPARE(waitFor(*alice, alice->joinChannel(protocol::kRootChannelId)), true);
        QTRY_VERIFY_WITH_TIMEOUT(std::none_of(bot->channels().cbegin(), bot->channels().cend(),
                                              [channelId](const sdk::Channel &c) { return c.id == channelId; }),
                                 kWaitMs);
    }
};

QTEST_GUILESS_MAIN(TestSdk)
#include "tst_sdk.moc"
