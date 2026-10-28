// SPDX-License-Identifier: GPL-3.0-only
// The real main window connected to a real in-process server: trust-on-first-use,
// the channel tree, chat, private messages, announcements and accessibility while
// connected. Dialogs are replaced by the window's test hooks.

#include "../unit/AudioTestUtil.h"

#include "opentalk/audio/AudioEngine.h"
#include "opentalk/client/Connection.h"
#include "opentalk/security/PasswordHash.h"
#include "opentalk/security/Tls.h"
#include "opentalk/server/Server.h"
#include "opentalk/storage/AppPaths.h"
#include "opentalk/storage/ClientSettings.h"
#include "opentalk/storage/ServerProfiles.h"
#include "opentalk/storage/SettingsStore.h"
#include "opentalk/ui/AudioController.h"
#include "opentalk/ui/ChannelTree.h"
#include "opentalk/ui/ConnectDialog.h"
#include "opentalk/ui/MainWindow.h"

#include <QAbstractSpinBox>
#include <QAccessible>
#include <QLineEdit>
#include <QListWidget>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidgetItemIterator>

using namespace opentalk;

namespace {

const QString kDataDir = QStringLiteral(OPENTALK_TEST_DATA_DIR);
constexpr int kWaitMs = 10'000;

QStringList treeTexts(QTreeWidget *tree)
{
    QStringList texts;
    for (QTreeWidgetItemIterator it(tree); *it != nullptr; ++it) {
        texts << (*it)->text(0);
    }
    return texts;
}

QStringList unnamedFocusableWidgets(QWidget *root)
{
    QStringList problems;
    for (QWidget *widget : root->findChildren<QWidget *>()) {
        if (widget->focusPolicy() == Qt::NoFocus || !widget->isVisibleTo(root)) {
            continue;
        }
        // A spin box's internal editor is not exposed separately; the spin box itself is
        // checked (and named) as its own focusable widget.
        if (qobject_cast<QAbstractSpinBox *>(widget->parentWidget()) != nullptr) {
            continue;
        }
        QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(widget);
        if (iface == nullptr || iface->text(QAccessible::Name).trimmed().isEmpty()) {
            problems << QString::fromLatin1(widget->metaObject()->className()) + QLatin1Char(' ') +
                            widget->objectName();
        }
    }
    return problems;
}

} // namespace

