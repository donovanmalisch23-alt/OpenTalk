// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/client/VoiceTransport.h"

#include "opentalk/logging/Logging.h"
#include "opentalk/protocol/VoicePacket.h"

#include <QNetworkDatagram>
#include <QNetworkProxy>

namespace opentalk::client {

using protocol::VoicePacketKind;
using security::PacketDirection;

VoiceTransport::VoiceTransport(QObject *parent)
    : QObject(parent)
{
    m_pingTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_pingTimer, &QTimer::timeout, this, &VoiceTransport::sendPing);
    connect(&m_socket, &QUdpSocket::readyRead, this, &VoiceTransport::readDatagrams);
    // Voice goes straight to the server; proxies cannot carry it usefully, and the TLS
    // fallback covers networks where UDP does not get through.
    m_socket.setProxy(QNetworkProxy::NoProxy);
}

void VoiceTransport::start(const protocol::AudioSetup &setup, const QHostAddress &serverAddress, bool allowUdp)
{
    stop();
    m_active = true;
    m_maxBitrate = setup.maxBitrate;
    m_clock.start();
    m_lastPongMs = -1;
    m_server = serverAddress;
    // A connection over IPv4 can be reported as an IPv4-mapped IPv6 address.
    bool isV4 = false;
    const quint32 v4 = serverAddress.toIPv4Address(&isV4);
    if (isV4) {
        m_server = QHostAddress(v4);
    }
    m_port = setup.udpPort;
    m_udpEnabled = allowUdp && setup.udpPort != 0 && !m_server.isNull();
    if (!m_udpEnabled) {
        qCInfo(lcClient) << "Voice uses the TLS connection" << (allowUdp ? "(server offers no UDP)" : "(UDP disabled)");
        return;
    }
    m_toServer = security::PacketSealer(setup.clientToServerKey, PacketDirection::ClientToServer, setup.udpSessionId);
    m_fromServer = security::PacketOpener(setup.serverToClientKey, PacketDirection::ServerToClient, setup.udpSessionId);
    const QHostAddress any = m_server.protocol() == QAbstractSocket::IPv6Protocol ? QHostAddress(QHostAddress::AnyIPv6)
                                                                                  : QHostAddress(QHostAddress::AnyIPv4);
    if (!m_socket.bind(any, 0)) {
        qCWarning(lcClient).noquote() << "Voice uses the TLS connection; cannot open a UDP socket:"
                                      << m_socket.errorString();
        m_udpEnabled = false;
        return;
    }
    sendPing();
    m_pingTimer.start(kProbeIntervalMs);
}

void VoiceTransport::stop()
{
    m_pingTimer.stop();
    m_socket.close();
    m_toServer = {};
    m_fromServer = {};
    m_active = false;
    m_udpEnabled = false;
    if (m_usingUdp) {
        m_usingUdp = false;
        emit pathChanged(false);
    }
}

bool VoiceTransport::isUsingUdp() const
{
    return m_usingUdp;
}

void VoiceTransport::updatePath()
{
    const bool usable = m_udpEnabled && m_lastPongMs >= 0 && m_clock.elapsed() - m_lastPongMs <= kUdpTimeoutMs;
    if (usable != m_usingUdp) {
        m_usingUdp = usable;
        qCInfo(lcClient) << "Voice path:" << (usable ? "UDP" : "TLS connection");
        emit pathChanged(usable);
    }
}

void VoiceTransport::sendPing()
{
    if (!m_udpEnabled) {
        return;
    }
    updatePath();
    protocol::VoicePing ping{m_nextPingToken++, m_usingUdp ? protocol::kVoicePingReceivingUdp : quint8{0}};
    m_socket.writeDatagram(m_toServer.seal(static_cast<quint8>(VoicePacketKind::Ping), protocol::encodeVoicePing(ping)),
                           m_server, m_port);
}

void VoiceTransport::send(const protocol::AudioData &audio)
{
    if (!m_active) {
        return;
    }
    updatePath();
    if (m_usingUdp) {
        protocol::AudioData copy = audio;
        copy.userId = 0;
        m_socket.writeDatagram(
            m_toServer.seal(static_cast<quint8>(VoicePacketKind::Audio), protocol::encodeVoiceAudio(copy)), m_server,
            m_port);
    } else {
        emit tunnelSendRequested(audio);
    }
}

void VoiceTransport::receiveTunnel(const protocol::AudioData &audio)
{
    if (m_active && audio.userId != 0) {
        emit audioReceived(audio);
    }
}

void VoiceTransport::readDatagrams()
{
    while (m_socket.hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_socket.receiveDatagram(2048);
        if (!m_udpEnabled || datagram.senderPort() != m_port) {
            continue;
        }
        bool isV4 = false;
        const quint32 v4 = datagram.senderAddress().toIPv4Address(&isV4);
        if ((isV4 ? QHostAddress(v4) : datagram.senderAddress()) != m_server) {
            continue;
        }
        security::PacketHeader header;
        const std::optional<QByteArray> plaintext = m_fromServer.open(datagram.data(), &header);
        if (!plaintext) {
            continue;
        }
        switch (static_cast<VoicePacketKind>(header.kind)) {
        case VoicePacketKind::Pong:
            if (protocol::decodeVoicePong(*plaintext)) {
                const bool first = m_lastPongMs < 0;
                m_lastPongMs = m_clock.elapsed();
                updatePath();
                if (first) {
                    m_pingTimer.start(kPingIntervalMs);
                    sendPing(); // tells the server at once that UDP works both ways
                }
            }
            break;
        case VoicePacketKind::Audio:
            if (std::optional<protocol::AudioData> audio = protocol::decodeVoiceAudio(*plaintext);
                audio && audio->userId != 0) {
                emit audioReceived(*audio);
            }
            break;
        case VoicePacketKind::Ping:
            break;
        }
    }
}

} // namespace opentalk::client
