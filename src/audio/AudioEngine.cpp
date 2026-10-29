// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/audio/AudioEngine.h"

#include "opentalk/audio/Dsp.h"
#include "opentalk/audio/JitterBuffer.h"
#include "opentalk/audio/MediaFile.h"
#include "opentalk/audio/OpusCodec.h"
#include "opentalk/audio/RingBuffer.h"
#include "opentalk/audio/VoiceEffects.h"
#include "opentalk/audio/VoiceProcessing.h"

#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>

#include <array>
#include <deque>
#include <map>
#include <vector>

namespace opentalk::audio {
namespace {

constexpr int kTickMs = 5;
constexpr int kPlaybackTargetFrames = 2; ///< Mixed audio kept ready for the device (40 ms).
constexpr int kRetryMs = 3000;
constexpr qint64 kStreamIdleDropMs = 30'000;
/// A capture gap this long means the microphone stalled; the echo reference is resynchronised.
constexpr qint64 kCaptureGapMs = 100;
/// If the worker was held up for longer, a playing file skips ahead instead of sending a burst.
constexpr qint64 kMediaMaxLagMs = 200;
constexpr std::size_t kMonitorMaxFrames = 10;

quint64 streamKey(quint32 sender, StreamType type)
{
    return (static_cast<quint64>(sender) << 8) | static_cast<quint8>(type);
}

struct IncomingPacket {
    quint32 sender;
    StreamType type;
    quint16 sequence;
    QByteArray opus;
};

} // namespace

class AudioEngine::Worker : public QObject {
public:
    Worker(AudioEngine &engine, AudioBackend &backend)
        : m_engine(engine)
        , m_backend(backend)
        , m_capture(kSampleRate)
        , m_playback(kSampleRate)
        , m_loopback(kSampleRate)
        , m_encoder(OpusEncoderWrapper::Content::Voice, 32000)
        , m_mediaEncoder(OpusEncoderWrapper::Content::Music, 64000)
        , m_systemEncoder(OpusEncoderWrapper::Content::Music, 64000)
    {
        m_frame.resize(kFrameSamples);
        m_mix.resize(kFrameSamples);
        m_decoded.resize(kFrameSamples);
        m_mediaFrame.resize(kFrameSamples);
        m_systemFrame.resize(kFrameSamples);
    }

    void begin()
    {
        m_timer = new QTimer(this);
        m_timer->setTimerType(Qt::PreciseTimer);
        m_timer->setInterval(kTickMs);
        connect(m_timer, &QTimer::timeout, this, [this] { tick(); });
        m_clock.start();
        openDevices();
        m_timer->start();
    }

    void end()
    {
        if (m_timer != nullptr) {
            m_timer->stop();
        }
        stopSystemAudio();
        m_backend.stop();
        m_engine.m_running = false;
    }

    bool startSystemAudio(QString *errorMessage, bool *excludesOwnSound)
    {
        stopSystemAudio();
        if (!m_backend.supportsLoopback()) {
            *errorMessage = QStringLiteral("sharing computer audio is not supported on this system");
            return false;
        }
        m_loopback.clear();
        QString exclusionError;
        if (m_backend.startLoopback(m_loopback, true, &exclusionError)) {
            *excludesOwnSound = true;
        } else {
            // Capture everything and take out what OpenTalk itself plays (other people's
            // voices), which would otherwise go back to them as an echo. The reference is
            // the exact signal sent to the speakers, so this works far better than it does
            // for a microphone, but loud passages can still leak through.
            if (!m_backend.startLoopback(m_loopback, false, errorMessage)) {
                return false;
            }
            m_loopbackCanceller = std::make_unique<EchoCanceller>(300);
            *excludesOwnSound = false;
        }
        m_engine.m_sharingSystemAudio = true;
        return true;
    }

    bool startProgramAudio(quint32 processId, QString *errorMessage)
    {
        stopSystemAudio();
        if (!m_backend.supportsLoopback()) {
            *errorMessage = QStringLiteral("sharing computer audio is not supported on this system");
            return false;
        }
        m_loopback.clear();
        if (!m_backend.startProgramLoopback(m_loopback, processId, errorMessage)) {
            return false;
        }
        m_engine.m_sharingSystemAudio = true;
        return true;
    }

