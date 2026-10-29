// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/audio/VoiceEffects.h"

#include "opentalk/audio/AudioFormat.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace opentalk::audio {
namespace {

constexpr double kPi = std::numbers::pi;

/// Delay-line length for the pitch shifter: 40 ms, long enough for the lowest voices,
/// short enough that the delay is not distracting.
constexpr std::size_t kPitchWindow = kSampleRate * 40 / 1000;

} // namespace

// ----------------------------------------------------------------------- Biquad

Biquad Biquad::highPass(double frequency, double q)
{
    const double w0 = 2 * kPi * frequency / kSampleRate;
    const double alpha = std::sin(w0) / (2 * q);
    const double cosw = std::cos(w0);
    const double a0 = 1 + alpha;
    Biquad f;
    f.m_b0 = (1 + cosw) / 2 / a0;
    f.m_b1 = -(1 + cosw) / a0;
    f.m_b2 = (1 + cosw) / 2 / a0;
    f.m_a1 = -2 * cosw / a0;
    f.m_a2 = (1 - alpha) / a0;
    return f;
}

Biquad Biquad::lowShelf(double frequency, double gainDb)
{
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2 * kPi * frequency / kSampleRate;
    const double cosw = std::cos(w0);
    const double alpha = std::sin(w0) / 2 * std::sqrt(2.0); // shelf slope 1
    const double sqrtA2alpha = 2 * std::sqrt(a) * alpha;
    const double a0 = (a + 1) + (a - 1) * cosw + sqrtA2alpha;
    Biquad f;
    f.m_b0 = a * ((a + 1) - (a - 1) * cosw + sqrtA2alpha) / a0;
    f.m_b1 = 2 * a * ((a - 1) - (a + 1) * cosw) / a0;
    f.m_b2 = a * ((a + 1) - (a - 1) * cosw - sqrtA2alpha) / a0;
    f.m_a1 = -2 * ((a - 1) + (a + 1) * cosw) / a0;
    f.m_a2 = ((a + 1) + (a - 1) * cosw - sqrtA2alpha) / a0;
    return f;
}

Biquad Biquad::highShelf(double frequency, double gainDb)
{
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2 * kPi * frequency / kSampleRate;
    const double cosw = std::cos(w0);
    const double alpha = std::sin(w0) / 2 * std::sqrt(2.0);
    const double sqrtA2alpha = 2 * std::sqrt(a) * alpha;
    const double a0 = (a + 1) - (a - 1) * cosw + sqrtA2alpha;
    Biquad f;
    f.m_b0 = a * ((a + 1) + (a - 1) * cosw + sqrtA2alpha) / a0;
    f.m_b1 = -2 * a * ((a - 1) + (a + 1) * cosw) / a0;
    f.m_b2 = a * ((a + 1) + (a - 1) * cosw - sqrtA2alpha) / a0;
    f.m_a1 = 2 * ((a - 1) - (a + 1) * cosw) / a0;
    f.m_a2 = ((a + 1) - (a - 1) * cosw - sqrtA2alpha) / a0;
    return f;
}

void Biquad::process(std::span<float> samples)
{
    // Transposed direct form II: numerically well behaved for audio filters.
    for (float &s : samples) {
        const double in = s;
        const double out = m_b0 * in + m_z1;
        m_z1 = m_b1 * in - m_a1 * out + m_z2;
        m_z2 = m_b2 * in - m_a2 * out;
        s = static_cast<float>(out);
    }
    // Keep denormal numbers out of the state once the input has gone silent.
    if (std::fabs(m_z1) < 1e-20) {
        m_z1 = 0;
    }
    if (std::fabs(m_z2) < 1e-20) {
        m_z2 = 0;
    }
}

double Biquad::responseDb(double frequency) const
{
    const std::complex<double> z1 = std::polar(1.0, -2 * kPi * frequency / kSampleRate);
    const std::complex<double> z2 = z1 * z1;
    const std::complex<double> h = (m_b0 + m_b1 * z1 + m_b2 * z2) / (1.0 + m_a1 * z1 + m_a2 * z2);
    return 20.0 * std::log10(std::max(std::abs(h), 1e-12));
}

