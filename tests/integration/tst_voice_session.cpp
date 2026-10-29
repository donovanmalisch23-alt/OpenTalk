// SPDX-License-Identifier: GPL-3.0-only
// End-to-end voice: a real server and real clients on localhost, over encrypted UDP and
// over the TLS fallback, finishing with real audio engines on synthetic devices.

#include "../unit/AudioTestUtil.h"

#include "opentalk/audio/AudioEngine.h"
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

struct VoiceClient {
    client::Connection connection;
    QList<AudioData> heard;
    int connectedCount = 0;
    QHash<quint64, std::optional<Error>> results;
    QHash<quint64, std::optional<quint32>> resultIds;

    VoiceClient()
    {
        QObject::connect(&connection, &client::Connection::audioReceived,
                         [this](const AudioData &audio) { heard.append(audio); });
        QObject::connect(&connection, &client::Connection::connected, [this] { ++connectedCount; });
        QObject::connect(&connection, &client::Connection::requestFinished,
                         [this](quint64 id, const std::optional<Error> &e, std::optional<quint32> r) {
                             results.insert(id, e);
                             resultIds.insert(id, r);
                         });
    }

    quint32 id() const { return connection.serverState().selfId(); }

    bool succeeded(quint64 request)
    {
        return request != 0 && QTest::qWaitFor([&] { return results.contains(request); }, kWaitMs) &&
               !results.value(request).has_value();
    }

    void talk(int packets, quint16 firstSequence = 0)
    {
        for (int i = 0; i < packets; ++i) {
            QVERIFY(connection.sendAudio(AudioStream::Voice, static_cast<quint16>(firstSequence + i),
                                         QByteArray(60, static_cast<char>(i))));
            QTest::qWait(2); // a little pacing, like a real 20 ms stream but faster
        }
    }
};

} // namespace

