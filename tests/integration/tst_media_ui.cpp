// SPDX-License-Identifier: GPL-3.0-only
// Streaming files and sharing computer audio from the real desktop window, against a
// real in-process server: who may stream, what listeners are told, pausing and
// stopping, muting other people's streams, and the warning when OpenTalk's own sound
// cannot be left out of shared computer audio.

#include "../unit/AudioTestUtil.h"

#include "opentalk/audio/AudioEngine.h"
#include "opentalk/client/Connection.h"
#include "opentalk/security/PasswordHash.h"
#include "opentalk/security/Tls.h"
#include "opentalk/server/Server.h"
#include "opentalk/server/ServerDatabase.h"
#include "opentalk/storage/AppPaths.h"
#include "opentalk/storage/ClientSettings.h"
#include "opentalk/storage/ServerProfiles.h"
#include "opentalk/storage/SettingsStore.h"
#include "opentalk/ui/AudioController.h"
#include "opentalk/ui/ChannelTree.h"
#include "opentalk/ui/MainWindow.h"

#include <QAction>
#include <QDataStream>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

using namespace opentalk;
using protocol::Role;

namespace {

const QString kDataDir = QStringLiteral(OPENTALK_TEST_DATA_DIR);
constexpr int kWaitMs = 10'000;

/// One person's desktop app, logged in to the test server.
struct App {
    QTemporaryDir dir;
    std::unique_ptr<storage::AppPaths> paths;
    std::unique_ptr<storage::SettingsStore> settings;
    std::unique_ptr<storage::ServerProfileStore> profiles;
    std::unique_ptr<ui::MainWindow> window;
    QStringList errors;
    QStringList spoken;

    ~App()
    {
        if (window) {
            window->connection().disconnectFromServer();
        }
    }

    QAction *action(const char *name) const { return window->findChild<QAction *>(QLatin1String(name)); }
    QString history() const { return window->historyText(); }
    audio::SyntheticAudioBackend &device() const
    {
        return dynamic_cast<audio::SyntheticAudioBackend &>(window->audio().engine().backend());
    }
    quint32 selfId() const { return window->connection().serverState().selfId(); }
    /// How strongly a tone was heard over the last 400 ms.
    double heard(double frequency) const
    {
        const QVector<float> out = device().recordedOutput();
        const qsizetype n = std::min<qsizetype>(out.size(), 48 * 400);
        return test::toneStrength(out.constData() + (out.size() - n), n, frequency);
    }
    /// The channel tree's accessible description of a user ("talking", "playing a file"...).
    QString description(const QString &userName) const
    {
        for (QTreeWidgetItemIterator it(window->channelTree()); *it != nullptr; ++it) {
            if ((*it)->text(0) == userName) {
                return (*it)->data(0, Qt::AccessibleDescriptionRole).toString();
            }
        }
        return {};
    }
};

/// A 48 kHz mono 16-bit WAV file holding a tone.
QString writeToneFile(const QString &path, double seconds, double frequency)
{
    const auto frames = static_cast<quint32>(seconds * 48000);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + frames * 2);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << quint16(1) << quint32(48000) << quint32(96000) << quint16(2) << quint16(16);
    out.writeRawData("data", 4);
    out << quint32(frames * 2);
    for (quint32 i = 0; i < frames; ++i) {
        out << static_cast<qint16>(std::lround(test::sine(i, frequency, 0.4) * 32767.0));
    }
    return path;
}

} // namespace