// ----------------------------------------------------------------- PitchShifter

PitchShifter::PitchShifter()
    : m_buffer(kPitchWindow * 2, 0.0f)
{
}

void PitchShifter::setSemitones(int semitones)
{
    semitones = std::clamp(semitones, -12, 12);
    if (semitones == m_semitones) {
        return;
    }
    m_semitones = semitones;
    m_ratio = std::pow(2.0, semitones / 12.0);
}

void PitchShifter::reset()
{
    std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
    m_write = 0;
    m_phase = 0.0;
}

void PitchShifter::process(std::span<float> samples)
{
    if (m_semitones == 0) {
        return;
    }
    const std::size_t size = m_buffer.size();
    const auto window = static_cast<double>(kPitchWindow);
    // The delay changes by (1 - ratio) samples per sample, so the read position moves
    // at `ratio` times the speed of the write position.
    const double step = (1.0 - m_ratio) / window;
    auto readAt = [&](double delay) {
        // One extra sample of delay, so interpolation never reads ahead of the input.
        double position = static_cast<double>(m_write) - 1.0 - delay;
        while (position < 0) {
            position += static_cast<double>(size);
        }
        const auto index = static_cast<std::size_t>(position) % size;
        const double fraction = position - std::floor(position);
        const float a = m_buffer[index];
        const float b = m_buffer[(index + 1) % size];
        return a + static_cast<float>(fraction) * (b - a);
    };
    for (float &s : samples) {
        m_buffer[m_write] = s;
        const double phaseB = std::fmod(m_phase + 0.5, 1.0);
        // Each read position fades in and out across the window, so its jump from one
        // end of the delay line to the other is silent; the two weights always sum to 1.
        const double weightA = std::pow(std::sin(kPi * m_phase), 2);
        const double weightB = 1.0 - weightA;
        s = static_cast<float>(weightA * readAt(m_phase * window) + weightB * readAt(phaseB * window));
        m_write = (m_write + 1) % size;
        m_phase += step;
        if (m_phase >= 1.0) {
            m_phase -= 1.0;
        } else if (m_phase < 0.0) {
            m_phase += 1.0;
        }
    }
}

// ----------------------------------------------------------------- VoiceEffects

void VoiceEffects::setOptions(const Options &options)
{
    Options o = options;
    o.bassDb = std::clamp(o.bassDb, -12, 12);
    o.trebleDb = std::clamp(o.trebleDb, -12, 12);
    o.pitchSemitones = std::clamp(o.pitchSemitones, -12, 12);
    if (o == m_options) {
        return;
    }
    if (o.highPass != m_options.highPass) {
        m_highPass = Biquad::highPass(100.0);
    }
    if (o.bassDb != m_options.bassDb) {
        m_bass = Biquad::lowShelf(250.0, o.bassDb);
    }
    if (o.trebleDb != m_options.trebleDb) {
        m_treble = Biquad::highShelf(3500.0, o.trebleDb);
    }
    if (o.pitchSemitones != m_options.pitchSemitones) {
        m_pitch.setSemitones(o.pitchSemitones);
        m_pitch.reset();
    }
    m_options = o;
}

bool VoiceEffects::isActive() const
{
    return m_options.highPass || m_options.bassDb != 0 || m_options.trebleDb != 0 || m_options.pitchSemitones != 0;
}

void VoiceEffects::process(std::span<float> samples)
{
    if (m_options.highPass) {
        m_highPass.process(samples);
    }
    if (m_options.bassDb != 0) {
        m_bass.process(samples);
    }
    if (m_options.trebleDb != 0) {
        m_treble.process(samples);
    }
    if (m_options.pitchSemitones != 0) {
        m_pitch.process(samples);
    }
}

void VoiceEffects::reset()
{
    m_highPass.reset();
    m_bass.reset();
    m_treble.reset();
    m_pitch.reset();
}

} // namespace opentalk::audio