    void stopSystemAudio()
    {
        m_backend.stopLoopback();
        m_loopbackCanceller.reset();
        m_engine.m_sharingSystemAudio = false;
    }

    // ------------------------------------------------- state shared with the GUI thread
    QMutex mutex;
    AudioSettings settings;
    bool settingsChanged = true;
    bool devicesChanged = false;
    std::vector<IncomingPacket> incoming;
    QHash<quint32, float> userGains;
    std::vector<EventSound> soundRequests;
    SoundSet sounds;
    bool resetRequested = false;
    std::unique_ptr<MediaFile> pendingMedia;
    bool mediaStopRequested = false;
    qint64 mediaSkipMs = 0;
    MediaStatus mediaSnapshot;
    std::atomic<bool> transmitAllowed{false};
    std::atomic<bool> pushToTalk{false};
    std::atomic<bool> mediaPaused{false};
    std::array<std::atomic<bool>, 3> mutedStreams{}; ///< Indexed by StreamType.

private:
    struct Stream {
        JitterBuffer jitter;
        std::unique_ptr<OpusDecoderWrapper> decoder = std::make_unique<OpusDecoderWrapper>();
        qint64 lastPacketMs = 0;
        bool talking = false;
    };
    struct ActiveSound {
        const QVector<float> *samples;
        qsizetype position;
    };

    void openDevices()
    {
        QString error;
        AudioSettings s;
        {
            QMutexLocker lock(&mutex);
            s = settings;
            devicesChanged = false;
        }
        m_backend.stop();
        m_capture.clear();
        m_playback.clear();
        m_processor.resync();
        const bool ok = m_backend.start(s.inputDeviceId, s.outputDeviceId, m_capture, m_playback, &error);
        m_engine.m_running = ok;
        if (ok) {
            if (!error.isEmpty()) {
                emit m_engine.deviceProblem(error); // started, but without a microphone
            } else {
                emit m_engine.devicesStarted();
            }
            m_reportedFailure = false;
        } else if (!m_reportedFailure) {
            emit m_engine.deviceProblem(error);
            m_reportedFailure = true;
        }
        m_lastOpenAttemptMs = m_clock.elapsed();
    }

    void tick()
    {
        const qint64 now = m_clock.elapsed();
        AudioSettings s;
        std::vector<IncomingPacket> packets;
        std::vector<EventSound> requests;
        QHash<quint32, float> gains;
        bool reopen = false;
        qint64 skip = 0;
        {
            QMutexLocker lock(&mutex);
            s = settings;
            packets.swap(incoming);
            requests.swap(soundRequests);
            gains = userGains;
            reopen = devicesChanged;
            if (settingsChanged) {
                m_encoder.setBitrate(s.bitrate);
                m_vad.setThreshold(s.voiceActivationThresholdDb);
                m_processor.setOptions({s.noiseSuppression, -30, s.autoGain, s.echoCancellation});
                m_effects.setOptions({s.highPass, s.bassDb, s.trebleDb, s.pitchSemitones});
                m_mediaEncoder.setBitrate(s.mediaBitrate);
                m_systemEncoder.setBitrate(s.mediaBitrate);
                settingsChanged = false;
            }
            if (mediaStopRequested) {
                m_media.reset();
                m_monitor.clear();
                mediaStopRequested = false;
            }
            if (pendingMedia) {
                m_media = std::move(pendingMedia);
                m_monitor.clear();
                m_mediaNextMs = now;
            }
            skip = mediaSkipMs;
            mediaSkipMs = 0;
            if (resetRequested) {
                for (const auto &[key, stream] : m_streams) {
                    if (stream.talking) {
                        emit m_engine.talkingChanged(static_cast<quint32>(key >> 8),
                                                     static_cast<StreamType>(key & 0xff), false);
                    }
                }
                m_streams.clear();
                resetRequested = false;
            }
        }

        // Recover from unplugged devices, and apply device changes from Settings.
        if (reopen || (m_backend.hasFailed() && now - m_lastOpenAttemptMs > kRetryMs) ||
            (!m_engine.m_running && now - m_lastOpenAttemptMs > kRetryMs)) {
            if (m_backend.hasFailed() && !m_reportedFailure) {
                emit m_engine.deviceProblem(QStringLiteral("an audio device stopped working"));
                m_reportedFailure = true;
            }
            openDevices();
        }

        for (IncomingPacket &p : packets) {
            Stream &stream = m_streams[streamKey(p.sender, p.type)];
            stream.jitter.insert(p.sequence, p.opus, now);
            stream.lastPacketMs = now;
        }
        for (EventSound sound : requests) {
            const QVector<float> &samples = sounds[static_cast<std::size_t>(sound)];
            if (s.soundsEnabled && !samples.isEmpty() && m_activeSounds.size() < 8) {
                m_activeSounds.push_back({&samples, 0});
            }
        }

        if (skip != 0 && m_media) {
            m_media->seekMs(m_media->positionMs() + skip);
            m_monitor.clear();
        }

        processCapture(s, now);
        processMedia(s, now);
        processSystemAudio(s);
        processPlayback(s, gains, now);
    }

