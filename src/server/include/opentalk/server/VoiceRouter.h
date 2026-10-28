// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/protocol/Session.h"
#include "opentalk/security/PacketCipher.h"

#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>

#include <functional>

namespace opentalk::server {

/// Forwards voice between the users of a channel without decoding it (the server never
/// needs the audio itself). Audio arrives as encrypted UDP datagrams or, when UDP is
/// blocked, as AudioData over the TLS connection, and leaves the same two ways.
///
/// Performs no I/O: the network layer feeds it datagrams and tunnel messages and sends
/// what it asks for, so all rules are unit-testable.
class VoiceRouter : public QObject {
    Q_OBJECT

public:
    struct Config {
        /// Advertised to clients, which cap their encoders to it.
        quint32 maxBitrate = 128'000;
        /// Per-session audio packet budget (three 20 ms streams need 150 per second).
        double packetsPerSecond = 200.0;
        double packetBurst = 400.0;
        /// A client's UDP address is used only while it keeps sending (it pings every 5 s).
        qint64 endpointTimeoutMs = 20'000;
    };

    struct Stats {
        quint64 receivedUdp = 0;
        quint64 receivedTunnel = 0;
        quint64 sentUdp = 0;
        quint64 sentTunnel = 0;
        quint64 rejected = 0; ///< Forged, replayed, malformed or over the rate limit.
    };

    /// `recipients(sessionId)` lists the sessions that should hear that user right now
    /// (decided by ServerCore: same channel, permissions). `nowMs` is a monotonic clock.
    VoiceRouter(Config config, std::function<QList<quint32>(quint32)> recipients, std::function<qint64()> nowMs,
                QObject *parent = nullptr);

    /// Creates keys for a logged-in session and returns the setup message to send it.
    /// `udpPort` 0 means UDP is unavailable and everything goes through the tunnel.
    protocol::AudioSetup addSession(quint32 sessionId, quint16 udpPort);
    void removeSession(quint32 sessionId);

    void handleDatagram(QByteArrayView datagram, const QHostAddress &from, quint16 fromPort);
    void handleTunnel(quint32 sessionId, const protocol::AudioData &audio);

    /// True while the server sends this session's voice over UDP.
    bool isUsingUdp(quint32 sessionId) const;
    const Stats &stats() const { return m_stats; }

signals:
    void sendDatagram(const QByteArray &datagram, const QHostAddress &to, quint16 toPort);
    void sendTunnel(quint32 sessionId, const opentalk::protocol::AudioData &audio);

private:
    struct Peer {
        quint32 sessionId = 0;
        quint32 udpId = 0;
        security::PacketOpener fromClient;
        security::PacketSealer toClient;
        QHostAddress address;
        quint16 port = 0;
        qint64 lastUdpMs = -1;
        bool clientReceivesUdp = false;
        double tokens = 0;
        qint64 refillMs = 0;
    };

    bool admit(Peer &peer);
    void forward(Peer &sender, protocol::AudioData audio);
    bool udpUsable(const Peer &peer) const;

    Config m_config;
    std::function<QList<quint32>(quint32)> m_recipients;
    std::function<qint64()> m_now;
    QHash<quint32, Peer> m_peers;      ///< By session ID.
    QHash<quint32, quint32> m_byUdpId; ///< UDP session ID -> session ID.
    Stats m_stats;
};

} // namespace opentalk::server
