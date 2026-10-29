// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/audio/OpusCodec.h"

#include <opus.h>

#include <algorithm>

namespace opentalk::audio {

OpusEncoderWrapper::OpusEncoderWrapper(Content content, int bitrate)
{
    int error = OPUS_OK;
    m_encoder = opus_encoder_create(kSampleRate, 1,
                                    content == Content::Voice ? OPUS_APPLICATION_VOIP : OPUS_APPLICATION_AUDIO, &error);
    if (error != OPUS_OK) {
        m_encoder = nullptr;
        return;
    }
    opus_encoder_ctl(m_encoder, OPUS_SET_SIGNAL(content == Content::Voice ? OPUS_SIGNAL_VOICE : OPUS_SIGNAL_MUSIC));
    // In-band FEC lets the receiver rebuild a lost frame from the next packet.
    opus_encoder_ctl(m_encoder, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(m_encoder, OPUS_SET_PACKET_LOSS_PERC(10));
    setBitrate(bitrate);
}

OpusEncoderWrapper::~OpusEncoderWrapper()
{
    if (m_encoder != nullptr) {
        opus_encoder_destroy(m_encoder);
    }
}

void OpusEncoderWrapper::setBitrate(int bitsPerSecond)
{
    if (m_encoder != nullptr) {
        opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(std::clamp(bitsPerSecond, 6000, 256000)));
    }
}

QByteArray OpusEncoderWrapper::encode(std::span<const float> frame)
{
    if (m_encoder == nullptr || frame.size() != static_cast<std::size_t>(kFrameSamples)) {
        return {};
    }
    QByteArray out(kMaxOpusPacketBytes, Qt::Uninitialized);
    const opus_int32 length = opus_encode_float(m_encoder, frame.data(), kFrameSamples,
                                                reinterpret_cast<unsigned char *>(out.data()), kMaxOpusPacketBytes);
    if (length < 0) {
        return {};
    }
    out.truncate(length);
    return out;
}

OpusDecoderWrapper::OpusDecoderWrapper()
{
    int error = OPUS_OK;
    m_decoder = opus_decoder_create(kSampleRate, 1, &error);
    if (error != OPUS_OK) {
        m_decoder = nullptr;
    }
}

OpusDecoderWrapper::~OpusDecoderWrapper()
{
    if (m_decoder != nullptr) {
        opus_decoder_destroy(m_decoder);
    }
}

bool OpusDecoderWrapper::decode(const QByteArray &packet, std::span<float> out)
{
    std::fill(out.begin(), out.end(), 0.0f);
    if (m_decoder == nullptr || out.size() < static_cast<std::size_t>(kFrameSamples) || packet.isEmpty() ||
        packet.size() > kMaxOpusPacketBytes) {
        return false;
    }
    const int samples = opus_decode_float(m_decoder, reinterpret_cast<const unsigned char *>(packet.constData()),
                                          static_cast<opus_int32>(packet.size()), out.data(), kFrameSamples, 0);
    if (samples != kFrameSamples) {
        std::fill(out.begin(), out.end(), 0.0f);
        return false;
    }
    return true;
}

void OpusDecoderWrapper::conceal(const QByteArray &nextPacket, std::span<float> out)
{
    std::fill(out.begin(), out.end(), 0.0f);
    if (m_decoder == nullptr || out.size() < static_cast<std::size_t>(kFrameSamples)) {
        return;
    }
    int samples = -1;
    if (!nextPacket.isEmpty() && nextPacket.size() <= kMaxOpusPacketBytes) {
        samples = opus_decode_float(m_decoder, reinterpret_cast<const unsigned char *>(nextPacket.constData()),
                                    static_cast<opus_int32>(nextPacket.size()), out.data(), kFrameSamples, 1);
    }
    if (samples != kFrameSamples) {
        samples = opus_decode_float(m_decoder, nullptr, 0, out.data(), kFrameSamples, 0);
    }
    if (samples != kFrameSamples) {
        std::fill(out.begin(), out.end(), 0.0f);
    }
}

} // namespace opentalk::audio
