// SPDX-License-Identifier: GPL-3.0-only
// Voice processing measured on generated signals: noise suppression lowers steady
// noise but keeps speech, automatic gain lifts quiet speech, and echo cancellation
// removes what was played to the speakers while keeping the local talker.

#include "opentalk/audio/AudioFormat.h"
#include "opentalk/audio/VoiceProcessing.h"

#include <QRandomGenerator>
#include <QTest>

#include <cmath>
#include <numbers>
#include <vector>

using namespace opentalk::audio;

namespace {

using Signal = std::vector<float>;

double rmsDb(const Signal &s, std::size_t from, std::size_t to)
{
    double sum = 0;
    for (std::size_t i = from; i < to && i < s.size(); ++i) {
        sum += static_cast<double>(s[i]) * s[i];
    }
    const double mean = sum / static_cast<double>(std::max<std::size_t>(1, std::min(to, s.size()) - from));
    return 10.0 * std::log10(std::max(mean, 1e-12));
}

/// The last `seconds` of a signal (after the algorithms have adapted).
double tailDb(const Signal &s, double seconds)
{
    const auto n = static_cast<std::size_t>(seconds * kSampleRate);
    return rmsDb(s, s.size() - n, s.size());
}

Signal noise(double seconds, double dbfs, quint32 seed)
{
    QRandomGenerator random(seed);
    const double amplitude = std::pow(10.0, dbfs / 20.0) * std::sqrt(3.0); // uniform noise with that RMS
    Signal s(static_cast<std::size_t>(seconds * kSampleRate));
    for (float &x : s) {
        x = static_cast<float>((random.generateDouble() * 2.0 - 1.0) * amplitude);
    }
    return s;
}

/// Something like speech: a voiced harmonic sound whose pitch wanders, in syllables of
/// about 150 ms separated by short pauses.
Signal speech(double seconds, double dbfs, double basePitch)
{
    Signal s(static_cast<std::size_t>(seconds * kSampleRate));
    double phase = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        const double pitch = basePitch * (1.0 + 0.15 * std::sin(2 * std::numbers::pi * 1.3 * t));
        phase += 2 * std::numbers::pi * pitch / kSampleRate;
        double voiced = 0;
        for (int h = 1; h <= 8; ++h) {
            voiced += std::sin(h * phase) / h;
        }
        const double syllable = std::max(0.0, std::sin(2 * std::numbers::pi * 3.5 * t)); // 3.5 per second
        s[i] = static_cast<float>(voiced * syllable);
    }
    const double scale = std::pow(10.0, (dbfs - rmsDb(s, 0, s.size())) / 20.0);
    for (float &x : s) {
        x = static_cast<float>(x * scale);
    }
    return s;
}

Signal mix(const Signal &a, const Signal &b)
{
    Signal out(std::max(a.size(), b.size()), 0.0f);
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = (i < a.size() ? a[i] : 0.0f) + (i < b.size() ? b[i] : 0.0f);
    }
    return out;
}

Signal delayed(const Signal &s, int samples, float gain)
{
    Signal out(s.size(), 0.0f);
    for (std::size_t i = static_cast<std::size_t>(samples); i < s.size(); ++i) {
        out[i] = s[i - static_cast<std::size_t>(samples)] * gain;
    }
    return out;
}

/// Runs a processor over a whole signal, frame by frame, giving it `played` as the
/// speaker signal when there is one.
template <typename Processor> Signal run(Processor &processor, Signal captured, const Signal *played = nullptr)
{
    for (std::size_t start = 0; start + kFrameSamples <= captured.size(); start += kFrameSamples) {
        if (played != nullptr) {
            processor.playback(std::span<const float>(played->data() + start, kFrameSamples));
        }
        processor.process(std::span<float>(captured.data() + start, kFrameSamples));
    }
    return captured;
}

} // namespace

class TestVoiceProcessing : public QObject {
    Q_OBJECT

private slots:
    void everythingOffLeavesTheSignalAlone()
    {
        VoiceProcessor processor;
        processor.setOptions({false, -30, false, false});
        QVERIFY(!processor.isActive());
        const Signal in = speech(1.0, -20, 150);
        QVERIFY(run(processor, in) == in);
    }

    void steadyNoiseIsSuppressed()
    {
        VoiceProcessor processor;
        processor.setOptions({true, -30, false, false});
        const Signal in = noise(4.0, -35, 1);
        const Signal out = run(processor, in);
        const double reduction = tailDb(in, 2.0) - tailDb(out, 2.0);
        QVERIFY2(reduction >= 12.0, qPrintable(QStringLiteral("noise lowered by only %1 dB").arg(reduction)));
    }

