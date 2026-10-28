// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/audio/Dsp.h"

#include <algorithm>
#include <cmath>

namespace opentalk::audio {

float levelDbfs(std::span<const float> samples)
{
    if (samples.empty()) {
        return -100.0f;
    }
    double sum = 0.0;
    for (const float s : samples) {
        sum += static_cast<double>(s) * static_cast<double>(s);
    }
    const double rms = std::sqrt(sum / static_cast<double>(samples.size()));
    if (rms < 1e-5) {
        return -100.0f;
    }
    return static_cast<float>(20.0 * std::log10(rms));
}

void applyGain(std::span<float> samples, float gain)
{
    if (gain == 1.0f) {
        return;
    }
    for (float &s : samples) {
        s *= gain;
    }
}

void mixInto(std::span<float> destination, std::span<const float> source, float gain)
{
    const std::size_t n = std::min(destination.size(), source.size());
    for (std::size_t i = 0; i < n; ++i) {
        destination[i] += source[i] * gain;
    }
}

void softClip(std::span<float> samples)
{
    // Linear below 0.8, then a smooth curve that approaches (but never exceeds) 1.0.
    constexpr float knee = 0.8f;
    for (float &s : samples) {
        const float a = std::fabs(s);
        if (a > knee) {
            const float over = (a - knee) / (1.0f - knee);
            const float shaped = knee + (1.0f - knee) * std::tanh(over);
            s = std::copysign(shaped, s);
        }
    }
}

VoiceActivityDetector::VoiceActivityDetector(float thresholdDb, int hangoverFrames)
    : m_thresholdDb(thresholdDb)
    , m_hangoverFrames(hangoverFrames)
{
}

bool VoiceActivityDetector::update(float levelDb)
{
    if (levelDb >= m_thresholdDb) {
        m_remaining = m_hangoverFrames;
        return true;
    }
    if (m_remaining > 0) {
        --m_remaining;
        return true;
    }
    return false;
}

} // namespace opentalk::audio
