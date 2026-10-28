// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QMap>
#include <QtGlobal>

namespace opentalk::audio {

/// Turns packets that arrive late, early, out of order or not at all into a steady
/// stream of 20 ms frames for playback.
///
/// - Playback of a talk spurt starts once `targetFrames` packets are waiting. The target
///   adapts to the measured network jitter (between kMinTarget and kMaxTarget frames).
/// - Packets older than the playback position are dropped (too late to be useful).
/// - A missing packet is reported as Lost, together with the following packet when it
///   is already here, so the decoder can rebuild the gap from Opus forward error
///   correction instead of guessing.
/// - If the buffer grows well past the target (for example after a network stall), old
///   frames are skipped to keep the delay low.
/// - After a few frames with nothing to play the spurt is over and the buffer resets.
class JitterBuffer {
public:
    static constexpr int kMinTarget = 2;
    static constexpr int kMaxTarget = 10;
    static constexpr int kMaxStored = 64;
    static constexpr int kEndAfterMissing = 3;

    enum class Result {
        Empty,  ///< Nothing to play (not talking, or still buffering).
        Packet, ///< `payload` holds the next packet.
        Lost,   ///< The next packet is missing; `fecSource` may hold the one after it.
    };

    /// Stores a packet. `arrivalMs` is a monotonic time used to measure jitter.
    void insert(quint16 sequence, const QByteArray &payload, qint64 arrivalMs);

    /// Takes the next 20 ms of the stream.
    Result pop(QByteArray *payload, QByteArray *fecSource);

    bool isPlaying() const { return m_playing; }
    int bufferedFrames() const { return static_cast<int>(m_packets.size()); }
    int targetFrames() const { return m_target; }
    double jitterMs() const { return m_jitterMs; }
    void reset();

private:
    qint64 unwrap(quint16 sequence) const;

    QMap<qint64, QByteArray> m_packets; ///< Keyed by unwrapped sequence number.
    bool m_playing = false;
    bool m_haveReference = false;
    qint64 m_next = 0; ///< Next unwrapped sequence to play (valid while playing).
    qint64 m_lastInserted = 0;
    int m_missingInRow = 0;
    int m_target = 3;
    double m_jitterMs = 0.0;
    qint64 m_lastArrivalMs = 0;
    qint64 m_lastArrivalSeq = 0;
};

} // namespace opentalk::audio
