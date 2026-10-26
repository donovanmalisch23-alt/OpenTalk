// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QtGlobal>

#include <optional>

namespace opentalk::protocol {

/// Control-channel frame layout (all integers big-endian). See docs/protocol/proposal.md.
///
///   offset 0  u32  payload length in bytes (header not included)
///   offset 4  u16  message type
///   offset 6  u16  flags (reserved; must be zero in protocol version 1)
///   offset 8  ...  payload
inline constexpr qsizetype kFrameHeaderSize = 8;

/// Default upper bound for one control message. Large enough for any planned control
/// message, small enough that a malicious peer cannot make us allocate much memory.
inline constexpr quint32 kDefaultMaxPayloadSize = 64 * 1024;

struct Frame {
    quint16 type = 0;
    QByteArray payload;

    friend bool operator==(const Frame &, const Frame &) = default;
};

/// Builds the bytes for one frame. Returns nullopt if the payload exceeds `maxPayloadSize`.
std::optional<QByteArray> encodeFrame(quint16 type, QByteArrayView payload,
                                      quint32 maxPayloadSize = kDefaultMaxPayloadSize);

enum class DecodeError {
    None,
    PayloadTooLarge,  ///< Header announced a payload above the configured limit.
    ReservedFlagsSet, ///< Flags field was non-zero; not valid in protocol version 1.
};

/// Incrementally splits a byte stream into frames.
///
/// It never buffers more than one incomplete frame, so memory use is bounded by
/// kFrameHeaderSize + maxPayloadSize plus the size of a single feed() call. After any
/// error the decoder stays failed and the caller must close the connection: once framing
/// is lost there is no safe way to resynchronise a stream.
class FrameDecoder {
public:
    explicit FrameDecoder(quint32 maxPayloadSize = kDefaultMaxPayloadSize);

    /// Consumes `bytes` and appends every completed frame to `frames`.
    /// Returns the decoder's error state after processing.
    DecodeError feed(QByteArrayView bytes, QList<Frame> &frames);

    DecodeError error() const { return m_error; }
    bool hasFailed() const { return m_error != DecodeError::None; }
    qsizetype bufferedBytes() const { return m_buffer.size(); }
    quint32 maxPayloadSize() const { return m_maxPayloadSize; }

private:
    quint32 m_maxPayloadSize;
    QByteArray m_buffer;
    DecodeError m_error = DecodeError::None;
};

} // namespace opentalk::protocol
