// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/audio/AudioBackend.h"
#include "opentalk/audio/AudioFormat.h"

#include <QFile>

#include <atomic>
#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_GENERATION
#include "miniaudio.h"

namespace opentalk::audio {
namespace {

void setError(QString *target, const QString &message)
{
    if (target != nullptr) {
        *target = message;
    }
}

QList<AudioDeviceInfo> listDevices(bool capture)
{
    QList<AudioDeviceInfo> result;
    ma_context context;
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
        return result;
    }
    ma_device_info *playbackInfos = nullptr;
    ma_device_info *captureInfos = nullptr;
    ma_uint32 playbackCount = 0;
    ma_uint32 captureCount = 0;
    if (ma_context_get_devices(&context, &playbackInfos, &playbackCount, &captureInfos, &captureCount) == MA_SUCCESS) {
        const ma_device_info *infos = capture ? captureInfos : playbackInfos;
        const ma_uint32 count = capture ? captureCount : playbackCount;
        for (ma_uint32 i = 0; i < count; ++i) {
            const QString name = QString::fromUtf8(infos[i].name);
            result.append({name, name, infos[i].isDefault != 0});
        }
    }
    ma_context_uninit(&context);
    return result;
}

/// Finds a device by its stored name; null means "use the default".
bool findDevice(ma_context &context, bool capture, const QString &id, ma_device_id *out)
{
    if (id.isEmpty()) {
        return false;
    }
    ma_device_info *playbackInfos = nullptr;
    ma_device_info *captureInfos = nullptr;
    ma_uint32 playbackCount = 0;
    ma_uint32 captureCount = 0;
    if (ma_context_get_devices(&context, &playbackInfos, &playbackCount, &captureInfos, &captureCount) != MA_SUCCESS) {
        return false;
    }
    const ma_device_info *infos = capture ? captureInfos : playbackInfos;
    const ma_uint32 count = capture ? captureCount : playbackCount;
    const QByteArray wanted = id.toUtf8();
    for (ma_uint32 i = 0; i < count; ++i) {
        if (wanted == infos[i].name) {
            *out = infos[i].id;
            return true;
        }
    }
    return false;
}

class MiniaudioBackend : public AudioBackend {
public:
    MiniaudioBackend() = default;
    ~MiniaudioBackend() override
    {
        stopLoopback();
        stop();
    }

    QString name() const override { return QStringLiteral("miniaudio"); }
    QList<AudioDeviceInfo> inputDevices() override { return listDevices(true); }
    QList<AudioDeviceInfo> outputDevices() override { return listDevices(false); }