    void processCapture(const AudioSettings &s, qint64 now)
    {
        if (m_capture.available() >= static_cast<std::size_t>(kFrameSamples)) {
            if (now - m_lastCaptureMs > kCaptureGapMs) {
                m_processor.resync();
            }
            m_lastCaptureMs = now;
        }
        while (m_capture.available() >= static_cast<std::size_t>(kFrameSamples)) {
            m_capture.read(m_frame.data(), m_frame.size());
            applyGain(m_frame, s.inputGain);
            // Always, not only while talking, so the processing stays adapted.
            m_processor.process(m_frame);
            if (m_effects.isActive()) {
                m_effects.process(m_frame);
            }
            const float level = levelDbfs(m_frame);
            m_engine.m_inputLevel = level;
            const bool voice = m_vad.update(level);
            bool transmit = false;
            switch (s.mode) {
            case TransmitMode::PushToTalk:
                transmit = pushToTalk.load() || m_engine.m_latched.load();
                break;
            case TransmitMode::VoiceActivation:
                transmit = voice;
                break;
            case TransmitMode::Continuous:
                transmit = true;
                break;
            }
            transmit = transmit && !m_engine.m_muted.load() && transmitAllowed.load();
            if (transmit != m_engine.m_transmitting.load()) {
                m_engine.m_transmitting = transmit;
                emit m_engine.transmittingChanged(transmit);
                if (s.mode == TransmitMode::PushToTalk && s.pushToTalkSounds && s.soundsEnabled) {
                    const auto &click =
                        sounds[static_cast<std::size_t>(transmit ? EventSound::TransmitOn : EventSound::TransmitOff)];
                    if (!click.isEmpty()) {
                        m_activeSounds.push_back({&click, 0});
                    }
                }
            }
            if (transmit) {
                softClip(m_frame);
                const QByteArray packet = m_encoder.encode(m_frame);
                if (!packet.isEmpty()) {
                    emit m_engine.packetReady(StreamType::Voice, m_sequence++, packet);
                }
            }
        }
    }

    void processMedia(const AudioSettings &s, qint64 now)
    {
        if (!m_media) {
            return;
        }
        if (mediaPaused.load()) {
            m_mediaNextMs = now; // carry on from here when resumed, without catching up
            publishMediaStatus();
            return;
        }
        if (now - m_mediaNextMs > kMediaMaxLagMs) {
            m_mediaNextMs = now;
        }
        while (m_mediaNextMs <= now) {
            m_mediaNextMs += kFrameMs;
            const std::size_t got = m_media->read(m_mediaFrame);
            if (got > 0) {
                std::fill(m_mediaFrame.begin() + static_cast<std::ptrdiff_t>(got), m_mediaFrame.end(), 0.0f);
                applyGain(m_mediaFrame, s.mediaGain);
                softClip(m_mediaFrame);
                if (transmitAllowed.load()) {
                    const QByteArray packet = m_mediaEncoder.encode(m_mediaFrame);
                    if (!packet.isEmpty()) {
                        emit m_engine.packetReady(StreamType::Media, m_mediaSequence++, packet);
                    }
                }
                if (s.mediaMonitor) {
                    m_monitor.emplace_back(m_mediaFrame);
                    while (m_monitor.size() > kMonitorMaxFrames) {
                        m_monitor.pop_front();
                    }
                }
            }
            if (got < static_cast<std::size_t>(kFrameSamples)) {
                finishMedia();
                return;
            }
        }
        publishMediaStatus();
    }

