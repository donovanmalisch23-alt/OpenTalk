// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/audio/JitterBuffer.h"

#include "opentalk/audio/AudioFormat.h"

#include <algorithm>
#include <cmath>

namespace opentalk::audio {

qint64 JitterBuffer::unwrap(quint16 sequence) const
{
    // Choose the unwrapped value closest to the last packet we saw (16-bit wrap-around).
    const qint64 reference = m_playing ? m_next : m_lastInserted;
    const auto delta = static_cast<qint16>(static_cast<quint16>(sequence - static_cast<quint16>(reference)));
    return reference + delta;
}

void JitterBuffer::insert(quint16 sequence, const QByteArray &payload, qint64 arrivalMs)
{
    if (payload.isEmpty()) {
        return;
    }
    const qint64 seq = m_haveReference ? unwrap(sequence) : static_cast<qint64>(sequence);
    if (m_playing && seq < m_next) {
        return; // too late: that moment has already been played
    }
    // Interarrival jitter estimate (RFC 3550, section 6.4.1), in milliseconds.
    if (m_haveReference) {
        const double transitDifference =
            static_cast<double>(arrivalMs - m_lastArrivalMs) - static_cast<double>(seq - m_lastArrivalSeq) * kFrameMs;
        m_jitterMs += (std::fabs(transitDifference) - m_jitterMs) / 16.0;
    }
    m_haveReference = true;
    m_lastArrivalMs = arrivalMs;
    m_lastArrivalSeq = seq;
    m_lastInserted = std::max(m_lastInserted, seq);
    if (!m_playing && m_packets.isEmpty()) {
        m_lastInserted = seq;
    }

    m_packets.insert(seq, payload);
    while (m_packets.size() > kMaxStored) {
        m_packets.erase(m_packets.begin());
    }
}

JitterBuffer::Result JitterBuffer::pop(QByteArray *payload, QByteArray *fecSource)
{
    payload->clear();
    fecSource->clear();
    if (!m_playing) {
        if (m_packets.size() < m_target) {
            return Result::Empty;
        }
        m_playing = true;
        m_next = m_packets.firstKey();
        m_missingInRow = 0;
    }
    // Catch up after a stall instead of keeping the extra delay for the rest of the spurt.
    while (m_packets.size() > m_target + 4) {
        m_packets.erase(m_packets.begin());
        m_next = std::max(m_next, m_packets.firstKey());
    }
    // Drop anything that slipped behind the playback position.
    while (!m_packets.isEmpty() && m_packets.firstKey() < m_next) {
        m_packets.erase(m_packets.begin());
    }

    if (auto it = m_packets.find(m_next); it != m_packets.end()) {
        *payload = it.value();
        m_packets.erase(it);
        ++m_next;
        m_missingInRow = 0;
        return Result::Packet;
    }
    ++m_missingInRow;
    if (m_packets.isEmpty() && m_missingInRow > kEndAfterMissing) {
        // The talk spurt is over. Choose the target for the next one from current jitter.
        reset();
        return Result::Empty;
    }
    if (auto following = m_packets.find(m_next + 1); following != m_packets.end()) {
        *fecSource = following.value();
    }
    ++m_next;
    return Result::Lost;
}

void JitterBuffer::reset()
{
    m_packets.clear();
    m_playing = false;
    m_missingInRow = 0;
    const double frames = std::ceil((2.0 * m_jitterMs + kFrameMs) / kFrameMs);
    m_target = std::clamp(static_cast<int>(frames), kMinTarget, kMaxTarget);
}

} // namespace opentalk::audio
