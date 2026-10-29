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
        // Too late: that moment has already been played. If it was concealed, the
        // buffer is too short for this network; grow it by one frame.
        if (m_concealed.contains(seq)) {
            ++m_lateCount;
            ++m_windowLate;
            m_lateInWindow = true;
            m_lateInSpurt = true;
            // Grow at most one frame beyond what the measured jitter calls for, so a
            // few unusually late packets do not keep adding delay.
            if (m_target < std::min(kMaxTarget, targetForJitter() + 1)) {
                ++m_target;
                m_growPending = true;
            }
        }
        return;
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
        m_windowPops = 0;
        m_windowMinBuffered = kMaxStored;
    }
    if (m_growPending) {
        // Play one concealed frame without advancing: the buffer becomes 20 ms longer.
        m_growPending = false;
        return Result::Lost;
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
        adaptDuringSpurt();
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
    } else {
        ++m_windowUnrecovered;
    }
    m_concealed.append(m_next);
    if (m_concealed.size() > 16) {
        m_concealed.removeFirst();
    }
    ++m_next;
    return Result::Lost;
}

int JitterBuffer::targetForJitter() const
{
    // Enough to cover about twice the measured jitter, plus the frame being received.
    const double frames = std::ceil((2.0 * m_jitterMs + kFrameMs) / kFrameMs);
    return std::clamp(static_cast<int>(frames), kMinTarget, kMaxTarget);
}

void JitterBuffer::adaptDuringSpurt()
{
    // Every 2 seconds of continuous speech: if the buffer never ran down to its target,
    // it holds more delay than this network needs, so skip one frame to shorten it.
    m_windowMinBuffered = std::min(m_windowMinBuffered, static_cast<int>(m_packets.size()));
    if (++m_windowPops < kAdaptWindowFrames) {
        return;
    }
    // Losses that error correction could not repair mean the next packet was not here
    // in time; one more frame of buffer lets correction work on lossy networks.
    // (Packets that came late were counted as unrepaired too; they are not losses.)
    const bool lossy = (m_windowUnrecovered - m_windowLate) * 100 >= kLossyPercent * kAdaptWindowFrames;
    if (lossy && m_lossFloor < kMinTarget + 1) {
        m_lossFloor = kMinTarget + 1;
        if (m_target < m_lossFloor) {
            m_target = m_lossFloor;
            m_growPending = true;
        }
    }
    // Growing happens only when packets actually come too late (see insert) or losses
    // need it; the jitter estimate alone only decides how far the buffer may shrink.
    m_target = std::max({targetForJitter(), m_lossFloor, m_lateInWindow ? m_target : kMinTarget});
    if (!m_lateInWindow && m_windowMinBuffered >= m_target && !m_packets.isEmpty()) {
        m_packets.erase(m_packets.begin());
        m_next = m_packets.isEmpty() ? m_next + 1 : m_packets.firstKey();
    }
    m_windowUnrecovered = 0;
    m_windowLate = 0;
    m_windowPops = 0;
    m_windowMinBuffered = kMaxStored;
    m_lateInWindow = false;
}

void JitterBuffer::reset()
{
    m_packets.clear();
    m_playing = false;
    m_missingInRow = 0;
    m_growPending = false;
    m_concealed.clear();
    // Between talk spurts the target follows the measured jitter, but does not drop
    // below what late packets in the last spurt showed was needed.
    const int wanted = std::max(targetForJitter(), m_lossFloor);
    m_target = m_lateInSpurt ? std::max(m_target, wanted) : wanted;
    m_lateInSpurt = false;
    m_lateInWindow = false;
}

} // namespace opentalk::audio
