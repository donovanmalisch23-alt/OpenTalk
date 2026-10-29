// SPDX-License-Identifier: GPL-3.0-only
// Real plugin processes connected to a real client and server: messages in both
// directions, menu commands, enforcement of what each plugin was granted, and plugins
// that misbehave, hang or crash. The plugin is tests/plugins/test-plugin, copied into a
// plugin folder the way a user would install it.

#include "opentalk/client/Connection.h"
#include "opentalk/plugins/PluginHost.h"
#include "opentalk/plugins/PluginRegistry.h"
#include "opentalk/security/Tls.h"
#include "opentalk/server/Server.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace opentalk;
using namespace opentalk::plugins;
using State = PluginProcess::State;

namespace {

const QString kDataDir = QStringLiteral(OPENTALK_TEST_DATA_DIR);
const QString kTestPlugin = QStringLiteral(OPENTALK_TEST_PLUGIN);
const QString kPythonExample = QStringLiteral(OPENTALK_PYTHON_EXAMPLE);
const QString kDiceExample = QStringLiteral(OPENTALK_DICE_EXAMPLE);
constexpr int kWaitMs = 10'000;

struct Notification {
    QString plugin;
    QString text;
};

} // namespace

class TestPlugins : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QSslConfiguration m_tls;
    std::unique_ptr<server::Server> m_server;
    int m_counter = 0;

    /// Makes a plugin folder around the test plugin running in `mode`.
    QString makePlugin(const QString &id, const QString &mode, const QStringList &capabilities)
    {
        const QString folder = m_dir.filePath(QStringLiteral("source-%1").arg(++m_counter));
        QDir().mkpath(folder);
        const QString program = QFileInfo(kTestPlugin).fileName();
        if (!QFile::copy(kTestPlugin, QDir(folder).filePath(program))) {
            return {};
        }
        QJsonObject manifest{{QStringLiteral("id"), id},
                             {QStringLiteral("name"), QStringLiteral("Test %1").arg(mode)},
                             {QStringLiteral("version"), QStringLiteral("1.0")},
                             {QStringLiteral("executable"), program},
                             {QStringLiteral("arguments"), QJsonArray{mode}},
                             {QStringLiteral("capabilities"), QJsonArray::fromStringList(capabilities)}};
        QFile file(QDir(folder).filePath(QStringLiteral("plugin.json")));
        if (!file.open(QIODevice::WriteOnly)) {
            return {};
        }
        file.write(QJsonDocument(manifest).toJson());
        return folder;
    }

    /// Installs and enables a plugin, granting `granted` (names).
    static bool installAndEnable(PluginRegistry &registry, const QString &source, const QStringList &granted)
    {
        QString error;
        const auto id = registry.install(source, &error);
        if (!id) {
            qWarning() << error;
            return false;
        }
        QList<PluginCapability> capabilities;
        for (const QString &name : granted) {
            capabilities.append(*pluginCapabilityFromName(name));
        }
        return registry.enable(*id, capabilities, &error);
    }

    client::ConnectOptions options(const QString &name) const
    {
        client::ConnectOptions o;
        o.host = QStringLiteral("127.0.0.1");
        o.port = m_server->serverPort();
        o.trust = security::ServerTrust::pinned(m_server->certificateFingerprint());
        o.credentials.name = name;
        o.autoReconnect = false;
        return o;
    }

    static bool login(client::Connection &connection, const client::ConnectOptions &o)
    {
        QSignalSpy connected(&connection, &client::Connection::connected);
        connection.connectToServer(o);
        return connected.wait(kWaitMs);
    }

    /// A registry in a fresh data folder.
    std::unique_ptr<PluginRegistry> freshRegistry()
    {
        const QString root = m_dir.filePath(QStringLiteral("data-%1").arg(++m_counter));
        auto registry = std::make_unique<PluginRegistry>(QDir(root).filePath(QStringLiteral("plugins")),
                                                         QDir(root).filePath(QStringLiteral("config/plugins.json")));
        registry->load();
        return registry;
    }

    static PluginHost::Limits fastLimits()
    {
        PluginHost::Limits limits;
        limits.process.readyTimeoutMs = 1500;
        limits.process.stopGraceMs = 1000;
        return limits;
    }

