// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/audio/AudioBackend.h"
#include "opentalk/audio/AudioFormat.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace opentalk::audio {

struct SyntheticAudioBackend::Impl {
    std::function<float(qint64)> generator;
    mutable std::mutex mutex;
    QVector<float> recorded;
    std::thread thread;
    std::atomic<bool> running{false};
    std::atomic<bool> failed{false};
    qint64 sampleIndex = 0;
};

SyntheticAudioBackend::SyntheticAudioBackend(std::function<float(qint64)> generator)
    : d(std::make_unique<Impl>())
{
    d->generator = std::move(generator);
}

SyntheticAudioBackend::~SyntheticAudioBackend()
{
    stop();
}

QList<AudioDeviceInfo> SyntheticAudioBackend::inputDevices()
{
    return {{QStringLiteral("Synthetic microphone"), QStringLiteral("Synthetic microphone"), true}};
}

QList<AudioDeviceInfo> SyntheticAudioBackend::outputDevices()
{
    return {{QStringLiteral("Synthetic speakers"), QStringLiteral("Synthetic speakers"), true}};
}

bool SyntheticAudioBackend::start(const QString &, const QString &, RingBuffer &capture, RingBuffer &playback,
                                  QString *)
{
    stop();
    d->failed = false;
    d->running = true;
    d->thread = std::thread([this, &capture, &playback] {
        constexpr int kChunk = kSampleRate / 100; // 10 ms, like a real device period
        std::vector<float> buffer(kChunk);
        auto next = std::chrono::steady_clock::now();
        while (d->running) {
            next += std::chrono::milliseconds(10);
            {
                std::lock_guard lock(d->mutex);
                for (float &s : buffer) {
                    s = d->generator ? d->generator(d->sampleIndex) : 0.0f;
                    ++d->sampleIndex;
                }
            }
            capture.write(buffer.data(), buffer.size());
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            playback.read(buffer.data(), buffer.size()); // missing samples stay silent (an underrun)
            {
                std::lock_guard lock(d->mutex);
                d->recorded += QVector<float>(buffer.begin(), buffer.end());
                if (d->recorded.size() > kSampleRate * 120) {
                    d->recorded.remove(0, kSampleRate * 60);
                }
            }
            std::this_thread::sleep_until(next);
        }
    });
    return true;
}

void SyntheticAudioBackend::stop()
{
    d->running = false;
    if (d->thread.joinable()) {
        d->thread.join();
    }
}

bool SyntheticAudioBackend::hasFailed() const
{
    return d->failed;
}

void SyntheticAudioBackend::setGenerator(std::function<float(qint64)> generator)
{
    std::lock_guard lock(d->mutex);
    d->generator = std::move(generator);
}

QVector<float> SyntheticAudioBackend::recordedOutput() const
{
    std::lock_guard lock(d->mutex);
    return d->recorded;
}

void SyntheticAudioBackend::clearRecording()
{
    std::lock_guard lock(d->mutex);
    d->recorded.clear();
}

void SyntheticAudioBackend::simulateFailure()
{
    d->running = false;
    if (d->thread.joinable()) {
        d->thread.join();
    }
    d->failed = true;
}

} // namespace opentalk::audio