    bool start(const QString &inputId, const QString &outputId, RingBuffer &capture, RingBuffer &playback,
               QString *errorMessage) override
    {
        stop();
        m_failed = false;
        m_capture = &capture;
        m_playback = &playback;
        m_context = std::make_unique<ma_context>();
        if (ma_context_init(nullptr, 0, nullptr, m_context.get()) != MA_SUCCESS) {
            m_context.reset();
            setError(errorMessage, QStringLiteral("the sound system could not be opened"));
            return false;
        }

        ma_device_id inputDevice;
        ma_device_id outputDevice;
        const bool haveInput = findDevice(*m_context, true, inputId, &inputDevice);
        const bool haveOutput = findDevice(*m_context, false, outputId, &outputDevice);

        ma_device_config captureConfig = ma_device_config_init(ma_device_type_capture);
        captureConfig.capture.pDeviceID = haveInput ? &inputDevice : nullptr;
        captureConfig.capture.format = ma_format_f32;
        captureConfig.capture.channels = 1;
        captureConfig.sampleRate = kSampleRate;
        captureConfig.periodSizeInMilliseconds = 10;
        captureConfig.dataCallback = &MiniaudioBackend::captureCallback;
        captureConfig.notificationCallback = &MiniaudioBackend::notificationCallback;
        captureConfig.pUserData = this;

        ma_device_config playbackConfig = ma_device_config_init(ma_device_type_playback);
        playbackConfig.playback.pDeviceID = haveOutput ? &outputDevice : nullptr;
        playbackConfig.playback.format = ma_format_f32;
        playbackConfig.playback.channels = 1;
        playbackConfig.sampleRate = kSampleRate;
        playbackConfig.periodSizeInMilliseconds = 10;
        playbackConfig.dataCallback = &MiniaudioBackend::playbackCallback;
        playbackConfig.notificationCallback = &MiniaudioBackend::notificationCallback;
        playbackConfig.pUserData = this;

        m_playbackDevice = std::make_unique<ma_device>();
        if (ma_device_init(m_context.get(), &playbackConfig, m_playbackDevice.get()) != MA_SUCCESS) {
            m_playbackDevice.reset();
            stop();
            setError(errorMessage, QStringLiteral("the speakers or headphones could not be opened"));
            return false;
        }
        // A missing microphone is not fatal: you can still listen.
        m_captureDevice = std::make_unique<ma_device>();
        if (ma_device_init(m_context.get(), &captureConfig, m_captureDevice.get()) != MA_SUCCESS) {
            m_captureDevice.reset();
            setError(errorMessage, QStringLiteral("no microphone could be opened; you can listen but not talk"));
        }
        m_stopping = false;
        if (ma_device_start(m_playbackDevice.get()) != MA_SUCCESS ||
            (m_captureDevice && ma_device_start(m_captureDevice.get()) != MA_SUCCESS)) {
            stop();
            setError(errorMessage, QStringLiteral("the audio devices could not be started"));
            return false;
        }
        return true;
    }

    void stop() override
    {
        m_stopping = true;
        if (m_captureDevice) {
            ma_device_uninit(m_captureDevice.get());
            m_captureDevice.reset();
        }
        if (m_playbackDevice) {
            ma_device_uninit(m_playbackDevice.get());
            m_playbackDevice.reset();
        }
        if (m_context && !m_loopbackDevice) {
            ma_context_uninit(m_context.get());
            m_context.reset();
        }
    }

    bool hasFailed() const override { return m_failed; }

#ifdef Q_OS_WIN
    bool supportsLoopback() const override { return true; }
#endif

    bool startLoopback(RingBuffer &ring, bool excludeOwnProcess, QString *errorMessage) override
    {
#ifdef Q_OS_WIN
        stopLoopback();
        m_loopbackRing = &ring;
        if (!m_context) {
            m_context = std::make_unique<ma_context>();
            if (ma_context_init(nullptr, 0, nullptr, m_context.get()) != MA_SUCCESS) {
                m_context.reset();
                setError(errorMessage, QStringLiteral("the sound system could not be opened"));
                return false;
            }
        }
        ma_device_config config = ma_device_config_init(ma_device_type_loopback);
        config.capture.format = ma_format_f32;
        config.capture.channels = 1;
        config.sampleRate = kSampleRate;
        config.periodSizeInMilliseconds = 10;
        config.dataCallback = &MiniaudioBackend::loopbackCallback;
        config.pUserData = this;
        if (excludeOwnProcess) {
            config.wasapi.loopbackProcessID = GetCurrentProcessId();
            config.wasapi.loopbackProcessExclude = MA_TRUE;
        }
        m_loopbackDevice = std::make_unique<ma_device>();
        if (ma_device_init(m_context.get(), &config, m_loopbackDevice.get()) != MA_SUCCESS ||
            ma_device_start(m_loopbackDevice.get()) != MA_SUCCESS) {
            m_loopbackDevice.reset();
            setError(errorMessage, excludeOwnProcess
                                       ? QStringLiteral("sharing computer audio without OpenTalk's own sound needs "
                                                        "Windows 10 version 2104 (build 20348) or later")
                                       : QStringLiteral("computer audio could not be captured"));
            return false;
        }
        return true;
#else
        return AudioBackend::startLoopback(ring, excludeOwnProcess, errorMessage);
#endif
    }

