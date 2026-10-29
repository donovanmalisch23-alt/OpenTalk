// SPDX-License-Identifier: GPL-3.0-only
// Two AudioEngines on synthetic devices, wired back to back as if over a network.

#include "AudioTestUtil.h"

#include "opentalk/audio/AudioEngine.h"

#include <QDataStream>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <chrono>
#include <thread>

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
    std::array<int, 3> packetsByType{}; ///< Indexed by StreamType.

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
        // Test tones are steady, which noise suppression is designed to remove.
        s.noiseSuppression = false;
        talker->setSettings(s);
        AudioSettings quiet;
        quiet.soundsEnabled = false;
        listener->setSettings(quiet);
        QObject::connect(talker.get(), &AudioEngine::packetReady, listener.get(),
                         [this](StreamType type, quint16 seq, const QByteArray &opus) {
                             ++packets;
                             ++packetsByType[static_cast<std::size_t>(type)];
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

    /// The same for what the talker's own speakers played.
    double talkerHeard(double frequency, int ms = 500) const
    {
        const QVector<float> out = talkerDevice->recordedOutput();
        const qsizetype n = std::min<qsizetype>(out.size(), 48 * ms);
        return toneStrength(out.constData() + (out.size() - n), n, frequency);
    }
};

/// A 48 kHz mono 16-bit WAV file holding a tone.
QString writeToneFile(const QTemporaryDir &dir, double seconds, double frequency)
{
    const QString path = dir.filePath(QStringLiteral("tone %1.wav").arg(frequency));
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
        out << static_cast<qint16>(std::lround(sine(i, frequency, 0.4) * 32767.0));
    }
    return path;
}

