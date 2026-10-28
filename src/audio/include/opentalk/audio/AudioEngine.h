// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/audio/AudioBackend.h"
#include "opentalk/audio/AudioFormat.h"
#include "opentalk/audio/Sounds.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QThread>

#include <atomic>
#include <memory>

namespace opentalk::audio {

enum class TransmitMode {
    PushToTalk,      ///< Transmit while the push-to-talk key is held (or latched with "toggle").
    VoiceActivation, ///< Transmit when the microphone level is above the threshold.
    Continuous,      ///< Always transmit (unless muted).
};

struct AudioSettings {
    QString inputDeviceId;  ///< Empty: system default.
    QString outputDeviceId; ///< Empty: system default.
    float inputGain = 1.0f;
    float outputGain = 1.0f;
    TransmitMode mode = TransmitMode::PushToTalk;
    float voiceActivationThresholdDb = -45.0f;
    int bitrate = 32000;
    bool soundsEnabled = true;
    float soundGain = 0.8f;
    bool pushToTalkSounds = true;
};

/// Voice in and out, independent of the network.
///
/// A worker thread turns microphone frames into Opus packets (packetReady) when
/// transmitting, and mixes received packets, per-user volumes and event sounds into the
/// speakers. Device callbacks only touch lock-free ring buffers.
///
/// Public methods are called from the thread that owns the engine (the GUI thread);
/// signals are delivered to that thread.
class AudioEngine : public QObject {
    Q_OBJECT

public:
    explicit AudioEngine(std::unique_ptr<AudioBackend> backend, QObject *parent = nullptr);
    ~AudioEngine() override;

    AudioBackend &backend() { return *m_backend; }

    /// Opens the devices from the current settings. On failure the engine keeps trying
    /// every few seconds and reports through deviceProblem().
    void start();
    void stop();
    bool isRunning() const { return m_running.load(); }

    void setSettings(const AudioSettings &settings);
    AudioSettings settings() const;
    void setSoundSet(const SoundSet &sounds);

    /// Whether captured audio is sent at all (only while connected).
    void setTransmitAllowed(bool allowed);
    void setPushToTalkPressed(bool pressed);
    /// "Toggle transmit": stays transmitting until toggled off (push-to-talk mode).
    void setTransmitLatched(bool latched);
    bool isTransmitLatched() const { return m_latched.load(); }
    void setMicrophoneMuted(bool muted);
    bool isMicrophoneMuted() const { return m_muted.load(); }
    void setDeafened(bool deafened);
    bool isDeafened() const { return m_deafened.load(); }
    void setUserGain(quint32 userId, float gain);

    /// A packet from the network (any thread).
    void receivePacket(quint32 senderId, StreamType type, quint16 sequence, const QByteArray &opus);
    /// Forgets all incoming streams (after disconnecting).
    void resetStreams();
    void playSound(EventSound sound);

    bool isTransmitting() const { return m_transmitting.load(); }
    /// Microphone level of the last frame in dBFS (-100 = silence), for meters.
    float inputLevelDb() const { return m_inputLevel.load(); }

signals:
    void packetReady(opentalk::audio::StreamType type, quint16 sequence, const QByteArray &opus);
    void transmittingChanged(bool transmitting);
    void talkingChanged(quint32 userId, opentalk::audio::StreamType type, bool talking);
    void deviceProblem(const QString &message);
    void devicesStarted();

private:
    class Worker;
    std::unique_ptr<AudioBackend> m_backend;
    QThread m_thread;
    Worker *m_worker = nullptr;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_transmitting{false};
    std::atomic<bool> m_latched{false};
    std::atomic<bool> m_muted{false};
    std::atomic<bool> m_deafened{false};
    std::atomic<float> m_inputLevel{-100.0f};
};

} // namespace opentalk::audio

Q_DECLARE_METATYPE(opentalk::audio::StreamType)
