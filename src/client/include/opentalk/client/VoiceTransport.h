// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/protocol/Messages.h"
#include "opentalk/security/PacketCipher.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>

namespace opentalk::client {

/// Carries voice between this client and the server: over encrypted UDP when it works,
/// otherwise over the TLS connection (the "tunnel"). It switches automatically in both
/// directions; nothing is lost but the latency advantage of UDP.
///
/// UDP is considered working while the server answers the pings sent every 5 seconds.
class VoiceTransport : public QObject {
    Q_OBJECT

public:
    static constexpr int kPingIntervalMs = 5'000;
    static constexpr int kProbeIntervalMs = 1'000; ///< Until the first answer arrives.
    static constexpr qint64 kUdpTimeoutMs = 12'000;

    explicit VoiceTransport(QObject *parent = nullptr);

    /// Starts with the server's setup. `serverAddress` is the address of the TLS
    /// connection; UDP is only exchanged with that address.
    void start(const protocol::AudioSetup &setup, const QHostAddress &serverAddress, bool allowUdp);
    void stop();

    bool isActive() const { return m_active; }
    /// True while voice to the server goes over UDP.
    bool isUsingUdp() const;
    quint32 maxBitrate() const { return m_maxBitrate; }

    /// Sends one packet (the userId field is ignored; the server fills it in).
    void send(const protocol::AudioData &audio);
    /// Audio that arrived over the TLS connection.
    void receiveTunnel(const protocol::AudioData &audio);

signals:
    /// Send this over the TLS connection.
    void tunnelSendRequested(const opentalk::protocol::AudioData &audio);
    void audioReceived(const opentalk::protocol::AudioData &audio);
    void pathChanged(bool usingUdp);

private:
    void sendPing();
    void readDatagrams();
    void updatePath();

    QUdpSocket m_socket;
    QTimer m_pingTimer;
    QElapsedTimer m_clock;
    security::PacketSealer m_toServer;
    security::PacketOpener m_fromServer;
    QHostAddress m_server;
    quint16 m_port = 0;
    quint32 m_maxBitrate = 0;
    quint64 m_nextPingToken = 1;
    qint64 m_lastPongMs = -1;
    bool m_active = false;
    bool m_udpEnabled = false;
    bool m_usingUdp = false;
};

} // namespace opentalk::client