double toDb(double ratio)
{
    return 20.0 * std::log10(std::max(ratio, 1e-9));
}

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

    void mediaFileIsStreamedAtNormalSpeed()
    {
        QTemporaryDir dir;
        const QString path = writeToneFile(dir, 1.5, 660.0);
        Pair p(TransmitMode::PushToTalk); // the microphone stays quiet
        QSignalSpy finished(p.talker.get(), &AudioEngine::mediaFinished);
        QString error;
        QVERIFY2(p.talker->startMedia(path, &error), qPrintable(error));
        QCOMPARE(p.talker->mediaStatus().title, QStringLiteral("tone 660.wav"));
        QVERIFY(std::abs(p.talker->mediaStatus().durationMs - 1500) <= 20);
        QTest::qWait(500);
        // Real time: about 25 frames after half a second, not the whole file at once.
        const int early = p.packetsByType[static_cast<std::size_t>(StreamType::Media)];
        QVERIFY2(early >= 15 && early <= 40, qPrintable(QString::number(early)));
        QVERIFY(p.talker->mediaStatus().positionMs >= 300);
        QTRY_VERIFY_WITH_TIMEOUT(p.heard(660.0, 300) > 0.05, 3000); // the listener hears it...
        QVERIFY(p.talkerHeard(660.0, 300) > 0.05);                  // ...and so does the sender
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 4000);
        QCOMPARE(finished.first().at(0).toString(), QStringLiteral("tone 660.wav"));
        QVERIFY(!p.talker->mediaStatus().active);
        const int total = p.packetsByType[static_cast<std::size_t>(StreamType::Media)];
        QVERIFY2(total >= 72 && total <= 76, qPrintable(QString::number(total))); // 1.5 s of 20 ms frames
        QCOMPARE(p.packetsByType[static_cast<std::size_t>(StreamType::Voice)], 0);
    }

    void mediaPausesResumesAndStops()
    {
        QTemporaryDir dir;
        const QString path = writeToneFile(dir, 10.0, 660.0);
        Pair p(TransmitMode::PushToTalk);
        QSignalSpy finished(p.talker.get(), &AudioEngine::mediaFinished);
        QVERIFY(p.talker->startMedia(path, nullptr));
        auto media = [&p] { return p.packetsByType[static_cast<std::size_t>(StreamType::Media)]; };
        QTRY_VERIFY_WITH_TIMEOUT(media() > 10, 3000);
        p.talker->setMediaPaused(true);
        QVERIFY(p.talker->mediaStatus().paused);
        QTest::qWait(100);
        const int paused = media();
        const qint64 position = p.talker->mediaStatus().positionMs;
        QTest::qWait(300);
        QVERIFY(media() <= paused + 1);
        QVERIFY(p.talker->mediaStatus().positionMs <= position + 20);
        p.talker->setMediaPaused(false);
        QTRY_VERIFY_WITH_TIMEOUT(media() > paused + 10, 3000);
        p.talker->skipMedia(5000);
        QTRY_VERIFY_WITH_TIMEOUT(p.talker->mediaStatus().positionMs > 5000, 2000);
        p.talker->stopMedia();
        QVERIFY(!p.talker->mediaStatus().active);
        QTest::qWait(100);
        const int stopped = media();
        QTest::qWait(300);
        QVERIFY(media() <= stopped + 1);
        QCOMPARE(finished.count(), 0); // stopping is not finishing
        QString error;
        QVERIFY(!p.talker->startMedia(dir.filePath(QStringLiteral("missing.wav")), &error));
        QVERIFY(!error.isEmpty());
    }

    void pausingIsNeverUndoneByTheAudioThread()
    {
        // Found in CI: the audio thread could publish "playing" just after the window
        // paused, so the menu kept offering "Pause" and pressing it again paused instead
        // of resuming. Toggled at many points in the audio thread's 10 ms cycle.
        QTemporaryDir dir;
        const QString path = writeToneFile(dir, 30.0, 660.0);
        Pair p(TransmitMode::PushToTalk);
        QVERIFY(p.talker->startMedia(path, nullptr));
        QTRY_VERIFY_WITH_TIMEOUT(p.talker->mediaStatus().positionMs > 100, 3000);
        bool paused = false;
        for (int i = 0; i < 400; ++i) {
            paused = !paused;
            p.talker->setMediaPaused(paused);
            std::this_thread::sleep_for(std::chrono::microseconds(250 * (i % 11)));
            QVERIFY2(p.talker->mediaStatus().paused == paused, qPrintable(QString::number(i)));
        }
        p.talker->stopMedia();
    }

    void listenersCanMuteMediaButNotVoice()
    {
        QTemporaryDir dir;
        const QString path = writeToneFile(dir, 10.0, 660.0);
        Pair p(TransmitMode::Continuous); // 440 Hz voice and 660 Hz media together
        QVERIFY(p.talker->startMedia(path, nullptr));
        QTRY_VERIFY_WITH_TIMEOUT(p.heard(660.0) > 0.05 && p.heard(440.0) > 0.05, 5000);
        p.listener->setIncomingStreamMuted(StreamType::Media, true);
        QVERIFY(p.listener->isIncomingStreamMuted(StreamType::Media));
        QTRY_VERIFY_WITH_TIMEOUT(p.heard(660.0, 300) < 0.005, 3000);
        QVERIFY(p.heard(440.0, 300) > 0.05);
        p.listener->setIncomingStreamMuted(StreamType::Voice, true); // has no effect
        QVERIFY(!p.listener->isIncomingStreamMuted(StreamType::Voice));
    }

    void computerAudioIsSharedWithoutOwnSound()
    {
        Pair p(TransmitMode::PushToTalk);
        // Windows 11: OpenTalk's own sound can be left out of the capture.
        p.talkerDevice->setLoopback([](qint64 i) { return sine(i, 880.0, 0.3); }, true);
        QVERIFY(p.talker->canShareSystemAudio());
        QString error;
        bool excludes = false;
        QVERIFY2(p.talker->startSystemAudio(&error, &excludes), qPrintable(error));
        QVERIFY(excludes);
        QVERIFY(p.talker->isSharingSystemAudio());
        QTRY_VERIFY_WITH_TIMEOUT(p.heard(880.0) > 0.05, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(p.packetsByType[static_cast<std::size_t>(StreamType::SystemAudio)] > 10, 2000);
        QCOMPARE(p.packetsByType[static_cast<std::size_t>(StreamType::Voice)], 0);
        p.talker->stopSystemAudio();
        QVERIFY(!p.talker->isSharingSystemAudio());
        QTRY_VERIFY_WITH_TIMEOUT(p.heard(880.0, 300) < 0.005, 3000);

        // If the shared device goes away, sharing stops and says so.
        QSignalSpy stopped(p.talker.get(), &AudioEngine::systemAudioStopped);
        QVERIFY(p.talker->startSystemAudio(&error, &excludes));
        p.talkerDevice->simulateLoopbackFailure();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 2000);
        QVERIFY(!p.talker->isSharingSystemAudio());
        QVERIFY(!stopped.first().at(0).toString().isEmpty());
    }

    void oneProgramCanBeSharedAlone()
    {
        Pair p(TransmitMode::PushToTalk);
        p.talkerDevice->setLoopback([](qint64 i) { return sine(i, 880.0, 0.3); }, true);
        p.talkerDevice->setProgramLoopback(
            [](quint32 processId, qint64 i) { return processId == 42 ? sine(i, 660.0, 0.3) : 0.0f; });
        QString error;
        QVERIFY2(p.talker->startProgramAudio(42, &error), qPrintable(error));
        QVERIFY(p.talker->isSharingSystemAudio());
        QTRY_VERIFY_WITH_TIMEOUT(p.heard(660.0) > 0.05, 5000);
        QVERIFY(p.heard(880.0) < 0.005); // only that program
        QTRY_VERIFY_WITH_TIMEOUT(p.packetsByType[static_cast<std::size_t>(StreamType::SystemAudio)] > 10, 2000);
        p.talker->stopSystemAudio();
        QVERIFY(!p.talker->isSharingSystemAudio());

        // Like Windows 10, where one program cannot be captured on its own.
        p.talkerDevice->setLoopback([](qint64 i) { return sine(i, 880.0, 0.3); }, false);
        QVERIFY(!p.talker->startProgramAudio(42, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!p.talker->isSharingSystemAudio());
    }

    void ownSoundIsRemovedWhenItCannotBeLeftOut()
    {
        // Windows 10: the capture holds everything the computer plays, including the
        // voices OpenTalk plays. Those must not be sent back.
        Pair p(TransmitMode::PushToTalk);
        p.talkerDevice->setLoopback([](qint64 i) { return sine(i, 880.0, 0.3); }, false);
        auto other = std::make_unique<AudioEngine>(
            std::make_unique<SyntheticAudioBackend>([](qint64 i) { return sine(i, 523.25, 0.4); }));
        AudioSettings s;
        s.mode = TransmitMode::Continuous;
        s.noiseSuppression = false;
        other->setSettings(s);
        AudioEngine *talker = p.talker.get();
        QObject::connect(other.get(), &AudioEngine::packetReady, talker,
                         [talker](StreamType type, quint16 seq, const QByteArray &opus) {
                             talker->receivePacket(9, type, seq, opus);
                         });
        other->setTransmitAllowed(true);
        other->start();
        QTRY_VERIFY_WITH_TIMEOUT(p.talkerHeard(523.25) > 0.1, 5000); // the other person is heard

        QString error;
        bool excludes = true;
        QVERIFY2(p.talker->startSystemAudio(&error, &excludes), qPrintable(error));
        QVERIFY(!excludes);
        QTRY_VERIFY_WITH_TIMEOUT(p.heard(880.0) > 0.05, 5000);
        QTest::qWait(4000); // let the canceller learn
        const double shared = p.heard(880.0, 1000);
        const double leaked = p.heard(523.25, 1000);
        const double played = p.talkerHeard(523.25, 1000);
        qInfo("computer audio %.1f dB, own sound sent back %.1f dB below what was played", toDb(shared / 0.15),
              toDb(played / std::max(leaked, 1e-9)));
        QVERIFY2(toDb(played / std::max(leaked, 1e-9)) >= 20.0,
                 qPrintable(QStringLiteral("own sound only %1 dB down").arg(toDb(played / leaked))));
        QVERIFY(toDb(shared / 0.15) > -6.0); // and what was meant to be shared is kept
        other->stop();
    }

    void sharingNeedsSupport()
    {
        auto backend = std::make_unique<SyntheticAudioBackend>();
        AudioEngine engine(std::move(backend));
        engine.start();
        QVERIFY(!engine.canShareSystemAudio());
        QString error;
        QVERIFY(!engine.startSystemAudio(&error));
        QVERIFY(error.contains(QStringLiteral("not supported")));
        QVERIFY(!engine.isSharingSystemAudio());
    }
};

QTEST_GUILESS_MAIN(TestAudioEngine)
#include "tst_audio_engine.moc"
