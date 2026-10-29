// SPDX-License-Identifier: GPL-3.0-only
// Voice effects measured on test tones: the filters have the gains they promise and the
// voice changer moves the pitch without changing the length.

#include "AudioTestUtil.h"

#include "opentalk/audio/AudioFormat.h"
#include "opentalk/audio/VoiceEffects.h"

#include <QTest>

#include <cmath>
#include <vector>

using namespace opentalk::audio;
using opentalk::test::sine;
using opentalk::test::toneStrength;

namespace {

std::vector<float> tone(double frequency, double seconds, double amplitude = 0.5)
{
    std::vector<float> s(static_cast<std::size_t>(seconds * kSampleRate));
    for (std::size_t i = 0; i < s.size(); ++i) {
        s[i] = sine(static_cast<qint64>(i), frequency, amplitude);
    }
    return s;
}

/// Runs `effect` over a tone in 20 ms frames and returns its gain at that frequency in
/// dB, measured over the second half (after the filter has settled).
template <typename Effect> double measuredGainDb(Effect &effect, double frequency)
{
    std::vector<float> s = tone(frequency, 1.0);
    const std::vector<float> in = s;
    for (std::size_t start = 0; start + kFrameSamples <= s.size(); start += kFrameSamples) {
        effect.process(std::span<float>(s.data() + start, kFrameSamples));
    }
    const auto half = static_cast<qsizetype>(s.size() / 2);
    const double out = toneStrength(s.data() + half, half, frequency);
    const double ref = toneStrength(in.data() + half, half, frequency);
    return 20.0 * std::log10(std::max(out, 1e-9) / ref);
}

} // namespace

class TestVoiceEffects : public QObject {
    Q_OBJECT

private slots:
    void highPassRemovesRumbleAndKeepsTheVoice()
    {
        Biquad lowCut = Biquad::highPass(100.0);
        QVERIFY(measuredGainDb(lowCut, 30.0) < -15.0);
        lowCut.reset();
        QVERIFY(std::abs(measuredGainDb(lowCut, 1000.0)) < 0.5);
        QVERIFY(std::abs(lowCut.responseDb(1000.0)) < 0.5);
    }

    void shelvesBoostAndCutTheirOwnRange()
    {
        Biquad bass = Biquad::lowShelf(250.0, 9.0);
        QVERIFY(std::abs(measuredGainDb(bass, 60.0) - 9.0) < 1.0);
        bass.reset();
        QVERIFY(std::abs(measuredGainDb(bass, 6000.0)) < 1.0);
        Biquad treble = Biquad::highShelf(3500.0, -9.0);
        QVERIFY(std::abs(measuredGainDb(treble, 12000.0) + 9.0) < 1.0);
        treble.reset();
        QVERIFY(std::abs(measuredGainDb(treble, 200.0)) < 1.0);
        // The calculated response agrees with the measurement.
        QVERIFY(std::abs(Biquad::lowShelf(250.0, 9.0).responseDb(60.0) - 9.0) < 1.0);
    }

    void voiceChangerMovesThePitch()
    {
        PitchShifter shifter;
        shifter.setSemitones(12); // an octave up
        std::vector<float> s = tone(300.0, 2.0, 0.3);
        for (std::size_t start = 0; start + kFrameSamples <= s.size(); start += kFrameSamples) {
            shifter.process(std::span<float>(s.data() + start, kFrameSamples));
        }
        QCOMPARE(s.size(), static_cast<std::size_t>(2 * kSampleRate)); // same length: same speed
        const auto half = static_cast<qsizetype>(s.size() / 2);
        const double octave = toneStrength(s.data() + half, half, 600.0);
        const double original = toneStrength(s.data() + half, half, 300.0);
        QVERIFY2(octave > 0.05 && octave > original * 4,
                 qPrintable(QStringLiteral("600 Hz %1, 300 Hz %2").arg(octave).arg(original)));

        shifter.setSemitones(-12); // an octave down
        shifter.reset();
        s = tone(600.0, 2.0, 0.3);
        for (std::size_t start = 0; start + kFrameSamples <= s.size(); start += kFrameSamples) {
            shifter.process(std::span<float>(s.data() + start, kFrameSamples));
        }
        QVERIFY(toneStrength(s.data() + half, half, 300.0) > toneStrength(s.data() + half, half, 600.0) * 4);
    }

    void effectsOffLeaveTheSignalAlone()
    {
        VoiceEffects effects;
        QVERIFY(!effects.isActive());
        std::vector<float> s = tone(440.0, 0.1);
        const std::vector<float> in = s;
        effects.process(s);
        QVERIFY(s == in);

        effects.setOptions({true, 30, -30, 40}); // out-of-range values are limited
        QVERIFY(effects.isActive());
        QCOMPARE(effects.options().bassDb, 12);
        QCOMPARE(effects.options().trebleDb, -12);
        QCOMPARE(effects.options().pitchSemitones, 12);
        effects.setOptions({});
        QVERIFY(!effects.isActive());
    }
};

QTEST_GUILESS_MAIN(TestVoiceEffects)
#include "tst_voice_effects.moc"
