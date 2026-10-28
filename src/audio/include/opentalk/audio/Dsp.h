// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <span>

namespace opentalk::audio {

/// Level of a block in decibels relative to full scale (-100 for silence).
float levelDbfs(std::span<const float> samples);

void applyGain(std::span<float> samples, float gain);

/// Adds `source * gain` into `destination` (same length).
void mixInto(std::span<float> destination, std::span<const float> source, float gain);

/// Gently limits samples to the range -1..1, so a loud mix distorts less than hard
/// clipping would.
void softClip(std::span<float> samples);

/// Decides whether a frame contains speech, from its level, with a hangover so the
/// ends of words are not cut off.
class VoiceActivityDetector {
public:
    explicit VoiceActivityDetector(float thresholdDb = -45.0f, int hangoverFrames = 15);
    void setThreshold(float thresholdDb) { m_thresholdDb = thresholdDb; }
    float threshold() const { return m_thresholdDb; }
    bool update(float levelDb);
    void reset() { m_remaining = 0; }

private:
    float m_thresholdDb;
    int m_hangoverFrames;
    int m_remaining = 0;
};

} // namespace opentalk::audio
