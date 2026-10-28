// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <span>
#include <vector>

namespace opentalk::audio {

/// A second-order IIR filter (the "Audio EQ Cookbook" designs by Robert
/// Bristow-Johnson), for 48 kHz audio.
class Biquad {
public:
    static Biquad highPass(double frequency, double q = 0.7071);
    static Biquad lowShelf(double frequency, double gainDb);
    static Biquad highShelf(double frequency, double gainDb);

    void process(std::span<float> samples);
    void reset() { m_z1 = m_z2 = 0.0; }
    /// The filter's gain at `frequency` in dB (for tests and displays).
    double responseDb(double frequency) const;

private:
    double m_b0 = 1, m_b1 = 0, m_b2 = 0, m_a1 = 0, m_a2 = 0;
    double m_z1 = 0, m_z2 = 0;
};

/// Changes the pitch of a voice without changing its speed, by reading a short delay
/// line faster or slower and cross-fading between two read positions. It adds about
/// 40 ms of delay and some roughness, which suits a "voice changer"; it is not meant
/// for music.
class PitchShifter {
public:
    PitchShifter();
    void setSemitones(int semitones);
    int semitones() const { return m_semitones; }
    void process(std::span<float> samples);
    void reset();

private:
    int m_semitones = 0;
    double m_ratio = 1.0;
    std::vector<float> m_buffer;
    std::size_t m_write = 0;
    double m_phase = 0.0;
};

/// Optional effects applied to the microphone after noise suppression and before
/// sending: a rumble filter, bass and treble controls, and a voice changer. They run
/// on the audio worker thread, never in the device callback.
class VoiceEffects {
public:
    struct Options {
        bool highPass = false;  ///< Removes rumble below about 100 Hz.
        int bassDb = 0;         ///< -12 to +12.
        int trebleDb = 0;       ///< -12 to +12.
        int pitchSemitones = 0; ///< -12 to +12; 0 = the voice is unchanged.
        friend bool operator==(const Options &, const Options &) = default;
    };

    void setOptions(const Options &options);
    Options options() const { return m_options; }
    bool isActive() const;
    void process(std::span<float> samples);
    void reset();

private:
    Options m_options;
    Biquad m_highPass;
    Biquad m_bass;
    Biquad m_treble;
    PitchShifter m_pitch;
};

} // namespace opentalk::audio
