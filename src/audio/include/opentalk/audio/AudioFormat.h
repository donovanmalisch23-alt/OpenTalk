// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QtGlobal>

namespace opentalk::audio {

/// OpenTalk processes all audio as 48 kHz mono 32-bit float in 20 ms frames. Device
/// backends convert to and from whatever the hardware uses.
inline constexpr int kSampleRate = 48000;
inline constexpr int kFrameMs = 20;
inline constexpr int kFrameSamples = kSampleRate * kFrameMs / 1000; // 960
/// Largest Opus packet for one frame (RFC 6716).
inline constexpr int kMaxOpusPacketBytes = 1275;

/// What an audio stream carries. Receivers label media and shared system audio so
/// listeners always know it is not someone's microphone. Values are part of the wire format.
enum class StreamType : quint8 {
    Voice = 0,
    Media = 1,       ///< A file played into the channel (Phase 7).
    SystemAudio = 2, ///< Shared computer sound (Phase 7).
};

} // namespace opentalk::audio