    void publishMediaStatus()
    {
        // `paused` is read again under the lock: the window may have paused or resumed
        // since this frame started, and must not see its own request undone.
        QMutexLocker lock(&mutex);
        if (!pendingMedia && !mediaStopRequested) {
            mediaSnapshot = {true, mediaPaused.load(), m_media->title(), m_media->positionMs(), m_media->durationMs()};
        }
    }

    void finishMedia()
    {
        const QString title = m_media->title();
        m_media.reset();
        {
            QMutexLocker lock(&mutex);
            if (!pendingMedia) {
                mediaSnapshot = {};
                mediaPaused = false;
            }
        }
        emit m_engine.mediaFinished(title);
    }

    void processSystemAudio(const AudioSettings &s)
    {
        if (!m_engine.m_sharingSystemAudio.load()) {
            return;
        }
        if (m_backend.hasLoopbackFailed()) {
            stopSystemAudio();
            emit m_engine.systemAudioStopped(QStringLiteral("the sound device being shared stopped working"));
            return;
        }
        while (m_loopback.available() >= static_cast<std::size_t>(kFrameSamples)) {
            m_loopback.read(m_systemFrame.data(), m_systemFrame.size());
            if (m_loopbackCanceller) {
                m_loopbackCanceller->process(m_systemFrame);
            }
            applyGain(m_systemFrame, s.mediaGain);
            softClip(m_systemFrame);
            if (transmitAllowed.load()) {
                const QByteArray packet = m_systemEncoder.encode(m_systemFrame);
                if (!packet.isEmpty()) {
                    emit m_engine.packetReady(StreamType::SystemAudio, m_systemSequence++, packet);
                }
            }
        }
    }

    void processPlayback(const AudioSettings &s, const QHash<quint32, float> &gains, qint64 now)
    {
        if (!m_engine.m_running.load()) {
            return;
        }
        const bool deafened = m_engine.m_deafened.load();
        QByteArray payload;
        QByteArray fec;
        while (m_playback.available() < static_cast<std::size_t>(kPlaybackTargetFrames) * kFrameSamples) {
            std::fill(m_mix.begin(), m_mix.end(), 0.0f);
            for (auto it = m_streams.begin(); it != m_streams.end();) {
                Stream &stream = it->second;
                const auto result = stream.jitter.pop(&payload, &fec);
                const quint32 sender = static_cast<quint32>(it->first >> 8);
                const auto type = static_cast<StreamType>(it->first & 0xff);
                if (result == JitterBuffer::Result::Packet) {
                    stream.decoder->decode(payload, m_decoded);
                } else if (result == JitterBuffer::Result::Lost) {
                    stream.decoder->conceal(fec, m_decoded);
                }
                const bool talking = stream.jitter.isPlaying();
                if (talking != stream.talking) {
                    stream.talking = talking;
                    emit m_engine.talkingChanged(sender, type, talking);
                }
                const bool muted = mutedStreams[static_cast<std::size_t>(type) % mutedStreams.size()].load();
                if (result != JitterBuffer::Result::Empty && !deafened && !muted) {
                    mixInto(m_mix, m_decoded, s.outputGain * gains.value(sender, 1.0f));
                }
                if (!talking && now - stream.lastPacketMs > kStreamIdleDropMs) {
                    it = m_streams.erase(it);
                } else {
                    ++it;
                }
            }
            if (!m_monitor.empty()) {
                if (!deafened) {
                    mixInto(m_mix, m_monitor.front(), s.outputGain);
                }
                m_monitor.pop_front();
            }
            for (auto it = m_activeSounds.begin(); it != m_activeSounds.end();) {
                const qsizetype remaining = it->samples->size() - it->position;
                const qsizetype n = std::min<qsizetype>(remaining, kFrameSamples);
                mixInto(std::span<float>(m_mix.data(), static_cast<std::size_t>(n)),
                        std::span<const float>(it->samples->constData() + it->position, static_cast<std::size_t>(n)),
                        s.soundGain);
                it->position += n;
                it = it->position >= it->samples->size() ? m_activeSounds.erase(it) : it + 1;
            }
            softClip(m_mix);
            if (m_playback.write(m_mix.data(), m_mix.size()) < m_mix.size()) {
                break;
            }
            // What the speakers play is the echo cancellers' reference.
            m_processor.playback(m_mix);
            if (m_loopbackCanceller) {
                m_loopbackCanceller->playback(m_mix);
            }
        }
    }

