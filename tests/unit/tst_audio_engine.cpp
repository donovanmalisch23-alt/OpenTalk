// SPDX-License-Identifier: GPL-3.0-only
// Two AudioEngines on synthetic devices, wired back to back as if over a network.

#include "AudioTestUtil.h"

#include "opentalk/audio/AudioEngine.h"

#include <QSignalSpy>
#include <QTest>

using namespace opentalk::audio;
using opentalk::test::sine;
using opentalk::test::toneStrength;

namespace {

struct Pair {
    SyntheticAudioBackend *talkerDevice;
    SyntheticAudioBackend *listenerDevice;
    std::unique_ptr<AudioEngine> talker;
    std::unique_ptr<AudioEngine> listener;
    int packets = 0;

    explicit Pair(TransmitMode mode, std::function<float(qint64)> microphone = [](qint64 i) { return sine(i, 440.0); })
    {
        auto talkerBackend = std::make_unique<SyntheticAudioBackend>(std::move(microphone));
        auto listenerBackend = std::make_unique<SyntheticAudioBackend>();
        talkerDevice = talkerBackend.get();
        listenerDevice = listenerBackend.get();
        talker = std::make_unique<AudioEngine>(std::move(talkerBackend));
        listener = std::make_unique<AudioEngine>(std::move(listenerBackend));
        AudioSettings s;
        s.mode = mode;
        s.voiceActivationThresholdDb = -30.0f;
        s.pushToTalkSounds = false;
        talker->setSettings(s);
        AudioSettings quiet;
        quiet.soundsEnabled = false;
        listener->setSettings(quiet);
        QObject::connect(talker.get(), &AudioEngine::packetReady, listener.get(),
                         [this](StreamType type, quint16 seq, const QByteArray &opus) {
                             ++packets;
                             listener->receivePacket(7, type, seq, opus);
                         });
        talker->setTransmitAllowed(true);
        talker->start();
        listener->start();
    }

    /// Tone level in the most recent `ms` of what the listener heard.
    double heard(double frequency, int ms = 500) const
    {
        const QVector<float> out = listenerDevice->recordedOutput();
        const qsizetype n = std::min<qsizetype>(out.size(), 48 * ms);
        return toneStrength(out.constData() + (out.size() - n), n, frequency);
    }
};

} // namespace

class TestAudioEngine : public QObject {
    Q_OBJECT

private slots:
    void voiceTravelsFromMicrophoneToSpeakers()
    {
        Pair p(TransmitMode::Continuous);
        QSignalSpy talking(p.listener.get(), &AudioEngine::talkingChanged);
        QTRY_VERIFY_WITH_TIMEOUT(p.heard(440.0) > 0.1, 5000);
        QVERIFY(p.heard(1000.0) < 0.02);
        QVERIFY(!talking.isEmpty());
        QCOMPARE(talking.first().at(0).toUInt(), 7u);
        QVERIFY(talking.first().at(2).toBool());
        QVERIFY(p.talker->isTransmitting());
        QVERIFY(p.talker->inputLevelDb() > -10.0f);
    }

    void pushToTalkGatesTransmission()
    {
        Pair p(TransmitMode::PushToTalk);
        QSignalSpy transmitting(p.talker.get(), &AudioEngine::transmittingChanged);
        QTest::qWait(400);
        QCOMPARE(p.packets, 0);
        p.talker->setPushToTalkPressed(true);
        QTRY_VERIFY_WITH_TIMEOUT(p.packets > 10, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(p.heard(440.0) > 0.1, 5000);
        p.talker->setPushToTalkPressed(false);
        QTRY_VERIFY_WITH_TIMEOUT(!p.talker->isTransmitting(), 2000);
        const int afterRelease = p.packets;
        QTest::qWait(300);
        QVERIFY(p.packets <= afterRelease + 1);
        QCOMPARE(transmitting.size(), 2); // on, then off
        // "Toggle transmit" keeps sending without holding the key.
        p.talker->setTransmitLatched(true);
        QTRY_VERIFY_WITH_TIMEOUT(p.packets > afterRelease + 10, 3000);
    }

    void muteAndDisconnectedStopSending()
    {
        Pair p(TransmitMode::Continuous);
        QTRY_VERIFY_WITH_TIMEOUT(p.packets > 5, 3000);
        p.talker->setMicrophoneMuted(true);
        QTest::qWait(100);
        int before = p.packets;
        QTest::qWait(300);
        QVERIFY(p.packets <= before + 1);
        p.talker->setMicrophoneMuted(false);
        p.talker->setTransmitAllowed(false); // not connected
        QTest::qWait(100);
        before = p.packets;
        QTest::qWait(300);
        QVERIFY(p.packets <= before + 1);
    }

    void deafenAndUserVolumeSilenceOthers()
    {
        Pair p(TransmitMode::Continuous);
        QTRY_VERIFY_WITH_TIMEOUT(p.heard(440.0) > 0.1, 5000);
        p.listener->setDeafened(true);
        QTRY_VERIFY_WITH_TIMEOUT(p.heard(440.0, 300) < 0.005, 3000);
        p.listener->setDeafened(false);
        QTRY_VERIFY_WITH_TIMEOUT(p.heard(440.0) > 0.1, 3000);
        p.listener->setUserGain(7, 0.0f);
        QTRY_VERIFY_WITH_TIMEOUT(p.heard(440.0, 300) < 0.005, 3000);
    }

    void voiceActivationFollowsTheLevel()
    {
        std::atomic<bool> speaking{false};
        Pair p(TransmitMode::VoiceActivation,
               [&speaking](qint64 i) { return speaking ? sine(i, 440.0) : sine(i, 440.0, 0.001); });
        QTest::qWait(500);
        QCOMPARE(p.packets, 0); // a quiet room does not transmit
        speaking = true;
        QTRY_VERIFY_WITH_TIMEOUT(p.talker->isTransmitting(), 2000);
        speaking = false;
        QTRY_VERIFY_WITH_TIMEOUT(!p.talker->isTransmitting(), 2000); // after the hangover
    }

    void eventSoundsPlay()
    {
        auto backend = std::make_unique<SyntheticAudioBackend>();
        SyntheticAudioBackend *device = backend.get();
        AudioEngine engine(std::move(backend));
        engine.start();
        QTest::qWait(100);
        engine.playSound(EventSound::ChannelMessage); // 1046.5 Hz
        QTRY_VERIFY_WITH_TIMEOUT(toneStrength(device->recordedOutput(), 1046.5) > 0.002, 3000);
    }

    void recoversWhenADeviceDisappears()
    {
        auto backend = std::make_unique<SyntheticAudioBackend>();
        SyntheticAudioBackend *device = backend.get();
        AudioEngine engine(std::move(backend));
        QSignalSpy problems(&engine, &AudioEngine::deviceProblem);
        QSignalSpy started(&engine, &AudioEngine::devicesStarted);
        engine.start();
        QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, 2000);
        device->simulateFailure();
        QTRY_COMPARE_WITH_TIMEOUT(problems.count(), 1, 6000);
        QTRY_COMPARE_WITH_TIMEOUT(started.count(), 2, 8000); // reopened automatically
        QVERIFY(engine.isRunning());
    }
};

QTEST_GUILESS_MAIN(TestAudioEngine)
#include "tst_audio_engine.moc"
