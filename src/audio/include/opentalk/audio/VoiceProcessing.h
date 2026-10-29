// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <memory>
#include <span>

namespace opentalk::audio {

/// Removes what OpenTalk itself played from a captured signal: other people's voices
/// coming back through the microphone (echo), or through shared computer audio. It
/// learns the path from the speakers to the capture adaptively (speexdsp).
///
/// Feed every frame sent to the speakers with playback() and clean every captured frame
/// with process(), both 48 kHz mono, 20 ms. Not thread-safe; use from one thread.
class EchoCanceller {
public:
    /// `tailMs` is the longest delay between playing and capturing that it can cancel.
    explicit EchoCanceller(int tailMs = 200);
    ~EchoCanceller();
    EchoCanceller(const EchoCanceller &) = delete;
    EchoCanceller &operator=(const EchoCanceller &) = delete;

    void playback(std::span<const float> frame);
    /// Cleans one captured frame in place.
    void process(std::span<float> frame);
    void reset();
    /// Forgets the frames played but not yet matched, keeping what was learned. For
    /// when capturing resumes after a gap.
    void resync();
    /// The underlying speexdsp state, for VoiceProcessor's residual echo suppression.
    void *nativeHandle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

/// Cleans up the microphone signal before it is sent: echo cancellation, noise
/// suppression and automatic gain, each optional. Not thread-safe.
class VoiceProcessor {
public:
    struct Options {
        bool noiseSuppression = true;
        int noiseSuppressionDb = -30; ///< How far noise is pushed down (negative dB).
        bool autoGain = false;
        bool echoCancellation = false;
        friend bool operator==(const Options &, const Options &) = default;
    };

    VoiceProcessor();
    ~VoiceProcessor();
    VoiceProcessor(const VoiceProcessor &) = delete;
    VoiceProcessor &operator=(const VoiceProcessor &) = delete;

    void setOptions(const Options &options);
    Options options() const { return m_options; }
    bool isActive() const { return m_options.noiseSuppression || m_options.autoGain || m_options.echoCancellation; }

    /// A frame just sent to the speakers (used only with echo cancellation).
    void playback(std::span<const float> frame);
    /// Processes one captured frame in place.
    void process(std::span<float> frame);
    /// See EchoCanceller::resync().
    void resync();
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    Options m_options;
};

} // namespace opentalk::audio