    AudioEngine &m_engine;
    AudioBackend &m_backend;

public:
    RingBuffer m_capture;
    RingBuffer m_playback;
    RingBuffer m_loopback;

private:
    OpusEncoderWrapper m_encoder;
    OpusEncoderWrapper m_mediaEncoder;
    OpusEncoderWrapper m_systemEncoder;
    VoiceActivityDetector m_vad;
    VoiceProcessor m_processor;
    VoiceEffects m_effects;
    std::unique_ptr<EchoCanceller> m_loopbackCanceller; ///< Only when own sound cannot be excluded.
    std::unique_ptr<MediaFile> m_media;
    std::deque<std::vector<float>> m_monitor; ///< Media frames to play locally.
    std::vector<float> m_mediaFrame;
    std::vector<float> m_systemFrame;
    qint64 m_mediaNextMs = 0;
    qint64 m_lastCaptureMs = 0;
    quint16 m_mediaSequence = 0;
    quint16 m_systemSequence = 0;
    std::map<quint64, Stream> m_streams;
    std::vector<ActiveSound> m_activeSounds;
    std::vector<float> m_frame;
    std::vector<float> m_mix;
    std::vector<float> m_decoded;
    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock;
    qint64 m_lastOpenAttemptMs = 0;
    bool m_reportedFailure = false;
    quint16 m_sequence = 0;
};

AudioEngine::AudioEngine(std::unique_ptr<AudioBackend> backend, QObject *parent)
    : QObject(parent)
    , m_backend(std::move(backend))
{
    qRegisterMetaType<opentalk::audio::StreamType>();
    m_worker = new Worker(*this, *m_backend);
    m_worker->sounds = builtInSounds();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("OpenTalk audio"));
    m_thread.start(QThread::HighPriority);
}

AudioEngine::~AudioEngine()
{
    stop();
    m_thread.quit();
    m_thread.wait();
}

void AudioEngine::start()
{
    QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->begin(); }, Qt::BlockingQueuedConnection);
}

void AudioEngine::stop()
{
    if (m_thread.isRunning()) {
        QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->end(); }, Qt::BlockingQueuedConnection);
    }
}

void AudioEngine::setSettings(const AudioSettings &settings)
{
    QMutexLocker lock(&m_worker->mutex);
    const bool devices = settings.inputDeviceId != m_worker->settings.inputDeviceId ||
                         settings.outputDeviceId != m_worker->settings.outputDeviceId;
    m_worker->settings = settings;
    m_worker->settingsChanged = true;
    m_worker->devicesChanged = m_worker->devicesChanged || devices;
}

AudioSettings AudioEngine::settings() const
{
    QMutexLocker lock(&m_worker->mutex);
    return m_worker->settings;
}

void AudioEngine::setSoundSet(const SoundSet &sounds)
{
    // Sounds may be playing on the worker; swap them there.
    QMetaObject::invokeMethod(m_worker, [w = m_worker, sounds] { w->sounds = sounds; }, Qt::BlockingQueuedConnection);
}

void AudioEngine::setTransmitAllowed(bool allowed)
{
    m_worker->transmitAllowed = allowed;
}

void AudioEngine::setPushToTalkPressed(bool pressed)
{
    m_worker->pushToTalk = pressed;
}

void AudioEngine::setTransmitLatched(bool latched)
{
    m_latched = latched;
}

void AudioEngine::setMicrophoneMuted(bool muted)
{
    m_muted = muted;
}