    void speechSurvivesNoiseSuppression()
    {
        VoiceProcessor processor;
        processor.setOptions({true, -30, false, false});
        const Signal talk = speech(4.0, -20, 140);
        const Signal out = run(processor, mix(talk, noise(4.0, -45, 2)));
        const double change = tailDb(out, 2.0) - tailDb(talk, 2.0);
        QVERIFY2(change > -6.0 && change < 3.0,
                 qPrintable(QStringLiteral("speech level changed by %1 dB").arg(change)));
    }

    void automaticGainLiftsQuietSpeech()
    {
        VoiceProcessor processor;
        processor.setOptions({false, -30, true, false});
        const Signal in = speech(8.0, -42, 160);
        const Signal out = run(processor, in);
        const double gain = tailDb(out, 2.0) - tailDb(in, 2.0);
        QVERIFY2(gain >= 8.0, qPrintable(QStringLiteral("quiet speech lifted by only %1 dB").arg(gain)));
    }

    void echoOfThePlayedSoundIsRemoved()
    {
        // What others say comes out of the speakers and back into the microphone 40 ms
        // later, a little quieter.
        const Signal played = speech(6.0, -20, 180);
        const Signal echo = delayed(played, kSampleRate * 40 / 1000, 0.4f);
        EchoCanceller canceller(200);
        const Signal out = run(canceller, echo, &played);
        const double reduction = tailDb(echo, 2.0) - tailDb(out, 2.0);
        QVERIFY2(reduction >= 15.0, qPrintable(QStringLiteral("echo lowered by only %1 dB").arg(reduction)));
    }

    void localTalkerIsKeptWhileEchoIsRemoved()
    {
        // The other person talks first (the canceller learns), then both talk at once.
        const Signal played = speech(7.0, -20, 180);
        Signal me = speech(7.0, -22, 110);
        std::fill(me.begin(), me.begin() + 3 * kSampleRate, 0.0f);
        const Signal echo = delayed(played, kSampleRate * 30 / 1000, 0.4f);
        const Signal microphone = mix(me, echo);

        // The canceller subtracts its estimate of the echo. It also high-pass filters its
        // input (an inaudible phase change at low frequencies), so compare with what it
        // makes of my voice alone: the difference is the echo it missed.
        EchoCanceller canceller(200);
        const Signal cancelled = run(canceller, microphone, &played);
        EchoCanceller reference(200);
        const Signal silence(played.size(), 0.0f);
        const Signal meAlone = run(reference, me, &silence);
        Signal missed(cancelled.size());
        for (std::size_t i = 0; i < cancelled.size(); ++i) {
            missed[i] = cancelled[i] - meAlone[i];
        }
        const double echoReduction = tailDb(echo, 2.0) - tailDb(missed, 2.0);
        qInfo("double talk: echo lowered by %.1f dB while I talk", echoReduction);
        QVERIFY2(echoReduction >= 10.0,
                 qPrintable(QStringLiteral("echo lowered by only %1 dB while I talk").arg(echoReduction)));

        // The full processor (whose noise suppression also removes what the canceller
        // misses) keeps my voice.
        VoiceProcessor processor;
        processor.setOptions({true, -30, false, true});
        const Signal out = run(processor, microphone, &played);
        const double kept = tailDb(out, 2.0) - tailDb(me, 2.0);
        // While both talk, the residual echo suppression makes me a few dB quieter, not silent.
        QVERIFY2(kept > -8.0 && kept < 3.0, qPrintable(QStringLiteral("my voice changed by %1 dB").arg(kept)));
        // ...and while only the other person talks, almost nothing goes out.
        const double echoOnly =
            rmsDb(echo, 2 * kSampleRate, 3 * kSampleRate) - rmsDb(out, 2 * kSampleRate, 3 * kSampleRate);
        QVERIFY2(echoOnly >= 20.0, qPrintable(QStringLiteral("echo alone lowered by only %1 dB").arg(echoOnly)));
    }

    void resetForgetsWhatWasLearned()
    {
        const Signal played = speech(3.0, -20, 180);
        EchoCanceller canceller(200);
        run(canceller, delayed(played, 1920, 0.4f), &played);
        canceller.reset();
        // Right after a reset nothing has been learned, so the first frame passes almost unchanged.
        Signal frame(played.begin() + kSampleRate, played.begin() + kSampleRate + kFrameSamples);
        const Signal before = frame;
        canceller.playback(std::span<const float>(played.data(), kFrameSamples));
        canceller.process(frame);
        QVERIFY(std::abs(rmsDb(frame, 0, frame.size()) - rmsDb(before, 0, before.size())) < 3.0);
    }
};

QTEST_GUILESS_MAIN(TestVoiceProcessing)
#include "tst_voice_processing.moc"