class TestVoiceSession : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QSslConfiguration m_tls;
    std::unique_ptr<server::ServerDatabase> m_db;
    std::unique_ptr<server::Server> m_server;

    void startServer(bool enableUdp)
    {
        m_db = std::make_unique<server::ServerDatabase>();
        QVERIFY(m_db->open(
            m_dir.filePath(QStringLiteral("voice-%1.db").arg(QString::fromLatin1(QTest::currentTestFunction())))));
        for (const QString &name : {QStringLiteral("alice"), QStringLiteral("bob"), QStringLiteral("carol")}) {
            const auto hash = security::hashPassword(name, security::PasswordHashCost::minimumForTests());
            QVERIFY(hash && m_db->createAccount(name, *hash, Role::User));
        }
        server::ServerConfig config;
        config.address = QHostAddress::LocalHost;
        config.port = 0;
        config.tls = m_tls;
        config.database = m_db.get();
        config.passwordCost = security::PasswordHashCost::minimumForTests();
        config.shutdownGraceMs = 1'000;
        config.enableUdp = enableUdp;
        m_server = std::make_unique<server::Server>(config);
        QString error;
        QVERIFY2(m_server->start(&error), qPrintable(error));
        QCOMPARE(m_server->udpPort() != 0, enableUdp);
    }

    bool login(VoiceClient &c, const QString &name, bool allowUdp = true)
    {
        client::ConnectOptions o;
        o.host = QStringLiteral("127.0.0.1");
        o.port = m_server->serverPort();
        o.trust = security::ServerTrust::pinned(m_server->certificateFingerprint());
        o.credentials.mode = client::Credentials::Mode::Account;
        o.credentials.name = name;
        o.credentials.password = name;
        o.autoReconnect = false;
        o.allowUdp = allowUdp;
        c.connection.connectToServer(o);
        return QTest::qWaitFor([&] { return c.connection.isVoiceReady(); }, kWaitMs);
    }

    /// Moves `c` into a new temporary channel of its own.
    bool moveAway(VoiceClient &c, const QString &channelName)
    {
        CreateChannel request;
        request.parentId = kRootChannelId;
        request.name = channelName;
        const quint64 id = c.connection.createChannel(request);
        if (!c.succeeded(id)) {
            return false;
        }
        return c.succeeded(c.connection.joinChannel(c.resultIds.value(id).value_or(0)));
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

    void cleanup()
    {
        if (m_server && m_server->isListening()) {
            QSignalSpy stopped(m_server.get(), &server::Server::stopped);
            m_server->stop();
            QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, kWaitMs);
        }
        m_server.reset();
        m_db.reset();
    }

    void udpVoiceReachesOnlyTheSameChannel()
    {
        startServer(true);
        VoiceClient alice;
        VoiceClient bob;
        VoiceClient carol;
        QVERIFY(login(alice, QStringLiteral("alice")));
        QVERIFY(login(bob, QStringLiteral("bob")));
        QVERIFY(login(carol, QStringLiteral("carol")));
        QVERIFY(moveAway(carol, QStringLiteral("Elsewhere")));
        QTRY_VERIFY_WITH_TIMEOUT(alice.connection.isVoiceUsingUdp() && bob.connection.isVoiceUsingUdp(), kWaitMs);
        QCOMPARE(m_server->voiceStats().sentUdp, quint64{0}); // nothing sent yet

        alice.talk(50);
        QTRY_VERIFY_WITH_TIMEOUT(bob.heard.size() >= 45, kWaitMs); // UDP; localhost should lose nothing
        QTest::qWait(200);
        QVERIFY(carol.heard.isEmpty());
        QVERIFY(alice.heard.isEmpty()); // never your own voice
        QCOMPARE(bob.heard.first().userId, alice.id());
        QCOMPARE(bob.heard.first().payload, QByteArray(60, '\0'));
        QVERIFY(m_server->voiceStats().receivedUdp >= 45);
        QVERIFY(m_server->voiceStats().sentUdp >= 45);
        QCOMPARE(m_server->voiceStats().sentTunnel, quint64{0});

        // Bob follows Carol: now Carol hears him and Alice does not.
        const qsizetype heardBefore = carol.heard.size();
        QVERIFY(bob.succeeded(bob.connection.joinChannel(carol.connection.serverState().currentChannelId())));
        bob.talk(20);
        QTRY_VERIFY_WITH_TIMEOUT(carol.heard.size() >= heardBefore + 18, kWaitMs);
        QVERIFY(alice.heard.isEmpty());
    }

    void tunnelCarriesVoiceWhenUdpIsNotAllowed()
    {
        startServer(true);
        VoiceClient alice;
        VoiceClient bob;
        QVERIFY(login(alice, QStringLiteral("alice"), false));
        QVERIFY(login(bob, QStringLiteral("bob")));
        QTRY_VERIFY_WITH_TIMEOUT(bob.connection.isVoiceUsingUdp(), kWaitMs);
        QVERIFY(!alice.connection.isVoiceUsingUdp());

        alice.talk(30);
        QTRY_COMPARE_WITH_TIMEOUT(bob.heard.size(), 30, kWaitMs);
        bob.talk(30, 100);
        QTRY_COMPARE_WITH_TIMEOUT(alice.heard.size(), 30, kWaitMs); // TCP to alice is lossless
        // Bob's packets reach the server over UDP, which may reorder them, so compare sets.
        QSet<quint16> sequences;
        for (const AudioData &audio : std::as_const(alice.heard)) {
            sequences.insert(audio.sequence);
            QCOMPARE(audio.userId, bob.id());
        }
        QCOMPARE(sequences.size(), 30);
        QVERIFY(sequences.contains(100) && sequences.contains(129));
        QVERIFY(m_server->voiceStats().receivedTunnel >= 30);
        QVERIFY(m_server->voiceStats().sentTunnel >= 30);
    }

    void serverWithoutUdpUsesTheTunnel()
    {
        startServer(false);
        VoiceClient alice;
        VoiceClient bob;
        QVERIFY(login(alice, QStringLiteral("alice")));
        QVERIFY(login(bob, QStringLiteral("bob")));
        alice.talk(25);
        QTRY_COMPARE_WITH_TIMEOUT(bob.heard.size(), 25, kWaitMs);
        QVERIFY(!alice.connection.isVoiceUsingUdp());
        QCOMPARE(m_server->voiceStats().receivedUdp, quint64{0});
    }

    void voiceIsNotAcceptedBeforeLogin()
    {
        startServer(true);
        VoiceClient alice;
        QVERIFY(!alice.connection.sendAudio(AudioStream::Voice, 1, "x"));
    }

    void toneTravelsEndToEnd()
    {
        // Real capture -> Opus -> encrypted UDP -> server -> jitter buffer -> mixer.
        startServer(true);
        VoiceClient alice;
        VoiceClient bob;
        QVERIFY(login(alice, QStringLiteral("alice")));
        QVERIFY(login(bob, QStringLiteral("bob")));
        QTRY_VERIFY_WITH_TIMEOUT(alice.connection.isVoiceUsingUdp() && bob.connection.isVoiceUsingUdp(), kWaitMs);

        auto micBackend = std::make_unique<audio::SyntheticAudioBackend>([](qint64 i) { return test::sine(i, 523.0); });
        auto speakerBackend = std::make_unique<audio::SyntheticAudioBackend>();
        audio::SyntheticAudioBackend *speaker = speakerBackend.get();
        audio::AudioEngine talker(std::move(micBackend));
        audio::AudioEngine listener(std::move(speakerBackend));
        audio::AudioSettings settings;
        settings.mode = audio::TransmitMode::Continuous;
        settings.soundsEnabled = false;
        // Test tones are steady, which noise suppression is designed to remove.
        settings.noiseSuppression = false;
        talker.setSettings(settings);
        listener.setSettings(settings);
        QObject::connect(&talker, &audio::AudioEngine::packetReady, &alice.connection,
                         [&](audio::StreamType type, quint16 seq, const QByteArray &opus) {
                             alice.connection.sendAudio(static_cast<AudioStream>(type), seq, opus);
                         });
        QObject::connect(&bob.connection, &client::Connection::audioReceived, &listener, [&](const AudioData &audio) {
            listener.receivePacket(audio.userId, static_cast<audio::StreamType>(audio.stream), audio.sequence,
                                   audio.payload);
        });
        QSignalSpy talking(&listener, &audio::AudioEngine::talkingChanged);
        talker.setTransmitAllowed(true);
        listener.start();
        talker.start();
        const auto heard = [&] {
            const QVector<float> out = speaker->recordedOutput();
            const qsizetype n = std::min<qsizetype>(out.size(), 48 * 500);
            return test::toneStrength(out.constData() + (out.size() - n), n, 523.0);
        };
        QTRY_VERIFY_WITH_TIMEOUT(heard() > 0.1, kWaitMs);
        QVERIFY(!talking.isEmpty());
        QCOMPARE(talking.first().at(0).toUInt(), alice.id());
        talker.stop();
        listener.stop();
    }
};

QTEST_MAIN(TestVoiceSession)
#include "tst_voice_session.moc"
