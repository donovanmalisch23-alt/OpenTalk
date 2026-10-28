// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/protocol/VoicePacket.h"

#include <QtEndian>

namespace opentalk::protocol {
namespace {

constexpr qsizetype kAudioHeaderBytes = 1 + 2 + 4;

const uchar *bytes(QByteArrayView view)
{
    return reinterpret_cast<const uchar *>(view.data());
}

} // namespace

QByteArray encodeVoiceAudio(const AudioData &audio)
{
    QByteArray out(kAudioHeaderBytes, Qt::Uninitialized);
    auto *p = reinterpret_cast<uchar *>(out.data());
    p[0] = static_cast<uchar>(audio.stream);
    qToBigEndian(audio.sequence, p + 1);
    qToBigEndian(audio.userId, p + 3);
    out.append(audio.payload);
    return out;
}

std::optional<AudioData> decodeVoiceAudio(QByteArrayView plaintext)
{
    if (plaintext.size() <= kAudioHeaderBytes || plaintext.size() > kAudioHeaderBytes + kMaxAudioPayloadBytes) {
        return std::nullopt;
    }
    const uchar *p = bytes(plaintext);
    if (p[0] > static_cast<uchar>(AudioStream::SystemAudio)) {
        return std::nullopt;
    }
    AudioData audio;
    audio.stream = static_cast<AudioStream>(p[0]);
    audio.sequence = qFromBigEndian<quint16>(p + 1);
    audio.userId = qFromBigEndian<quint32>(p + 3);
    audio.payload = plaintext.sliced(kAudioHeaderBytes).toByteArray();
    return audio;
}

QByteArray encodeVoicePing(const VoicePing &ping)
{
    QByteArray out(9, Qt::Uninitialized);
    auto *p = reinterpret_cast<uchar *>(out.data());
    qToBigEndian(ping.token, p);
    p[8] = ping.flags;
    return out;
}

std::optional<VoicePing> decodeVoicePing(QByteArrayView plaintext)
{
    if (plaintext.size() != 9) {
        return std::nullopt;
    }
    return VoicePing{qFromBigEndian<quint64>(bytes(plaintext)), bytes(plaintext)[8]};
}

QByteArray encodeVoicePong(quint64 token)
{
    QByteArray out(8, Qt::Uninitialized);
    qToBigEndian(token, reinterpret_cast<uchar *>(out.data()));
    return out;
}

std::optional<quint64> decodeVoicePong(QByteArrayView plaintext)
{
    if (plaintext.size() != 8) {
        return std::nullopt;
    }
    return qFromBigEndian<quint64>(bytes(plaintext));
}

} // namespace opentalk::protocol