private slots:
    void initTestCase()
    {
        qRegisterMetaType<client::DisconnectInfo>();
        QString error;
        QVERIFY2(security::initializeTls(&error), qPrintable(error));
        const auto tls = security::serverTlsConfiguration(kDataDir + QStringLiteral("/test-server.crt"),
                                                          kDataDir + QStringLiteral("/test-server.key"), &error);
        QVERIFY2(tls.has_value(), qPrintable(error));
        server::ServerConfig config;
        config.address = QHostAddress::LocalHost;
        config.port = 0;
        config.tls = *tls;
        config.session.serverName = QStringLiteral("Plugin test");
        config.allowGuests = true;
        config.shutdownGraceMs = 1'000;
        m_server = std::make_unique<server::Server>(config);
        QVERIFY2(m_server->start(&error), qPrintable(error));
    }

    void cleanupTestCase()
    {
        QSignalSpy stopped(m_server.get(), &server::Server::stopped);
        m_server->stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, kWaitMs);
    }

    void pluginTalksBothWaysAndAddsCommands()
    {
        auto registry = freshRegistry();
        const QStringList all{QStringLiteral("presence.read"), QStringLiteral("messages.read"),
                              QStringLiteral("messages.send"), QStringLiteral("notifications.show"),
                              QStringLiteral("commands.add")};
        QVERIFY(
            installAndEnable(*registry, makePlugin(QStringLiteral("org.test.echo"), QStringLiteral("echo"), all), all));
        client::Connection me;
        QVERIFY(login(me, options(QStringLiteral("Me"))));
        PluginHost host(*registry, me, m_dir.filePath(QStringLiteral("logs")), fastLimits());
        QList<Notification> notes;
        connect(&host, &PluginHost::notification, this,
                [&notes](const QString &plugin, const QString &text, bool) { notes.append({plugin, text}); });
        host.startEnabled();
        QTRY_VERIFY_WITH_TIMEOUT(host.state(QStringLiteral("org.test.echo")) == State::Running, kWaitMs);

        // Its menu item is offered and reaches it.
        QTRY_COMPARE_WITH_TIMEOUT(host.commands().size(), 1, kWaitMs);
        QCOMPARE(host.commands().first().title, QStringLiteral("Say ping"));
        host.runCommand(QStringLiteral("org.test.echo"), QStringLiteral("ping"));
        QTRY_VERIFY_WITH_TIMEOUT(!notes.isEmpty() && notes.last().text == QStringLiteral("pong"), kWaitMs);
        QCOMPARE(notes.last().plugin, QStringLiteral("Test echo"));

        // It sees someone join and answers a message in the channel, in my name.
        client::Connection friendConnection;
        QSignalSpy friendText(&friendConnection, &client::Connection::textReceived);
        QVERIFY(login(friendConnection, options(QStringLiteral("Friend"))));
        QTRY_VERIFY_WITH_TIMEOUT(
            std::any_of(notes.cbegin(), notes.cend(),
                        [](const Notification &n) { return n.text == QStringLiteral("joined Friend"); }),
            kWaitMs);
        friendConnection.sendText(protocol::TextTarget::Channel, protocol::kRootChannelId,
                                  QStringLiteral("!echo hello"));
        QTRY_VERIFY_WITH_TIMEOUT(std::any_of(friendText.cbegin(), friendText.cend(),
                                             [&me](const QList<QVariant> &args) {
                                                 const auto m = args.first().value<protocol::TextMessage>();
                                                 return m.text == QStringLiteral("echo: hello") &&
                                                        m.fromUserId == me.serverState().selfId();
                                             }),
                                 kWaitMs);
        // ...and is told the server accepted it.
        QTRY_VERIFY_WITH_TIMEOUT(
            std::any_of(notes.cbegin(), notes.cend(),
                        [](const Notification &n) { return n.text.startsWith(QStringLiteral("result 1 ok")); }),
            kWaitMs);
        // Its own message does not trigger it again (it ignores "fromSelf").
        QTest::qWait(300);
        QCOMPARE(std::count_if(friendText.cbegin(), friendText.cend(),
                               [](const QList<QVariant> &args) {
                                   return args.first().value<protocol::TextMessage>().text.startsWith(
                                       QStringLiteral("echo:"));
                               }),
                 1);

        // Stopping is orderly: it is asked to exit and does.
        host.stop(QStringLiteral("org.test.echo"));
        QTRY_VERIFY_WITH_TIMEOUT(host.state(QStringLiteral("org.test.echo")) == State::Stopped, kWaitMs);
        QVERIFY(host.commands().isEmpty());
        const QString log = [&host] {
            QFile file(host.logFileFor(QStringLiteral("org.test.echo")));
            return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
        }();
        QVERIFY2(log.contains(QStringLiteral("connected to Plugin test")), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("exited (0)")), qPrintable(log));
        friendConnection.disconnectFromServer();
        me.disconnectFromServer();
    }

    void onlyGrantedCapabilitiesWork()
    {
        auto registry = freshRegistry();
        // It asks for everything but is granted only notifications.
        const QStringList asked{QStringLiteral("messages.read"), QStringLiteral("messages.send"),
                                QStringLiteral("notifications.show"), QStringLiteral("commands.add")};
        QVERIFY(installAndEnable(*registry,
                                 makePlugin(QStringLiteral("org.test.limited"), QStringLiteral("echo"), asked),
                                 {QStringLiteral("notifications.show")}));
        QVERIFY(installAndEnable(*registry,
                                 makePlugin(QStringLiteral("org.test.forbidden"), QStringLiteral("forbidden"), asked),
                                 {QStringLiteral("notifications.show")}));
        client::Connection me;
        QVERIFY(login(me, options(QStringLiteral("Granted"))));
        PluginHost host(*registry, me, m_dir.filePath(QStringLiteral("logs")), fastLimits());
        QList<Notification> notes;
        connect(&host, &PluginHost::notification, this,
                [&notes](const QString &plugin, const QString &text, bool) { notes.append({plugin, text}); });
        host.startEnabled();

        // A request it was not granted is refused, and it is told why.
        QTRY_VERIFY_WITH_TIMEOUT(std::any_of(notes.cbegin(), notes.cend(),
                                             [](const Notification &n) {
                                                 return n.text ==
                                                        QStringLiteral("result 7 error not allowed: messages.send");
                                             }),
                                 kWaitMs);
        // No menu items without commands.add.
        QTRY_VERIFY_WITH_TIMEOUT(host.state(QStringLiteral("org.test.limited")) == State::Running, kWaitMs);
        QVERIFY(host.commands().isEmpty());
        // Without messages.read it never sees the channel, so it cannot answer.
        client::Connection other;
        QSignalSpy otherText(&other, &client::Connection::textReceived);
        QVERIFY(login(other, options(QStringLiteral("Other"))));
        other.sendText(protocol::TextTarget::Channel, protocol::kRootChannelId, QStringLiteral("!echo secret"));
        QTest::qWait(700);
        QVERIFY(std::none_of(otherText.cbegin(), otherText.cend(), [](const QList<QVariant> &args) {
            return args.first().value<protocol::TextMessage>().text.startsWith(QStringLiteral("echo:"));
        }));
        // Without presence.read it is not told who joined either.
        QVERIFY(std::none_of(notes.cbegin(), notes.cend(),
                             [](const Notification &n) { return n.text.startsWith(QStringLiteral("joined")); }));
        other.disconnectFromServer();
        me.disconnectFromServer();
        host.stopAll();
    }

    void misbehavingPluginsAreTurnedOff_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<QString>("problem");
        QTest::newRow("never ready") << "silent" << "did not start in time";
        QTest::newRow("flooding") << "flood" << "too often";
        QTest::newRow("huge line") << "longline" << "longer than 64 KiB";
        QTest::newRow("not JSON") << "garbage" << "not a JSON object";
    }

    void misbehavingPluginsAreTurnedOff()
    {
        QFETCH(QString, mode);
        QFETCH(QString, problem);
        auto registry = freshRegistry();
        const QStringList caps{QStringLiteral("notifications.show")};
        const QString id = QStringLiteral("org.test.") + mode;
        QVERIFY(installAndEnable(*registry, makePlugin(id, mode, caps), caps));
        client::Connection notConnected;
        PluginHost host(*registry, notConnected, m_dir.filePath(QStringLiteral("logs")), fastLimits());
        QStringList problems;
        int shown = 0;
        connect(&host, &PluginHost::problem, this,
                [&problems](const QString &, const QString &text) { problems.append(text); });
        connect(&host, &PluginHost::notification, this, [&shown] { ++shown; });
        QVERIFY(host.start(id));
        QTRY_VERIFY_WITH_TIMEOUT(host.state(id) == State::Failed, kWaitMs);
        QVERIFY2(host.process(id)->problem().contains(problem), qPrintable(host.process(id)->problem()));
        QVERIFY(!problems.isEmpty());
        QVERIFY(shown <= 5); // the notification limit held even while it flooded
    }

    void crashingPluginIsRestartedThenTurnedOff()
    {
        auto registry = freshRegistry();
        QVERIFY(
            installAndEnable(*registry, makePlugin(QStringLiteral("org.test.crash"), QStringLiteral("crash"), {}), {}));
        client::Connection notConnected;
        PluginHost::Limits limits = fastLimits();
        limits.process.maxRestarts = 2;
        PluginHost host(*registry, notConnected, m_dir.filePath(QStringLiteral("logs")), limits);
        QStringList problems;
        connect(&host, &PluginHost::problem, this,
                [&problems](const QString &, const QString &text) { problems.append(text); });
        QVERIFY(host.start(QStringLiteral("org.test.crash")));
        QTRY_VERIFY_WITH_TIMEOUT(host.state(QStringLiteral("org.test.crash")) == State::Failed, 15'000);
        QCOMPARE(std::count_if(problems.cbegin(), problems.cend(),
                               [](const QString &p) { return p.contains(QStringLiteral("being restarted")); }),
                 2);
        QVERIFY(problems.last().contains(QStringLiteral("turned off")));
        // The user can try again.
        QVERIFY(host.start(QStringLiteral("org.test.crash")));
        QVERIFY(host.state(QStringLiteral("org.test.crash")) == State::Starting);
        host.stopAll();
    }

    void pluginsThatNeedReviewDoNotStart()
    {
        auto registry = freshRegistry();
        const QString source = makePlugin(QStringLiteral("org.test.review"), QStringLiteral("echo"), {});
        QVERIFY(installAndEnable(*registry, source, {}));
        // The plugin is updated and now asks for more.
        QFile file(QDir(source).filePath(QStringLiteral("plugin.json")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonObject manifest = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        manifest.insert(QStringLiteral("capabilities"), QJsonArray{QStringLiteral("messages.private")});
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(manifest).toJson());
        file.close();
        QVERIFY(registry->install(source));
        client::Connection notConnected;
        PluginHost host(*registry, notConnected, m_dir.filePath(QStringLiteral("logs")), fastLimits());
        QString error;
        QVERIFY(!host.start(QStringLiteral("org.test.review"), &error));
        QVERIFY2(error.contains(QStringLiteral("review")), qPrintable(error));
        host.startEnabled();
        QVERIFY(host.state(QStringLiteral("org.test.review")) == State::Stopped);
    }

    void diceExampleRolls()
    {
        auto registry = freshRegistry();
        QString error;
        const auto id = registry->install(kDiceExample, &error);
        QVERIFY2(id.has_value(), qPrintable(error));
        QVERIFY(registry->enable(*id, registry->plugin(*id)->manifest.capabilities));
        client::Connection me;
        QVERIFY(login(me, options(QStringLiteral("Roller"))));
        client::Connection friendConnection;
        QSignalSpy friendText(&friendConnection, &client::Connection::textReceived);
        QVERIFY(login(friendConnection, options(QStringLiteral("Watcher"))));
        PluginHost host(*registry, me, m_dir.filePath(QStringLiteral("logs")), PluginHost::Limits{});
        host.startEnabled();
        QTRY_COMPARE_WITH_TIMEOUT(host.commands().size(), 1, kWaitMs);
        QCOMPARE(host.commands().first().title, QStringLiteral("Roll a dice"));
        const auto saw = [&friendText](const QRegularExpression &pattern) {
            return std::any_of(friendText.cbegin(), friendText.cend(), [&](const QList<QVariant> &args) {
                return pattern.match(args.first().value<protocol::TextMessage>().text).hasMatch();
            });
        };
        host.runCommand(*id, QStringLiteral("roll"));
        QTRY_VERIFY_WITH_TIMEOUT(saw(QRegularExpression(QStringLiteral("^Roller rolled [1-6] \\(1 to 6\\)\\.$"))),
                                 kWaitMs);
        friendConnection.sendText(protocol::TextTarget::Channel, protocol::kRootChannelId, QStringLiteral("!dice 20"));
        QTRY_VERIFY_WITH_TIMEOUT(
            saw(QRegularExpression(QStringLiteral("^Watcher rolled ([1-9]|1[0-9]|20) \\(1 to 20\\)\\.$"))), kWaitMs);
        host.stopAll();
        QTRY_VERIFY_WITH_TIMEOUT(host.state(*id) == State::Stopped, kWaitMs);
        friendConnection.disconnectFromServer();
        me.disconnectFromServer();
    }

    void pythonExampleRuns()
    {
        QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
        if (python.isEmpty()) {
            python = QStandardPaths::findExecutable(QStringLiteral("python"));
        }
        if (python.isEmpty()) {
            QSKIP("Python is not installed here");
        }
        auto registry = freshRegistry();
        QString error;
        const auto id = registry->install(kPythonExample, &error);
        QVERIFY2(id.has_value(), qPrintable(error));
        QVERIFY(registry->enable(*id, registry->plugin(*id)->manifest.capabilities));
        client::Connection me;
        QVERIFY(login(me, options(QStringLiteral("Pythonista"))));
        PluginHost host(*registry, me, m_dir.filePath(QStringLiteral("logs")), PluginHost::Limits{});
        QList<Notification> notes;
        connect(&host, &PluginHost::notification, this,
                [&notes](const QString &plugin, const QString &text, bool) { notes.append({plugin, text}); });
        host.startEnabled();
        QTRY_VERIFY_WITH_TIMEOUT(host.state(*id) == State::Running, kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(!host.commands().isEmpty(), kWaitMs);
        host.runCommand(*id, host.commands().first().id);
        QTRY_VERIFY_WITH_TIMEOUT(!notes.isEmpty(), kWaitMs);
        QVERIFY2(notes.last().text.contains(QStringLiteral("Plugin test")), qPrintable(notes.last().text));
        host.stopAll();
        QTRY_VERIFY_WITH_TIMEOUT(host.state(*id) == State::Stopped, kWaitMs);
        me.disconnectFromServer();
    }
};

QTEST_GUILESS_MAIN(TestPlugins)
#include "tst_plugins.moc"