class TestMainWindowOnline : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    std::unique_ptr<server::Server> m_server;
    std::unique_ptr<storage::AppPaths> m_paths;
    std::unique_ptr<storage::SettingsStore> m_settings;
    std::unique_ptr<storage::ServerProfileStore> m_profiles;
    std::unique_ptr<ui::MainWindow> m_window;
    QList<ui::MainWindow::TrustQuestion> m_trustQuestions;
    QStringList m_errors;
    QStringList m_spoken;

    storage::ServerProfile profile() const
    {
        storage::ServerProfile p;
        p.name = QStringLiteral("Test server");
        p.host = QStringLiteral("127.0.0.1");
        p.port = m_server->serverPort();
        p.guest = true;
        p.userName = QStringLiteral("Tester");
        return p;
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
        config.session.serverName = QStringLiteral("Online UI server");
        config.allowGuests = true;
        config.passwordCost = security::PasswordHashCost::minimumForTests();
        m_server = std::make_unique<server::Server>(config);
        QVERIFY2(m_server->start(&error), qPrintable(error));
    }

    void cleanupTestCase()
    {
        m_window.reset();
        QSignalSpy stopped(m_server.get(), &server::Server::stopped);
        m_server->stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, kWaitMs);
    }

    void init()
    {
        m_paths = std::make_unique<storage::AppPaths>(storage::AppPaths::resolve({}, m_dir.path()));
        QVERIFY(m_paths->ensureDirectories());
        QFile::remove(m_paths->serverProfilesFile());
        m_settings = std::make_unique<storage::SettingsStore>(storage::client::schema(), m_paths->clientSettingsFile());
        m_settings->load();
        m_profiles = std::make_unique<storage::ServerProfileStore>(m_paths->serverProfilesFile());
        m_window = std::make_unique<ui::MainWindow>(*m_settings, *m_paths, *m_profiles,
                                                    std::make_unique<audio::SyntheticAudioBackend>());
        m_trustQuestions.clear();
        m_errors.clear();
        m_spoken.clear();
        m_window->setTrustPrompt([this](const ui::MainWindow::TrustQuestion &q) {
            m_trustQuestions.append(q);
            return true;
        });
        m_window->setErrorReporter([this](const QString &, const QString &text) { m_errors.append(text); });
        m_window->setAnnouncementHook([this](const QString &text) { m_spoken.append(text); });
        m_window->show();
    }

    void cleanup()
    {
        m_window->connection().disconnectFromServer();
        m_window.reset();
    }

    void firstConnectionAsksToTrustAndRemembers()
    {
        m_window->connectToServer(profile(), QString(), true);
        QTRY_VERIFY_WITH_TIMEOUT(m_window->connection().state() == client::ConnectionState::Connected, kWaitMs);
        QCOMPARE(m_trustQuestions.size(), 1);
        QCOMPARE(m_trustQuestions.first().presentedFingerprint, m_server->certificateFingerprint());
        QVERIFY(m_trustQuestions.first().previousFingerprint.isEmpty());
        QVERIFY(m_errors.isEmpty());

        // The fingerprint is saved, so the next connection does not ask again.
        QCOMPARE(m_profiles->profiles().size(), 1);
        QCOMPARE(m_profiles->profiles().first().pinnedFingerprint, m_server->certificateFingerprint());
        storage::ServerProfileStore reloaded(m_paths->serverProfilesFile());
        reloaded.load();
        QCOMPARE(reloaded.profiles().first().pinnedFingerprint, m_server->certificateFingerprint());

        m_window->connection().disconnectFromServer();
        QTRY_VERIFY_WITH_TIMEOUT(m_window->connection().state() == client::ConnectionState::Disconnected, kWaitMs);
        m_window->connectToServer(m_profiles->profiles().first(), QString(), true);
        QTRY_VERIFY_WITH_TIMEOUT(m_window->connection().state() == client::ConnectionState::Connected, kWaitMs);
        QCOMPARE(m_trustQuestions.size(), 1);
    }

    void changedCertificateIsFlagged()
    {
        storage::ServerProfile p = profile();
        p.pinnedFingerprint = QStringLiteral("SHA256:") +
                              QString(64, QLatin1Char('A'))
                                  .replace(QRegularExpression(QStringLiteral("(..)(?!$)")), QStringLiteral("\\1:"));
        m_window->setTrustPrompt([this](const ui::MainWindow::TrustQuestion &q) {
            m_trustQuestions.append(q);
            return false; // the user declines
        });
        m_window->connectToServer(p, QString(), false);
        QTRY_COMPARE_WITH_TIMEOUT(m_trustQuestions.size(), 1, kWaitMs);
        QVERIFY(!m_trustQuestions.first().previousFingerprint.isEmpty());
        QTest::qWait(300);
        QVERIFY(m_window->connection().state() == client::ConnectionState::Disconnected);
        QVERIFY(m_window->historyText().contains(QStringLiteral("not trusted")));
    }

    void chatWhileConnected()
    {
        m_window->connectToServer(profile(), QString(), false);
        QTRY_VERIFY_WITH_TIMEOUT(m_window->connection().state() == client::ConnectionState::Connected, kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(m_window->statusText().startsWith(QStringLiteral("Connected to Online UI server")),
                                 kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(treeTexts(m_window->channelTree()).contains(QStringLiteral("Tester (you), guest")),
                                 kWaitMs);
        QCOMPARE(treeTexts(m_window->channelTree()).first(), QStringLiteral("Online UI server, 1 user"));

        // Another person arrives; the window shows and (after coalescing) announces it.
        client::Connection friendConnection;
        client::ConnectOptions o;
        o.host = QStringLiteral("127.0.0.1");
        o.port = m_server->serverPort();
        o.trust = security::ServerTrust::pinned(m_server->certificateFingerprint());
        o.credentials.name = QStringLiteral("Friend");
        o.autoReconnect = false;
        QSignalSpy friendConnected(&friendConnection, &client::Connection::connected);
        friendConnection.connectToServer(o);
        QVERIFY(friendConnected.wait(kWaitMs));
        QTRY_VERIFY_WITH_TIMEOUT(m_window->historyText().contains(QStringLiteral("Friend joined the server.")),
                                 kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(m_spoken.contains(QStringLiteral("Friend joined")), kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(treeTexts(m_window->channelTree()).contains(QStringLiteral("Friend, guest")), kWaitMs);

        // Typing in the message box sends to the channel.
        auto *input = qobject_cast<QLineEdit *>(m_window->paneWidget(ui::MainWindow::Pane::MessageInput));
        QVERIFY(input != nullptr);
        input->setText(QStringLiteral("hello everyone"));
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(input->text().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(m_window->historyText().contains(QStringLiteral("Tester: hello everyone")), kWaitMs);

        // Messages from others are shown and spoken; nothing is interpreted as markup.
        const quint32 testerId = m_window->connection().serverState().selfId();
        friendConnection.sendText(protocol::TextTarget::User, testerId, QStringLiteral("<b>psst</b>"));
        QTRY_VERIFY_WITH_TIMEOUT(m_window->historyText().contains(QStringLiteral("Private from Friend: <b>psst</b>")),
                                 kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(m_spoken.contains(QStringLiteral("Private from Friend: <b>psst</b>")), kWaitMs);

        // Everything focusable while connected still has an accessible name.
        const QStringList problems = unnamedFocusableWidgets(m_window.get());
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral(", "))));

        friendConnection.disconnectFromServer();
        QTRY_VERIFY_WITH_TIMEOUT(m_window->historyText().contains(QStringLiteral("Friend left the server.")), kWaitMs);

        m_window->connection().disconnectFromServer();
        QTRY_COMPARE_WITH_TIMEOUT(treeTexts(m_window->channelTree()),
                                  QStringList{QStringLiteral("Not connected to a server")}, kWaitMs);
    }

    void guestNameClashIsExplained()
    {
        client::Connection holder;
        client::ConnectOptions o;
        o.host = QStringLiteral("127.0.0.1");
        o.port = m_server->serverPort();
        o.trust = security::ServerTrust::pinned(m_server->certificateFingerprint());
        o.credentials.name = QStringLiteral("Tester");
        o.autoReconnect = false;
        QSignalSpy holderConnected(&holder, &client::Connection::connected);
        holder.connectToServer(o);
        QVERIFY(holderConnected.wait(kWaitMs));

        storage::ServerProfile p = profile();
        p.pinnedFingerprint = m_server->certificateFingerprint();
        m_window->connectToServer(p, QString(), false);
        QTRY_COMPARE_WITH_TIMEOUT(m_errors.size(), 1, kWaitMs);
        QVERIFY(m_errors.first().contains(QStringLiteral("already in use")));
    }

    void connectDialogIsAccessible()
    {
        storage::ServerProfile saved = profile();
        m_profiles->upsert(saved);
        ui::ConnectDialog dialog(*m_profiles);
        dialog.show();
        QCOMPARE(dialog.savedServers()->count(), 2); // "New server" and the saved one
        dialog.savedServers()->setCurrentRow(1);
        const QStringList problems = unnamedFocusableWidgets(&dialog);
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral(", "))));
        const ui::ConnectDialog::Choice choice = dialog.choice();
        QCOMPARE(choice.profile.host, saved.host);
        QVERIFY(choice.profile.guest);
    }

    void hostingFromTheWindow()
    {
        m_window->setLocalServerExecutable(QStringLiteral(OPENTALK_SERVER_EXECUTABLE));
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        const quint16 port = probe.serverPort();
        probe.close();

        ui::LocalServerDialog::Choice choice;
        choice.name = QStringLiteral("Hosted here");
        choice.port = port;
        choice.allowGuests = false;
        choice.adminName = QStringLiteral("owner");
        choice.adminPassword = QStringLiteral("owner password");
        m_window->startLocalServer(choice);
        QTRY_VERIFY_WITH_TIMEOUT(m_window->connection().state() == client::ConnectionState::Connected, 30'000);
        QVERIFY2(m_errors.isEmpty(), qPrintable(m_errors.join(QLatin1Char('\n'))));
        QVERIFY(m_trustQuestions.isEmpty()); // our own server: nothing to confirm
        QVERIFY(m_window->connection().serverState().selfRole() == protocol::Role::Owner);
        QVERIFY(m_window->historyText().contains(QStringLiteral("is running on port %1").arg(port)));
        // The choices (but not the password) are remembered.
        QCOMPARE(m_settings->value(storage::client::kLocalServerAdminName).toString(), QStringLiteral("owner"));

        m_window->stopLocalServer();
        QTRY_VERIFY_WITH_TIMEOUT(m_window->localServer().state() == client::LocalServer::State::Stopped, 30'000);
        QTRY_VERIFY_WITH_TIMEOUT(m_window->historyText().contains(QStringLiteral("Your server has stopped.")), 10'000);
        QVERIFY(m_window->connection().state() == client::ConnectionState::Disconnected); // not reconnecting
        QVERIFY(m_errors.isEmpty());
    }

    void localServerDialogIsAccessible()
    {
        ui::LocalServerDialog dialog(ui::LocalServerDialog::Choice{});
        dialog.show();
        const QStringList problems = unnamedFocusableWidgets(&dialog);
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral(", "))));
    }

    void createChannelDialogIsAccessible()
    {
        ui::CreateChannelDialog dialog(QStringLiteral("Server"), true);
        dialog.show();
        const QStringList problems = unnamedFocusableWidgets(&dialog);
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral(", "))));
        ui::CreateChannelDialog guestDialog(QStringLiteral("Server"), false);
        QVERIFY(!guestDialog.persistent());
    }

    void voiceWithTheWindow()
    {
        m_settings->setValue(storage::client::kAnnounceTalking, true);
        m_settings->setValue(storage::client::kAnnouncePresence, true);
        m_window->connectToServer(profile(), QString(), false);
        QTRY_VERIFY_WITH_TIMEOUT(m_window->connection().isVoiceReady(), kWaitMs);

        // Another person talks with a 600 Hz tone.
        client::Connection speaker;
        client::ConnectOptions o;
        o.host = QStringLiteral("127.0.0.1");
        o.port = m_server->serverPort();
        o.trust = security::ServerTrust::pinned(m_server->certificateFingerprint());
        o.credentials.name = QStringLiteral("Speaker");
        o.autoReconnect = false;
        speaker.connectToServer(o);
        QTRY_VERIFY_WITH_TIMEOUT(speaker.isVoiceReady(), kWaitMs);
        audio::AudioEngine speakerEngine(
            std::make_unique<audio::SyntheticAudioBackend>([](qint64 i) { return test::sine(i, 600.0); }));
        audio::AudioSettings settings;
        settings.mode = audio::TransmitMode::Continuous;
        settings.soundsEnabled = false;
        speakerEngine.setSettings(settings);
        connect(&speakerEngine, &audio::AudioEngine::packetReady, &speaker,
                [&](audio::StreamType type, quint16 sequence, const QByteArray &opus) {
                    speaker.sendAudio(static_cast<protocol::AudioStream>(type), sequence, opus);
                });
        int heardBySpeaker = 0;
        connect(&speaker, &client::Connection::audioReceived, &speaker,
                [&](const protocol::AudioData &) { ++heardBySpeaker; });
        speakerEngine.setTransmitAllowed(true);
        speakerEngine.start();

        const quint32 speakerId = speaker.serverState().selfId();
        QTRY_VERIFY_WITH_TIMEOUT(m_window->channelTree()->isUserTalking(speakerId), kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(m_spoken.contains(QStringLiteral("Speaker talking")), kWaitMs);
        auto &windowDevice = dynamic_cast<audio::SyntheticAudioBackend &>(m_window->audio().engine().backend());
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                const QVector<float> out = windowDevice.recordedOutput();
                const qsizetype n = std::min<qsizetype>(out.size(), 48 * 400);
                return test::toneStrength(out.constData() + (out.size() - n), n, 600.0) > 0.1;
            }(),
            kWaitMs);

        // Per-user volume: silencing the speaker for this window only.
        m_window->audio().setUserVolume(speakerId, 0);
        QCOMPARE(m_window->audio().userVolume(speakerId), 0);
        QTest::qWait(400);
        windowDevice.clearRecording();
        QTest::qWait(300);
        const QVector<float> silenced = windowDevice.recordedOutput();
        QVERIFY(test::toneStrength(silenced, 600.0) < 0.01);
        m_window->audio().setUserVolume(speakerId, 100);

        // The window talks with Ctrl+T (push-to-talk mode has no key yet).
        speakerEngine.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!m_window->channelTree()->isUserTalking(speakerId), kWaitMs);
        windowDevice.setGenerator([](qint64 i) { return test::sine(i, 700.0); });
        QVERIFY(m_window->voiceStatusText().isEmpty());
        m_window->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
        QTest::keyClick(m_window.get(), Qt::Key_T, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(heardBySpeaker > 10, kWaitMs);
        QTRY_COMPARE_WITH_TIMEOUT(m_window->voiceStatusText(), QStringLiteral("Talking"), kWaitMs);
        QTest::keyClick(m_window.get(), Qt::Key_M, Qt::ControlModifier); // mute microphone
        QCOMPARE(m_window->voiceStatusText(), QStringLiteral("Microphone muted"));
        QTest::qWait(200);
        const int afterMute = heardBySpeaker;
        QTest::qWait(300);
        QVERIFY(heardBySpeaker <= afterMute + 1);
        QVERIFY(m_spoken.contains(QStringLiteral("Microphone muted")));
        speaker.disconnectFromServer();
    }
};

QTEST_MAIN(TestMainWindowOnline)
#include "tst_mainwindow_online.moc"
