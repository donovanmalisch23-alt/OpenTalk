// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/audio/RingBuffer.h"

#include <QList>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>

namespace opentalk::audio {

struct AudioDeviceInfo {
    QString id; ///< Stable identifier to store in settings (the device name).
    QString name;
    bool isDefault = false;
};

/// Access to sound hardware. The real implementation uses miniaudio (WASAPI on Windows);
/// tests use SyntheticAudioBackend.
///
/// Real-time rule: the backend's device callbacks only move samples between the
/// hardware and the two ring buffers. Everything else happens on the engine's worker
/// thread.
class AudioBackend {
public:
    virtual ~AudioBackend() = default;

    virtual QString name() const = 0;
    virtual QList<AudioDeviceInfo> inputDevices() = 0;
    virtual QList<AudioDeviceInfo> outputDevices() = 0;

    /// Opens the devices (empty id = system default). 48 kHz mono float samples from the
    /// microphone go into `capture`; samples for the speakers are taken from `playback`.
    virtual bool start(const QString &inputId, const QString &outputId, RingBuffer &capture, RingBuffer &playback,
                       QString *errorMessage) = 0;
    virtual void stop() = 0;
    /// True once a device stopped unexpectedly (for example it was unplugged).
    virtual bool hasFailed() const = 0;

    /// Capturing what the computer plays ("loopback"), for sharing system audio.
    virtual bool supportsLoopback() const { return false; }
    /// `excludeOwnProcess` keeps OpenTalk's own output (other people's voices) out of
    /// the capture, which prevents feedback; it needs Windows 10 build 20348 or later.
    virtual bool startLoopback(RingBuffer &ring, bool excludeOwnProcess, QString *errorMessage)
    {
        Q_UNUSED(ring);
        Q_UNUSED(excludeOwnProcess);
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("sharing computer audio is not supported on this system");
        }
        return false;
    }
    virtual void stopLoopback() {}
    /// True once the loopback capture stopped by itself (for example its device went away).
    virtual bool hasLoopbackFailed() const { return false; }
};

/// Creates the backend for real hardware.
std::unique_ptr<AudioBackend> createSystemAudioBackend();

/// Decodes a WAV, FLAC or MP3 file to 48 kHz mono float. Files longer than `maxSeconds`
/// are refused, so a soundpack cannot exhaust memory.
bool decodeAudioFile(const QString &path, QVector<float> *samples, QString *errorMessage, int maxSeconds = 30);

/// Plays a generated signal into `capture` and records what reaches `playback`, at real
/// speed, without touching any hardware. For tests and for machines with no audio devices.
class SyntheticAudioBackend : public AudioBackend {
public:
    /// `generator(sampleIndex)` returns the microphone signal.
    explicit SyntheticAudioBackend(std::function<float(qint64)> generator = {});
    ~SyntheticAudioBackend() override;

    QString name() const override { return QStringLiteral("synthetic"); }
    QList<AudioDeviceInfo> inputDevices() override;
    QList<AudioDeviceInfo> outputDevices() override;
    bool start(const QString &inputId, const QString &outputId, RingBuffer &capture, RingBuffer &playback,
               QString *errorMessage) override;
    void stop() override;
    bool hasFailed() const override;

    void setGenerator(std::function<float(qint64)> generator);
    /// Makes "computer audio" available: `generator` is what other programs play.
    /// Without `canExcludeOwnProcess` (like Windows 10), the capture also contains what
    /// this backend played, as real loopback would.
    void setLoopback(std::function<float(qint64)> generator, bool canExcludeOwnProcess);
    bool supportsLoopback() const override;
    bool startLoopback(RingBuffer &ring, bool excludeOwnProcess, QString *errorMessage) override;
    void stopLoopback() override;
    bool hasLoopbackFailed() const override;
    /// Simulates the shared device going away while computer audio is being captured.
    void simulateLoopbackFailure();
    /// Everything played so far (thread-safe copy).
    QVector<float> recordedOutput() const;
    void clearRecording();
    /// Simulates the device being unplugged.
    void simulateFailure();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace opentalk::audio
