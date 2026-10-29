// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QVector>

#include <cmath>
#include <numbers>

namespace opentalk::test {

/// Strength of one frequency in a signal (Goertzel algorithm), as an amplitude-like
/// value: a full-scale sine at that frequency gives about 0.5.
inline double toneStrength(const float *samples, qsizetype count, double frequency, int sampleRate = 48000)
{
    if (count <= 0) {
        return 0.0;
    }
    const double coefficient = 2.0 * std::cos(2.0 * std::numbers::pi * frequency / sampleRate);
    double s1 = 0.0;
    double s2 = 0.0;
    for (qsizetype i = 0; i < count; ++i) {
        const double s = samples[i] + coefficient * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    const double power = s1 * s1 + s2 * s2 - coefficient * s1 * s2;
    return std::sqrt(std::max(0.0, power)) / static_cast<double>(count);
}

inline double toneStrength(const QVector<float> &samples, double frequency)
{
    return toneStrength(samples.constData(), samples.size(), frequency);
}

inline float sine(qint64 index, double frequency, double amplitude = 0.5)
{
    return static_cast<float>(amplitude *
                              std::sin(2.0 * std::numbers::pi * frequency * static_cast<double>(index) / 48000.0));
}

} // namespace opentalk::test
