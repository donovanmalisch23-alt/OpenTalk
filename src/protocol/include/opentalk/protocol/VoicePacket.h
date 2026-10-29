// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/protocol/Messages.h"

#include <QByteArray>
#include <QByteArrayView>

#include <optional>

namespace opentalk::protocol {

/// Plaintext carried inside an encrypted UDP voice datagram. The datagram header and
/// encryption are in security/PacketCipher.h; see docs/protocol/voice.md.
enum class VoicePacketKind : quint8 {
    Audio = 1, ///< u8 stream, u16 sequence, u32 userId (0 from clients), Opus payload.
    Ping = 2,  ///< u64 token, u8 flags. Sent by clients to open and keep the UDP path.
    Pong = 3,  ///< u64 token. The server's answer, sent to the address the ping came from.
};

/// Ping flag: the client has been receiving the server's UDP packets, so the server may
/// send voice to it over UDP.
inline constexpr quint8 kVoicePingReceivingUdp = 0x01;

QByteArray encodeVoiceAudio(const AudioData &audio);
/// Validates stream, sequence and payload length. Never trusts its input.
std::optional<AudioData> decodeVoiceAudio(QByteArrayView plaintext);

struct VoicePing {
    quint64 token = 0;
    quint8 flags = 0;
};

QByteArray encodeVoicePing(const VoicePing &ping);
std::optional<VoicePing> decodeVoicePing(QByteArrayView plaintext);
QByteArray encodeVoicePong(quint64 token);
std::optional<quint64> decodeVoicePong(QByteArrayView plaintext);

} // namespace opentalk::protocol
