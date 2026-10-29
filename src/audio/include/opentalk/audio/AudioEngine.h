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
    bool noiseSuppression = true;
    bool autoGain = false;
    bool echoCancellation = false; ///< For speakers; not needed with headphones.
    // Voice effects (VoiceEffects): off unless chosen.
    bool highPass = false;
    int bassDb = 0;
    int trebleDb = 0;
    int pitchSemitones = 0;
    // Media files and shared computer audio.
    int mediaBitrate = 64000;
    float mediaGain = 1.0f;   ///< Volume of what is streamed (files and computer audio).
    bool mediaMonitor = true; ///< Also play the file being streamed on this computer.
};

/// What the media player is doing.
struct MediaStatus {
    bool active = false;
    bool paused = false;
    QString title;
    qint64 positionMs = 0;
    qint64 durationMs = -1; ///< -1 when unknown.
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

    // ------------------------------------------------- media files (StreamType::Media)
    /// Streams a WAV, FLAC or MP3 file into the channel at its normal speed, replacing
    /// any file already playing. Packets go out through packetReady while transmitting
    /// is allowed; the microphone keeps working alongside.
    bool startMedia(const QString &path, QString *errorMessage);
    void stopMedia();
    void setMediaPaused(bool paused);
    /// Moves the playing file forwards (positive) or backwards.
    void skipMedia(qint64 deltaMs);
    MediaStatus mediaStatus() const;

    // ------------------------------------------ computer audio (StreamType::SystemAudio)
    bool canShareSystemAudio() const;
    /// Starts sending what the computer plays. It first tries to leave OpenTalk's own
    /// sound out (Windows 11). Where that is not possible it captures everything and
    /// removes OpenTalk's own sound with echo cancellation; `excludesOwnSound` tells
    /// which, so the user can be warned that some of it may come through.
    bool startSystemAudio(QString *errorMessage, bool *excludesOwnSound = nullptr);
    /// Starts sending only what one program plays (identified by its process ID), sent
    /// as computer audio. OpenTalk's own sound is never part of it. Needs Windows 11.
    bool startProgramAudio(quint32 processId, QString *errorMessage);
    void stopSystemAudio();
    bool isSharingSystemAudio() const { return m_sharingSystemAudio.load(); }

    /// Silences incoming files or computer audio from everyone; voice is not affected.
    void setIncomingStreamMuted(StreamType type, bool muted);
    bool isIncomingStreamMuted(StreamType type) const;

signals:
    void packetReady(opentalk::audio::StreamType type, quint16 sequence, const QByteArray &opus);
    void transmittingChanged(bool transmitting);
    void talkingChanged(quint32 userId, opentalk::audio::StreamType type, bool talking);
    void deviceProblem(const QString &message);
    void devicesStarted();
    /// The file reached its end (not sent for stopMedia()).
    void mediaFinished(const QString &title);
    /// Sharing computer audio stopped by itself (not sent for stopSystemAudio()).
    void systemAudioStopped(const QString &reason);

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
    std::atomic<bool> m_sharingSystemAudio{false};
};

} // namespace opentalk::audio

Q_DECLARE_METATYPE(opentalk::audio::StreamType)
