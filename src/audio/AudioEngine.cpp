// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/audio/AudioEngine.h"

#include "opentalk/audio/Dsp.h"
#include "opentalk/audio/JitterBuffer.h"
#include "opentalk/audio/OpusCodec.h"
#include "opentalk/audio/RingBuffer.h"

#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>

#include <array>
#include <map>
#include <vector>

namespace opentalk::audio {
namespace {

constexpr int kTickMs = 5;
constexpr int kPlaybackTargetFrames = 2; ///< Mixed audio kept ready for the device (40 ms).
constexpr int kRetryMs = 3000;
constexpr qint64 kStreamIdleDropMs = 30'000;

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
        , m_encoder(OpusEncoderWrapper::Content::Voice, 32000)
    {
        m_frame.resize(kFrameSamples);
        m_mix.resize(kFrameSamples);
        m_decoded.resize(kFrameSamples);
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
        m_backend.stop();
        m_engine.m_running = false;
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
    std::atomic<bool> transmitAllowed{false};
    std::atomic<bool> pushToTalk{false};

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
                settingsChanged = false;
            }
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

        processCapture(s);
        processPlayback(s, gains, now);
    }

    void processCapture(const AudioSettings &s)
    {
        while (m_capture.available() >= static_cast<std::size_t>(kFrameSamples)) {
            m_capture.read(m_frame.data(), m_frame.size());
            applyGain(m_frame, s.inputGain);
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

    void processPlayback(const AudioSettings &s, const QHash<quint32, float> &gains, qint64 now)
    {
        if (!m_engine.m_running.load()) {
            return;
        }
        const bool deafened = m_engine.m_deafened.load();
        QByteArray payload;
        QByteArray fec;
        while (m_playback.available() < static_cast<std::size_t>(kPlaybackTargetFrames * kFrameSamples)) {
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
                if (result != JitterBuffer::Result::Empty && !deafened) {
                    mixInto(m_mix, m_decoded, s.outputGain * gains.value(sender, 1.0f));
                }
                if (!talking && now - stream.lastPacketMs > kStreamIdleDropMs) {
                    it = m_streams.erase(it);
                } else {
                    ++it;
                }
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
        }
    }

    AudioEngine &m_engine;
    AudioBackend &m_backend;

public:
    RingBuffer m_capture;
    RingBuffer m_playback;

private:
    OpusEncoderWrapper m_encoder;
    VoiceActivityDetector m_vad;
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

} // namespace opentalk::audio
