// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/protocol/Frame.h"

#include <QtEndian>

namespace opentalk::protocol {

std::optional<QByteArray> encodeFrame(quint16 type, QByteArrayView payload, quint32 maxPayloadSize)
{
    if (payload.size() < 0 || static_cast<quint64>(payload.size()) > maxPayloadSize) {
        return std::nullopt;
    }
    QByteArray bytes(kFrameHeaderSize + payload.size(), Qt::Uninitialized);
    auto *out = reinterpret_cast<uchar *>(bytes.data());
    qToBigEndian<quint32>(static_cast<quint32>(payload.size()), out);
    qToBigEndian<quint16>(type, out + 4);
    qToBigEndian<quint16>(0, out + 6);
    if (!payload.isEmpty()) {
        std::copy(payload.begin(), payload.end(), bytes.begin() + kFrameHeaderSize);
    }
    return bytes;
}

FrameDecoder::FrameDecoder(quint32 maxPayloadSize)
    : m_maxPayloadSize(maxPayloadSize)
{
}

DecodeError FrameDecoder::feed(QByteArrayView bytes, QList<Frame> &frames)
{
    if (hasFailed()) {
        return m_error;
    }
    m_buffer.append(bytes);

    qsizetype offset = 0;
    while (m_buffer.size() - offset >= kFrameHeaderSize) {
        const auto *header = reinterpret_cast<const uchar *>(m_buffer.constData() + offset);
        const quint32 length = qFromBigEndian<quint32>(header);
        const quint16 type = qFromBigEndian<quint16>(header + 4);
        const quint16 flags = qFromBigEndian<quint16>(header + 6);

        // Validate the header before waiting for the payload, so an oversized announcement
        // is rejected immediately instead of after we have buffered it.
        if (length > m_maxPayloadSize) {
            m_error = DecodeError::PayloadTooLarge;
        } else if (flags != 0) {
            m_error = DecodeError::ReservedFlagsSet;
        }
        if (hasFailed()) {
            m_buffer.clear();
            m_buffer.squeeze();
            return m_error;
        }

        const qsizetype frameSize = kFrameHeaderSize + static_cast<qsizetype>(length);
        if (m_buffer.size() - offset < frameSize) {
            break;
        }
        frames.append(Frame{type, m_buffer.mid(offset + kFrameHeaderSize, length)});
        offset += frameSize;
    }
    m_buffer.remove(0, offset);
    return m_error;
}

} // namespace opentalk::protocol
