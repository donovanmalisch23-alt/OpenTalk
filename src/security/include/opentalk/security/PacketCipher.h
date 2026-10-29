// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QtGlobal>

#include <optional>

namespace opentalk::security {

/// Authenticated encryption for UDP voice datagrams (ADR 0008).
///
/// Wire layout of one datagram:
///
///     header (13 bytes, sent in the clear but authenticated)
///       u8  kind         what the plaintext is (audio, ping, pong)
///       u32 sessionId    which session's keys to use (big-endian)
///       u64 counter      per-direction packet number, starting at 1 (big-endian)
///     ciphertext         XChaCha20-Poly1305 of the plaintext, with a 16-byte tag
///
/// The 24-byte nonce is the counter (8 bytes, big-endian), the direction (1 byte) and
/// 15 zero bytes. Each direction of each session has its own random 32-byte key, sent
/// over the TLS connection, so a nonce never repeats under one key. Receivers reject
/// forged, damaged and replayed datagrams.
inline constexpr qsizetype kPacketKeyBytes = 32;
inline constexpr qsizetype kPacketHeaderBytes = 13;
inline constexpr qsizetype kPacketTagBytes = 16;
inline constexpr qsizetype kPacketOverheadBytes = kPacketHeaderBytes + kPacketTagBytes;

enum class PacketDirection : quint8 { ClientToServer = 0, ServerToClient = 1 };

struct PacketHeader {
    quint8 kind = 0;
    quint32 sessionId = 0;
    quint64 counter = 0;
};

/// 32 random bytes from the operating system's secure generator.
QByteArray randomPacketKey();

/// Reads the clear-text header. Does not authenticate anything.
std::optional<PacketHeader> peekPacketHeader(QByteArrayView datagram);

/// Encrypts outgoing datagrams for one direction of one session.
class PacketSealer {
public:
    PacketSealer() = default;
    PacketSealer(QByteArray key, PacketDirection direction, quint32 sessionId);

    bool isValid() const { return m_key.size() == kPacketKeyBytes; }
    /// Returns the complete datagram, or an empty array if the sealer is not set up.
    QByteArray seal(quint8 kind, QByteArrayView plaintext);
    quint64 packetsSealed() const { return m_counter; }

private:
    QByteArray m_key;
    PacketDirection m_direction = PacketDirection::ClientToServer;
    quint32 m_sessionId = 0;
    quint64 m_counter = 0;
};

/// Decrypts incoming datagrams for one direction of one session and rejects replays
/// with a 64-packet sliding window.
class PacketOpener {
public:
    PacketOpener() = default;
    PacketOpener(QByteArray key, PacketDirection direction, quint32 sessionId);

    bool isValid() const { return m_key.size() == kPacketKeyBytes; }
    /// Returns the plaintext, or nothing if the datagram is not authentic, belongs to
    /// another session, or was already received.
    std::optional<QByteArray> open(QByteArrayView datagram, PacketHeader *header = nullptr);

private:
    QByteArray m_key;
    PacketDirection m_direction = PacketDirection::ClientToServer;
    quint32 m_sessionId = 0;
    quint64 m_highest = 0;
    quint64 m_window = 0; ///< Bit i set = counter (m_highest - i) was received.
};

} // namespace opentalk::security