class TestMediaUi : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QSslConfiguration m_tls;
    std::unique_ptr<server::ServerDatabase> m_db;
    std::unique_ptr<server::Server> m_server;

    /// Opens a window and logs in. `loopback` makes "computer audio" (an 880 Hz tone)
    /// available, with or without leaving OpenTalk's own sound out.
    enum class Loopback { None, Excluding, Including };
    std::unique_ptr<App> login(const QString &name, bool guest = false, Loopback loopback = Loopback::None)
    {
        auto app = std::make_unique<App>();
        app->paths = std::make_unique<storage::AppPaths>(storage::AppPaths::resolve({}, app->dir.path()));
        if (!app->paths->ensureDirectories()) {
            return nullptr;
        }
        app->settings =
            std::make_unique<storage::SettingsStore>(storage::client::schema(), app->paths->clientSettingsFile());
        app->settings->load();
        app->profiles = std::make_unique<storage::ServerProfileStore>(app->paths->serverProfilesFile());
        auto backend = std::make_unique<audio::SyntheticAudioBackend>();
        if (loopback != Loopback::None) {
            backend->setLoopback([](qint64 i) { return test::sine(i, 880.0, 0.3); }, loopback == Loopback::Excluding);
        }
        app->window = std::make_unique<ui::MainWindow>(*app->settings, *app->paths, *app->profiles, std::move(backend));
        App *raw = app.get();
        app->window->setErrorReporter([raw](const QString &title, const QString &text) {
            raw->errors.append(title + QStringLiteral(": ") + text);
        });
        app->window->setAnnouncementHook([raw](const QString &text) { raw->spoken.append(text); });
        app->window->show();
        storage::ServerProfile profile;
        profile.name = QStringLiteral("Media test");
        profile.host = QStringLiteral("127.0.0.1");
        profile.port = m_server->serverPort();
        profile.userName = name;
        profile.guest = guest;
        profile.pinnedFingerprint = m_server->certificateFingerprint();
        app->window->connectToServer(profile, guest ? QString() : name + QStringLiteral("-password"), false);
        if (!QTest::qWaitFor(
                [raw] {
                    return raw->window->connection().state() == client::ConnectionState::Connected &&
                           raw->window->connection().isVoiceReady();
                },
                kWaitMs)) {
            return nullptr;
        }
        return app;
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
            m_dir.filePath(QStringLiteral("media-%1.db").arg(QString::fromLatin1(QTest::currentTestFunction())))));
        for (const QString &name : {QStringLiteral("alice"), QStringLiteral("bob")}) {
            const auto hash = security::hashPassword(name + QStringLiteral("-password"),
                                                     security::PasswordHashCost::minimumForTests());
            QVERIFY(hash && m_db->createAccount(name, *hash, Role::User));
        }
        server::ServerConfig config;
        config.address = QHostAddress::LocalHost;
        config.port = 0;
        config.tls = m_tls;
        config.session.serverName = QStringLiteral("Media test");
        config.allowGuests = true;
        config.database = m_db.get();
        config.passwordCost = security::PasswordHashCost::minimumForTests();
        config.shutdownGraceMs = 1'000;
        m_server = std::make_unique<server::Server>(config);
        QString error;
        QVERIFY2(m_server->start(&error), qPrintable(error));
    }

    void cleanup()
    {
        if (m_server) {
            QSignalSpy stopped(m_server.get(), &server::Server::stopped);
            m_server->stop();
            QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, kWaitMs);
        }
        m_server.reset();
        m_db.reset();
    }

    void streamingAFileFromTheWindow()
    {
        auto alice = login(QStringLiteral("alice"));
        auto bob = login(QStringLiteral("bob"));
        QVERIFY(alice && bob);
        bob->settings->setValue(storage::client::kAnnounceTalking, true);
        const QString path = writeToneFile(m_dir.filePath(QStringLiteral("Theme tune.wav")), 20.0, 660.0);
        QVERIFY(!path.isEmpty());

        QAction *stream = alice->action("streamFileAction");
        QVERIFY(stream && stream->isEnabled());
        QVERIFY(!alice->action("stopMediaAction")->isEnabled()); // nothing to stop yet
        alice->window->setMediaFileChooser([path] { return path; });
        stream->trigger();
        QVERIFY2(alice->errors.isEmpty(), qPrintable(alice->errors.join(QLatin1Char('\n'))));
        QVERIFY(alice->history().contains(QStringLiteral("Streaming Theme tune.wav, 0:20 long.")));
        QCOMPARE(alice->window->voiceStatusText(), QStringLiteral("Streaming Theme tune.wav"));
        QVERIFY(alice->action("stopMediaAction")->isEnabled());

        // Bob hears it, labelled as a file and never as Alice talking.
        const quint32 aliceId = alice->selfId();
        QTRY_VERIFY_WITH_TIMEOUT(bob->heard(660.0) > 0.05, kWaitMs);
        QTRY_COMPARE_WITH_TIMEOUT(bob->window->channelTree()->userActivity(aliceId),
                                  ui::AudioController::streamBit(audio::StreamType::Media), kWaitMs);
        QVERIFY(!bob->window->channelTree()->isUserTalking(aliceId));
        QCOMPARE(bob->description(QStringLiteral("alice")), QStringLiteral("playing a file"));
        QTRY_VERIFY_WITH_TIMEOUT(bob->spoken.contains(QStringLiteral("alice playing a file")), kWaitMs);
        QVERIFY(alice->heard(660.0) > 0.05); // Alice hears what she streams

        // Pause, ask what is playing, resume.
        alice->action("pauseMediaAction")->trigger();
        QCOMPARE(alice->window->voiceStatusText(), QStringLiteral("Paused Theme tune.wav"));
        QCOMPARE(alice->action("pauseMediaAction")->text(), QStringLiteral("&Resume streaming"));
        QTRY_VERIFY_WITH_TIMEOUT(bob->heard(660.0) < 0.005, kWaitMs);
        alice->action("mediaStatusAction")->trigger();
        QVERIFY(alice->window->statusText().startsWith(QStringLiteral("Paused Theme tune.wav, 0:")));
        QVERIFY(alice->window->statusText().endsWith(QStringLiteral(" of 0:20.")));
        alice->action("pauseMediaAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(bob->heard(660.0) > 0.05, kWaitMs);

        // Bob can mute files from others without muting voices.
        bob->action("muteIncomingMediaAction")->trigger();
        QVERIFY(bob->settings->value(storage::client::kMuteIncomingMedia).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(bob->heard(660.0) < 0.005, kWaitMs);
        bob->action("muteIncomingMediaAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(bob->heard(660.0) > 0.05, kWaitMs);

        alice->action("stopMediaAction")->trigger();
        QVERIFY(alice->history().contains(QStringLiteral("Stopped streaming Theme tune.wav.")));
        QVERIFY(alice->window->voiceStatusText().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(bob->window->channelTree()->userActivity(aliceId), 0, kWaitMs);

        // A file that cannot be played is explained.
        const QString bad = m_dir.filePath(QStringLiteral("notes.txt"));
        QFile text(bad);
        QVERIFY(text.open(QIODevice::WriteOnly));
        text.write("not music");
        text.close();
        alice->window->setMediaFileChooser([bad] { return bad; });
        stream->trigger();
        QCOMPARE(alice->errors.size(), 1);
        QVERIFY(alice->errors.first().contains(QStringLiteral("notes.txt cannot be streamed")));
    }

    void aFileThatEndsIsAnnounced()
    {
        auto alice = login(QStringLiteral("alice"));
        QVERIFY(alice);
        const QString path = writeToneFile(m_dir.filePath(QStringLiteral("Short.wav")), 0.5, 660.0);
        alice->window->setMediaFileChooser([path] { return path; });
        alice->action("streamFileAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(alice->history().contains(QStringLiteral("Finished streaming Short.wav.")), kWaitMs);
        QVERIFY(alice->window->voiceStatusText().isEmpty());
        QVERIFY(!alice->action("stopMediaAction")->isEnabled());
    }

    void guestsMayNotStream()
    {
        auto guest = login(QStringLiteral("Visitor"), true, Loopback::Excluding);
        QVERIFY(guest);
        QVERIFY(!guest->action("streamFileAction")->isEnabled());
        QVERIFY(guest->action("shareSystemAudioAction")->isVisible());
        QVERIFY(!guest->action("shareSystemAudioAction")->isEnabled());
        // Where computer audio cannot be captured at all, the command is not offered.
        auto alice = login(QStringLiteral("alice"));
        QVERIFY(alice);
        QVERIFY(!alice->action("shareSystemAudioAction")->isVisible());
    }

    void sharingComputerAudioWarnsWhenOwnSoundCannotBeLeftOut()
    {
        auto alice = login(QStringLiteral("alice"), false, Loopback::Including); // like Windows 10
        auto bob = login(QStringLiteral("bob"));
        QVERIFY(alice && bob);
        QAction *share = alice->action("shareSystemAudioAction");
        QVERIFY(share->isEnabled());
        share->trigger();
        QVERIFY2(alice->errors.isEmpty(), qPrintable(alice->errors.join(QLatin1Char('\n'))));
        QVERIFY(share->isChecked());
        QVERIFY(alice->history().contains(QStringLiteral("cannot leave OpenTalk's own sound out")));
        QCOMPARE(alice->window->voiceStatusText(), QStringLiteral("Sharing computer audio"));
        const quint32 aliceId = alice->selfId();
        QTRY_VERIFY_WITH_TIMEOUT(bob->heard(880.0) > 0.05, kWaitMs);
        QTRY_COMPARE_WITH_TIMEOUT(ui::ChannelTree::activityText(bob->window->channelTree()->userActivity(aliceId)),
                                  QStringLiteral("sharing computer audio"), kWaitMs);
        share->trigger();
        QVERIFY(!share->isChecked());
        QVERIFY(alice->history().contains(QStringLiteral("Stopped sharing computer audio.")));
        QTRY_VERIFY_WITH_TIMEOUT(bob->heard(880.0) < 0.005, kWaitMs);
    }

    void sharingIsQuietAboutWhatWorks()
    {
        auto alice = login(QStringLiteral("alice"), false, Loopback::Excluding); // like Windows 11
        QVERIFY(alice);
        alice->action("shareSystemAudioAction")->trigger();
        QVERIFY(alice->history().contains(QStringLiteral("except OpenTalk itself")));
        QVERIFY(!alice->history().contains(QStringLiteral("cannot leave")));
        // Disconnecting stops streaming, and says so.
        alice->window->connection().disconnectFromServer();
        QTRY_VERIFY_WITH_TIMEOUT(
            alice->history().contains(QStringLiteral("Streaming stopped because the connection closed.")), kWaitMs);
        QVERIFY(!alice->window->audio().isSharingSystemAudio());
    }
};

QTEST_MAIN(TestMediaUi)
#include "tst_media_ui.moc"