void AudioEngine::setDeafened(bool deafened)
{
    m_deafened = deafened;
}

void AudioEngine::setUserGain(quint32 userId, float gain)
{
    QMutexLocker lock(&m_worker->mutex);
    m_worker->userGains.insert(userId, gain);
}

void AudioEngine::receivePacket(quint32 senderId, StreamType type, quint16 sequence, const QByteArray &opus)
{
    QMutexLocker lock(&m_worker->mutex);
    if (m_worker->incoming.size() < 1000) { // bounded even if the worker stalls
        m_worker->incoming.push_back({senderId, type, sequence, opus});
    }
}

void AudioEngine::resetStreams()
{
    QMutexLocker lock(&m_worker->mutex);
    m_worker->incoming.clear();
    m_worker->resetRequested = true;
}

void AudioEngine::playSound(EventSound sound)
{
    QMutexLocker lock(&m_worker->mutex);
    m_worker->soundRequests.push_back(sound);
}

bool AudioEngine::startMedia(const QString &path, QString *errorMessage)
{
    auto file = std::make_unique<MediaFile>();
    if (!file->open(path, errorMessage)) {
        return false;
    }
    QMutexLocker lock(&m_worker->mutex);
    m_worker->mediaSnapshot = {true, false, file->title(), 0, file->durationMs()};
    m_worker->pendingMedia = std::move(file);
    m_worker->mediaSkipMs = 0;
    m_worker->mediaPaused = false;
    return true;
}

void AudioEngine::stopMedia()
{
    QMutexLocker lock(&m_worker->mutex);
    m_worker->pendingMedia.reset();
    m_worker->mediaStopRequested = true;
    m_worker->mediaSnapshot = {};
    m_worker->mediaPaused = false;
}

void AudioEngine::setMediaPaused(bool paused)
{
    QMutexLocker lock(&m_worker->mutex);
    if (m_worker->mediaSnapshot.active) {
        m_worker->mediaPaused = paused;
        m_worker->mediaSnapshot.paused = paused;
    }
}

void AudioEngine::skipMedia(qint64 deltaMs)
{
    QMutexLocker lock(&m_worker->mutex);
    m_worker->mediaSkipMs += deltaMs;
}

MediaStatus AudioEngine::mediaStatus() const
{
    QMutexLocker lock(&m_worker->mutex);
    return m_worker->mediaSnapshot;
}

bool AudioEngine::canShareSystemAudio() const
{
    return m_backend->supportsLoopback();
}

bool AudioEngine::startSystemAudio(QString *errorMessage, bool *excludesOwnSound)
{
    bool ok = false;
    bool excludes = false;
    QString error;
    // The loopback device and its echo canceller belong to the worker thread.
    QMetaObject::invokeMethod(
        m_worker, [&] { ok = m_worker->startSystemAudio(&error, &excludes); }, Qt::BlockingQueuedConnection);
    if (errorMessage != nullptr) {
        *errorMessage = error;
    }
    if (excludesOwnSound != nullptr) {
        *excludesOwnSound = excludes;
    }
    return ok;
}

bool AudioEngine::startProgramAudio(quint32 processId, QString *errorMessage)
{
    bool ok = false;
    QString error;
    QMetaObject::invokeMethod(
        m_worker, [&] { ok = m_worker->startProgramAudio(processId, &error); }, Qt::BlockingQueuedConnection);
    if (errorMessage != nullptr) {
        *errorMessage = error;
    }
    return ok;
}

void AudioEngine::stopSystemAudio()
{
    if (m_thread.isRunning()) {
        QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->stopSystemAudio(); }, Qt::BlockingQueuedConnection);
    }
}

void AudioEngine::setIncomingStreamMuted(StreamType type, bool muted)
{
    if (type != StreamType::Voice) {
        m_worker->mutedStreams[static_cast<std::size_t>(type) % m_worker->mutedStreams.size()] = muted;
    }
}

bool AudioEngine::isIncomingStreamMuted(StreamType type) const
{
    return m_worker->mutedStreams[static_cast<std::size_t>(type) % m_worker->mutedStreams.size()].load();
}

} // namespace opentalk::audio
