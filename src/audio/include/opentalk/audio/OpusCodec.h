// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/audio/AudioFormat.h"

#include <QByteArray>

#include <memory>
#include <span>

struct OpusEncoder;
struct OpusDecoder;

namespace opentalk::audio {

/// Opus encoder for one outgoing stream (48 kHz mono, 20 ms frames).
class OpusEncoderWrapper {
public:
    enum class Content { Voice, Music };

    OpusEncoderWrapper(Content content, int bitrate);
    ~OpusEncoderWrapper();
    OpusEncoderWrapper(const OpusEncoderWrapper &) = delete;
    OpusEncoderWrapper &operator=(const OpusEncoderWrapper &) = delete;

    bool isValid() const { return m_encoder != nullptr; }
    void setBitrate(int bitsPerSecond);
    /// Encodes exactly kFrameSamples samples. Returns an empty array on failure.
    QByteArray encode(std::span<const float> frame);

private:
    OpusEncoder *m_encoder = nullptr;
};

/// Opus decoder for one incoming stream, with packet-loss concealment.
class OpusDecoderWrapper {
public:
    OpusDecoderWrapper();
    ~OpusDecoderWrapper();
    OpusDecoderWrapper(const OpusDecoderWrapper &) = delete;
    OpusDecoderWrapper &operator=(const OpusDecoderWrapper &) = delete;

    bool isValid() const { return m_decoder != nullptr; }
    /// Decodes one packet into `out` (kFrameSamples). Returns false (and writes
    /// silence) if the packet is corrupt.
    bool decode(const QByteArray &packet, std::span<float> out);
    /// Fills a lost frame: from the forward-error-correction data in the *next* packet if
    /// given, otherwise by concealment.
    void conceal(const QByteArray &nextPacket, std::span<float> out);

private:
    OpusDecoder *m_decoder = nullptr;
};

} // namespace opentalk::audio
