// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/audio/VoiceProcessing.h"

#include "opentalk/audio/AudioFormat.h"

#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

namespace opentalk::audio {
namespace {

void toInt16(std::span<const float> in, std::vector<spx_int16_t> &out)
{
    out.resize(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        out[i] = static_cast<spx_int16_t>(std::lround(std::clamp(in[i], -1.0f, 1.0f) * 32767.0f));
    }
}

void toFloat(const std::vector<spx_int16_t> &in, std::span<float> out)
{
    for (std::size_t i = 0; i < out.size() && i < in.size(); ++i) {
        out[i] = static_cast<float>(in[i]) / 32768.0f;
    }
}

} // namespace

// ------------------------------------------------------------------ EchoCanceller

struct EchoCanceller::Impl {
    SpeexEchoState *state = nullptr;
    /// Frames played but not yet matched with a captured frame. The synchronous
    /// speexdsp call is used with this queue (rather than its own playback buffer) so a
    /// device that stops or starts cannot make the reference drift beyond the tail.
    std::deque<std::vector<spx_int16_t>> played;
    std::size_t maxQueued = 10;
    std::vector<spx_int16_t> captured;
    std::vector<spx_int16_t> cleaned;
    std::vector<spx_int16_t> silence;
};

EchoCanceller::EchoCanceller(int tailMs)
    : d(std::make_unique<Impl>())
{
    const int tail = kSampleRate / 1000 * std::clamp(tailMs, 20, 1000);
    d->state = speex_echo_state_init(kFrameSamples, tail);
    int rate = kSampleRate;
    speex_echo_ctl(d->state, SPEEX_ECHO_SET_SAMPLING_RATE, &rate);
    d->maxQueued = static_cast<std::size_t>(std::max(2, tail / kFrameSamples));
    d->silence.assign(kFrameSamples, 0);
}

EchoCanceller::~EchoCanceller()
{
    speex_echo_state_destroy(d->state);
}

void EchoCanceller::playback(std::span<const float> frame)
{
    if (frame.size() != static_cast<std::size_t>(kFrameSamples)) {
        return;
    }
    std::vector<spx_int16_t> copy;
    toInt16(frame, copy);
    if (d->played.size() >= d->maxQueued) {
        // Nothing has been captured for a while (the microphone stalled, or its clock runs
        // slower than the speakers'). Start again from the present: a reference that is
        // older than the echo it should remove cannot remove anything.
        d->played.clear();
    }
    d->played.push_back(std::move(copy));
}

void EchoCanceller::process(std::span<float> frame)
{
    if (frame.size() != static_cast<std::size_t>(kFrameSamples)) {
        return;
    }
    toInt16(frame, d->captured);
    d->cleaned.resize(d->captured.size());
    const std::vector<spx_int16_t> &reference = d->played.empty() ? d->silence : d->played.front();
    speex_echo_cancellation(d->state, d->captured.data(), reference.data(), d->cleaned.data());
    if (!d->played.empty()) {
        d->played.pop_front();
    }
    toFloat(d->cleaned, frame);
}

void EchoCanceller::reset()
{
    speex_echo_state_reset(d->state);
    d->played.clear();
}

void EchoCanceller::resync()
{
    d->played.clear();
}

void *EchoCanceller::nativeHandle() const
{
    return d->state;
}

// ----------------------------------------------------------------- VoiceProcessor

struct VoiceProcessor::Impl {
    SpeexPreprocessState *preprocess = nullptr;
    EchoCanceller echo{200};
    std::vector<spx_int16_t> buffer;
};

VoiceProcessor::VoiceProcessor()
    : d(std::make_unique<Impl>())
{
    // Its voice activity detection and dereverberation are off by default and stay off
    // (OpenTalk has its own voice activation).
    d->preprocess = speex_preprocess_state_init(kFrameSamples, kSampleRate);
    setOptions(m_options);
}

VoiceProcessor::~VoiceProcessor()
{
    speex_preprocess_state_destroy(d->preprocess);
}

void VoiceProcessor::setOptions(const Options &options)
{
    const bool echoChanged = options.echoCancellation != m_options.echoCancellation;
    m_options = options;
    int denoise = options.noiseSuppression ? 1 : 0;
    speex_preprocess_ctl(d->preprocess, SPEEX_PREPROCESS_SET_DENOISE, &denoise);
    int suppress = std::clamp(options.noiseSuppressionDb, -60, -5);
    speex_preprocess_ctl(d->preprocess, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &suppress);
    int agc = options.autoGain ? 1 : 0;
    speex_preprocess_ctl(d->preprocess, SPEEX_PREPROCESS_SET_AGC, &agc);
    if (options.autoGain) {
        float level = 12000.0f; // about -9 dBFS speech loudness
        speex_preprocess_ctl(d->preprocess, SPEEX_PREPROCESS_SET_AGC_LEVEL, &level);
        int maxGain = 24; // dB; more only lifts background noise
        speex_preprocess_ctl(d->preprocess, SPEEX_PREPROCESS_SET_AGC_MAX_GAIN, &maxGain);
    }
    // With echo cancellation, the preprocessor also removes what the canceller leaves:
    // strongly while only the other side talks, gently while both do, so the local
    // voice is not cut.
    speex_preprocess_ctl(d->preprocess, SPEEX_PREPROCESS_SET_ECHO_STATE,
                         options.echoCancellation ? d->echo.nativeHandle() : nullptr);
    int echoSuppress = -40;
    speex_preprocess_ctl(d->preprocess, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS, &echoSuppress);
    int echoSuppressActive = -1;
    speex_preprocess_ctl(d->preprocess, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS_ACTIVE, &echoSuppressActive);
    if (echoChanged) {
        d->echo.reset();
    }
}

void VoiceProcessor::playback(std::span<const float> frame)
{
    if (m_options.echoCancellation) {
        d->echo.playback(frame);
    }
}

void VoiceProcessor::process(std::span<float> frame)
{
    if (!isActive() || frame.size() != static_cast<std::size_t>(kFrameSamples)) {
        return;
    }
    if (m_options.echoCancellation) {
        d->echo.process(frame);
    }
    if (m_options.noiseSuppression || m_options.autoGain || m_options.echoCancellation) {
        toInt16(frame, d->buffer);
        speex_preprocess_run(d->preprocess, d->buffer.data());
        toFloat(d->buffer, frame);
    }
}

void VoiceProcessor::resync()
{
    d->echo.resync();
}

void VoiceProcessor::reset()
{
    d->echo.reset();
    speex_preprocess_state_destroy(d->preprocess);
    d->preprocess = speex_preprocess_state_init(kFrameSamples, kSampleRate);
    const Options options = m_options;
    m_options = {};
    setOptions(options);
}

} // namespace opentalk::audio
