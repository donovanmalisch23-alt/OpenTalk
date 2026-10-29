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
    std::function<float(qint64)> loopbackGenerator;
    bool canExclude = false;
    RingBuffer *loopbackRing = nullptr; ///< Guarded by mutex
    bool loopbackIncludesOwn = false;
    std::function<float(quint32, qint64)> programGenerator;
    quint32 loopbackProgram = 0; ///< Non-zero while capturing one program.
    std::atomic<bool> loopbackFailed{false};
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
                if (d->loopbackRing != nullptr) {
                    std::vector<float> system(buffer.size());
                    const qint64 first = d->sampleIndex - static_cast<qint64>(buffer.size());
                    for (std::size_t i = 0; i < system.size(); ++i) {
                        const qint64 index = first + static_cast<qint64>(i);
                        system[i] = d->loopbackProgram != 0
                                        ? d->programGenerator(d->loopbackProgram, index)
                                        : d->loopbackGenerator(index) + (d->loopbackIncludesOwn ? buffer[i] : 0.0f);
                    }
                    d->loopbackRing->write(system.data(), system.size());
                }
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

void SyntheticAudioBackend::setLoopback(std::function<float(qint64)> generator, bool canExcludeOwnProcess)
{
    std::lock_guard lock(d->mutex);
    d->loopbackGenerator = std::move(generator);
    d->canExclude = canExcludeOwnProcess;
}

bool SyntheticAudioBackend::supportsLoopback() const
{
    std::lock_guard lock(d->mutex);
    return static_cast<bool>(d->loopbackGenerator);
}

bool SyntheticAudioBackend::startLoopback(RingBuffer &ring, bool excludeOwnProcess, QString *errorMessage)
{
    std::lock_guard lock(d->mutex);
    if (!d->loopbackGenerator || (excludeOwnProcess && !d->canExclude)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("not supported by this synthetic device");
        }
        return false;
    }
    d->loopbackRing = &ring;
    d->loopbackIncludesOwn = !excludeOwnProcess;
    d->loopbackProgram = 0;
    d->loopbackFailed = false;
    return true;
}

void SyntheticAudioBackend::setProgramLoopback(std::function<float(quint32, qint64)> generator)
{
    std::lock_guard lock(d->mutex);
    d->programGenerator = std::move(generator);
}

bool SyntheticAudioBackend::startProgramLoopback(RingBuffer &ring, quint32 processId, QString *errorMessage)
{
    std::lock_guard lock(d->mutex);
    if (!d->programGenerator || !d->canExclude || processId == 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("not supported by this synthetic device");
        }
        return false;
    }
    d->loopbackRing = &ring;
    d->loopbackIncludesOwn = false;
    d->loopbackProgram = processId;
    d->loopbackFailed = false;
    return true;
}

void SyntheticAudioBackend::stopLoopback()
{
    std::lock_guard lock(d->mutex);
    d->loopbackRing = nullptr;
    d->loopbackProgram = 0;
}

bool SyntheticAudioBackend::hasLoopbackFailed() const
{
    return d->loopbackFailed;
}

void SyntheticAudioBackend::simulateLoopbackFailure()
{
    std::lock_guard lock(d->mutex);
    d->loopbackRing = nullptr;
    d->loopbackFailed = true;
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
