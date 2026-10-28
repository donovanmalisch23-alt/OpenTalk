// SPDX-License-Identifier: GPL-3.0-only
// The example bot (examples/greeter-bot) run as a real program against a real server,
// with a person connected through the client library: it is shown as a bot, greets,
// answers commands and talks (SDK 0.3 audio).

#include "opentalk/client/Connection.h"
#include "opentalk/security/PasswordHash.h"
#include "opentalk/security/Tls.h"
#include "opentalk/server/Server.h"
#include "opentalk/server/ServerDatabase.h"

#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace opentalk;

namespace {

const QString kDataDir = QStringLiteral(OPENTALK_TEST_DATA_DIR);
const QString kGreeterBot = QStringLiteral(OPENTALK_GREETER_BOT);
constexpr int kWaitMs = 15'000;

} // namespace

class TestExamples : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    std::unique_ptr<server::ServerDatabase> m_db;
    std::unique_ptr<server::Server> m_server;

    QStringList textsFrom(const QSignalSpy &spy, quint32 userId) const
    {
        QStringList texts;
        for (const QList<QVariant> &args : spy) {
            const auto m = args.first().value<protocol::TextMessage>();
            if (m.fromUserId == userId) {
                texts << m.text;
            }
        }
        return texts;
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
        m_db = std::make_unique<server::ServerDatabase>();
        QVERIFY(m_db->open(m_dir.filePath(QStringLiteral("examples.db"))));
        for (const QString &name : {QStringLiteral("greeter"), QStringLiteral("alice")}) {
            const auto hash = security::hashPassword(name + QStringLiteral("-password"),
                                                     security::PasswordHashCost::minimumForTests());
            QVERIFY(hash && m_db->createAccount(name, *hash, protocol::Role::User));
        }
        QVERIFY(m_db->setBot(QStringLiteral("greeter"), true));
        server::ServerConfig config;
        config.address = QHostAddress::LocalHost;
        config.port = 0;
        config.tls = *tls;
        config.session.serverName = QStringLiteral("Example test");
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

    void greeterBotGreetsAnswersAndTalks()
    {
        QProcess bot;
        bot.setProcessChannelMode(QProcess::MergedChannels);
        bot.start(kGreeterBot,
                  {QStringLiteral("127.0.0.1"), QStringLiteral("--port"), QString::number(m_server->serverPort()),
                   QStringLiteral("--fingerprint"), m_server->certificateFingerprint(), QStringLiteral("--user"),
                   QStringLiteral("greeter")});
        QVERIFY2(bot.waitForStarted(kWaitMs), qPrintable(bot.errorString()));
        bot.write("greeter-password\n");
        QString output;
        QTRY_VERIFY2_WITH_TIMEOUT((output += QString::fromUtf8(bot.readAll()),
                                   output.contains(QStringLiteral("Connected to Example test as greeter."))),
                                  qPrintable(output), kWaitMs);

        client::Connection alice;
        QSignalSpy text(&alice, &client::Connection::textReceived);
        QSignalSpy audio(&alice, &client::Connection::audioReceived);
        QSignalSpy connected(&alice, &client::Connection::connected);
        client::ConnectOptions o;
        o.host = QStringLiteral("127.0.0.1");
        o.port = m_server->serverPort();
        o.trust = security::ServerTrust::pinned(m_server->certificateFingerprint());
        o.credentials.mode = client::Credentials::Mode::Account;
        o.credentials.name = QStringLiteral("alice");
        o.credentials.password = QStringLiteral("alice-password");
        o.autoReconnect = false;
        alice.connectToServer(o);
        QVERIFY(connected.wait(kWaitMs));

        // Everyone can see that it is a bot.
        quint32 botId = 0;
        for (const protocol::UserInfo &u : alice.serverState().users()) {
            if (u.name == QLatin1String("greeter")) {
                botId = u.id;
                QVERIFY((u.flags & protocol::BotFlag) != 0);
            }
        }
        QVERIFY(botId != 0);

        QTRY_VERIFY_WITH_TIMEOUT(
            textsFrom(text, botId).contains(QStringLiteral("Welcome, alice! Type !help to see what I can do.")),
            kWaitMs);
        alice.sendText(protocol::TextTarget::Channel, protocol::kRootChannelId, QStringLiteral("!roll 20"));
        const QRegularExpression rolled(QStringLiteral("^alice rolled ([0-9]+) \\(1 to 20\\)\\.$"));
        QTRY_VERIFY_WITH_TIMEOUT(textsFrom(text, botId).indexOf(rolled) >= 0, kWaitMs);
        const int value =
            rolled.match(textsFrom(text, botId).at(textsFrom(text, botId).indexOf(rolled))).captured(1).toInt();
        QVERIFY(value >= 1 && value <= 20);
        alice.sendText(protocol::TextTarget::Channel, protocol::kRootChannelId, QStringLiteral("!who"));
        QTRY_VERIFY_WITH_TIMEOUT(textsFrom(text, botId).contains(QStringLiteral("Here: alice, greeter (bot).")) ||
                                     textsFrom(text, botId).contains(QStringLiteral("Here: greeter (bot), alice.")),
                                 kWaitMs);

        // It talks: a second of audio from the bot reaches alice.
        QTRY_VERIFY_WITH_TIMEOUT(alice.isVoiceReady(), kWaitMs);
        alice.sendText(protocol::TextTarget::Channel, protocol::kRootChannelId, QStringLiteral("!beep"));
        QTRY_VERIFY_WITH_TIMEOUT(std::count_if(audio.cbegin(), audio.cend(),
                                               [botId](const QList<QVariant> &args) {
                                                   return args.first().value<protocol::AudioData>().userId == botId;
                                               }) >= 25,
                                 kWaitMs);

        // Its own messages and other text do not make it answer itself.
        const qsizetype before = textsFrom(text, botId).size();
        alice.sendText(protocol::TextTarget::Channel, protocol::kRootChannelId, QStringLiteral("just chatting"));
        QTest::qWait(500);
        QCOMPARE(textsFrom(text, botId).size(), before);

        alice.disconnectFromServer();
        bot.kill();
        bot.waitForFinished(kWaitMs);
    }
};

QTEST_GUILESS_MAIN(TestExamples)
#include "tst_examples.moc"