    void stopLoopback() override
    {
        if (m_loopbackDevice) {
            ma_device_uninit(m_loopbackDevice.get());
            m_loopbackDevice.reset();
        }
        m_loopbackRing = nullptr;
    }

private:
    static void captureCallback(ma_device *device, void *, const void *input, ma_uint32 frames)
    {
        auto *self = static_cast<MiniaudioBackend *>(device->pUserData);
        self->m_capture->write(static_cast<const float *>(input), frames);
    }

    static void playbackCallback(ma_device *device, void *output, const void *, ma_uint32 frames)
    {
        auto *self = static_cast<MiniaudioBackend *>(device->pUserData);
        auto *out = static_cast<float *>(output);
        const std::size_t got = self->m_playback->read(out, frames);
        for (std::size_t i = got; i < frames; ++i) {
            out[i] = 0.0f; // underrun: play silence rather than garbage
        }
    }

    static void loopbackCallback(ma_device *device, void *, const void *input, ma_uint32 frames)
    {
        auto *self = static_cast<MiniaudioBackend *>(device->pUserData);
        if (self->m_loopbackRing != nullptr) {
            self->m_loopbackRing->write(static_cast<const float *>(input), frames);
        }
    }

    static void notificationCallback(const ma_device_notification *notification)
    {
        auto *self = static_cast<MiniaudioBackend *>(notification->pDevice->pUserData);
        if (notification->type == ma_device_notification_type_stopped && !self->m_stopping) {
            self->m_failed = true; // for example the headset was unplugged
        }
    }

    std::unique_ptr<ma_context> m_context;
    std::unique_ptr<ma_device> m_captureDevice;
    std::unique_ptr<ma_device> m_playbackDevice;
    std::unique_ptr<ma_device> m_loopbackDevice;
    RingBuffer *m_capture = nullptr;
    RingBuffer *m_playback = nullptr;
    RingBuffer *m_loopbackRing = nullptr;
    std::atomic<bool> m_failed{false};
    std::atomic<bool> m_stopping{false};
};

} // namespace

std::unique_ptr<AudioBackend> createSystemAudioBackend()
{
    return std::make_unique<MiniaudioBackend>();
}

bool decodeAudioFile(const QString &path, QVector<float> *samples, QString *errorMessage, int maxSeconds)
{
    samples->clear();
    QFile file(path);
    if (!file.exists()) {
        setError(errorMessage, QStringLiteral("file not found"));
        return false;
    }
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, kSampleRate);
    ma_decoder decoder;
#ifdef Q_OS_WIN
    const std::wstring wide = path.toStdWString();
    const ma_result opened = ma_decoder_init_file_w(wide.c_str(), &config, &decoder);
#else
    const QByteArray nativePath = QFile::encodeName(path);
    const ma_result opened = ma_decoder_init_file(nativePath.constData(), &config, &decoder);
#endif
    if (opened != MA_SUCCESS) {
        setError(errorMessage, QStringLiteral("not a WAV, FLAC or MP3 file"));
        return false;
    }
    const ma_uint64 maxFrames = static_cast<ma_uint64>(maxSeconds) * kSampleRate;
    float chunk[4096];
    bool ok = true;
    for (;;) {
        ma_uint64 read = 0;
        const ma_result result = ma_decoder_read_pcm_frames(&decoder, chunk, 4096, &read);
        samples->append(QVector<float>(chunk, chunk + read));
        if (static_cast<ma_uint64>(samples->size()) > maxFrames) {
            setError(errorMessage, QStringLiteral("longer than %1 seconds").arg(maxSeconds));
            ok = false;
            break;
        }
        if (result != MA_SUCCESS || read < 4096) {
            break;
        }
    }
    ma_decoder_uninit(&decoder);
    if (!ok) {
        samples->clear();
    }
    return ok;
}

} // namespace opentalk::audio
