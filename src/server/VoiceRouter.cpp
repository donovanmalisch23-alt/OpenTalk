// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/server/VoiceRouter.h"

#include "opentalk/logging/Logging.h"
#include "opentalk/protocol/VoicePacket.h"

#include <QRandomGenerator>

#include <algorithm>

namespace opentalk::server {

using protocol::AudioData;
using protocol::VoicePacketKind;
using security::PacketDirection;

VoiceRouter::VoiceRouter(Config config, std::function<QList<quint32>(quint32, protocol::AudioStream)> recipients,
                         std::function<qint64()> nowMs, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_recipients(std::move(recipients))
    , m_now(std::move(nowMs))
{
}

protocol::AudioSetup VoiceRouter::addSession(quint32 sessionId, quint16 udpPort)
{
    removeSession(sessionId);
    quint32 udpId = 0;
    while (udpId == 0 || m_byUdpId.contains(udpId)) {
        udpId = QRandomGenerator::system()->generate();
    }
    protocol::AudioSetup setup;
    setup.udpPort = udpPort;
    setup.udpSessionId = udpId;
    setup.clientToServerKey = security::randomPacketKey();
    setup.serverToClientKey = security::randomPacketKey();
    setup.maxBitrate = m_config.maxBitrate;

    Peer peer;
    peer.sessionId = sessionId;
    peer.udpId = udpId;
    peer.fromClient = security::PacketOpener(setup.clientToServerKey, PacketDirection::ClientToServer, udpId);
    peer.toClient = security::PacketSealer(setup.serverToClientKey, PacketDirection::ServerToClient, udpId);
    peer.tokens = m_config.packetBurst;
    peer.refillMs = m_now();
    m_peers.insert(sessionId, peer);
    m_byUdpId.insert(udpId, sessionId);
    return setup;
}

void VoiceRouter::removeSession(quint32 sessionId)
{
    const auto it = m_peers.find(sessionId);
    if (it != m_peers.end()) {
        m_byUdpId.remove(it->udpId);
        m_peers.erase(it);
    }
}

bool VoiceRouter::udpUsable(const Peer &peer) const
{
    return peer.clientReceivesUdp && peer.lastUdpMs >= 0 && m_now() - peer.lastUdpMs <= m_config.endpointTimeoutMs;
}

bool VoiceRouter::isUsingUdp(quint32 sessionId) const
{
    const auto it = m_peers.constFind(sessionId);
    return it != m_peers.constEnd() && udpUsable(*it);
}

bool VoiceRouter::admit(Peer &peer)
{
    const qint64 now = m_now();
    const double elapsed = static_cast<double>(std::max<qint64>(0, now - peer.refillMs)) / 1000.0;
    peer.tokens = std::min(m_config.packetBurst, peer.tokens + elapsed * m_config.packetsPerSecond);
    peer.refillMs = now;
    if (peer.tokens < 1.0) {
        return false;
    }
    peer.tokens -= 1.0;
    return true;
}

void VoiceRouter::handleDatagram(QByteArrayView datagram, const QHostAddress &from, quint16 fromPort)
{
    const std::optional<security::PacketHeader> header = security::peekPacketHeader(datagram);
    if (!header) {
        ++m_stats.rejected;
        return;
    }
    const auto id = m_byUdpId.constFind(header->sessionId);
    if (id == m_byUdpId.constEnd()) {
        ++m_stats.rejected;
        return;
    }
    Peer &peer = m_peers[*id];
    const std::optional<QByteArray> plaintext = peer.fromClient.open(datagram);
    if (!plaintext) {
        ++m_stats.rejected;
        return;
    }
    // Authentic: this is where the client is reachable now (its address can change,
    // for example when a router renews its mapping).
    peer.address = from;
    peer.port = fromPort;
    peer.lastUdpMs = m_now();

    switch (static_cast<VoicePacketKind>(header->kind)) {
    case VoicePacketKind::Ping: {
        const std::optional<protocol::VoicePing> ping = protocol::decodeVoicePing(*plaintext);
        if (!ping) {
            ++m_stats.rejected;
            return;
        }
        peer.clientReceivesUdp = (ping->flags & protocol::kVoicePingReceivingUdp) != 0;
        emit sendDatagram(
            peer.toClient.seal(static_cast<quint8>(VoicePacketKind::Pong), protocol::encodeVoicePong(ping->token)),
            from, fromPort);
        return;
    }
    case VoicePacketKind::Audio: {
        std::optional<AudioData> audio = protocol::decodeVoiceAudio(*plaintext);
        if (!audio || !admit(peer)) {
            ++m_stats.rejected;
            return;
        }
        ++m_stats.receivedUdp;
        forward(peer, std::move(*audio));
        return;
    }
    case VoicePacketKind::Pong:
        break;
    }
    ++m_stats.rejected;
}

void VoiceRouter::handleTunnel(quint32 sessionId, const AudioData &audio)
{
    const auto it = m_peers.find(sessionId);
    if (it == m_peers.end() || !admit(*it)) {
        ++m_stats.rejected;
        return;
    }
    // A client sends over the tunnel when UDP stopped working for it, so stop sending
    // to it over UDP too until its next ping says otherwise.
    it->clientReceivesUdp = false;
    ++m_stats.receivedTunnel;
    forward(*it, audio);
}

void VoiceRouter::forward(Peer &sender, AudioData audio)
{
    // The speaker is whoever owns the keys, never what the packet claims.
    audio.userId = sender.sessionId;
    const QList<quint32> recipients = m_recipients(sender.sessionId, audio.stream);
    if (recipients.isEmpty()) {
        return;
    }
    const QByteArray plaintext = protocol::encodeVoiceAudio(audio);
    for (quint32 recipientId : recipients) {
        const auto it = m_peers.find(recipientId);
        if (it == m_peers.end() || recipientId == sender.sessionId) {
            continue;
        }
        if (udpUsable(*it)) {
            ++m_stats.sentUdp;
            emit sendDatagram(it->toClient.seal(static_cast<quint8>(VoicePacketKind::Audio), plaintext), it->address,
                              it->port);
        } else {
            ++m_stats.sentTunnel;
            emit sendTunnel(recipientId, audio);
        }
    }
}

} // namespace opentalk::server
