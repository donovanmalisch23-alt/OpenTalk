// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/protocol/Messages.h"
#include "opentalk/protocol/Session.h"
#include "opentalk/security/PasswordHash.h"
#include "opentalk/server/VoiceRouter.h"

#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QSslConfiguration>
#include <QTimer>
#include <QUdpSocket>

#include <memory>

namespace opentalk::server {

class PasswordWorker;
class ServerCore;
class ServerDatabase;

struct ServerConfig {
    QHostAddress address = QHostAddress::Any;
    quint16 port = 10350; ///< 0 picks a free port (used by tests).
    QSslConfiguration tls;
    protocol::ServerSession::Config session;
    /// Connections beyond this receive Goodbye{ServerFull} after the TLS handshake.
    int maxSessions = 200;
    /// Further connections from one IP address are dropped immediately.
    int maxConnectionsPerAddress = 10;
    int tlsHandshakeTimeoutMs = 10'000;
    /// How long stop() waits for clients to receive their goodbye.
    int shutdownGraceMs = 3'000;
    /// Let people join with a nickname and no account.
    bool allowGuests = false;
    /// Accounts and persistent channels. Not owned; must outlive the server. Without a
    /// database only guests can log in.
    ServerDatabase *database = nullptr;
    /// Argon2id cost for channel passwords (tests lower it to stay fast).
    security::PasswordHashCost passwordCost = security::PasswordHashCost::interactive();
    /// Offer encrypted UDP voice on the same port number as the TLS listener. Without
    /// it (or if the port cannot be bound) voice travels over the TLS connection.
    bool enableUdp = true;
    VoiceRouter::Config voice;
    /// Audio for a client whose connection has this much unsent data is dropped rather
    /// than queued, so one slow client cannot build up delay or memory.
    qint64 maxTunnelBacklogBytes = 256 * 1024;
    /// Audit log entries older than this are removed (0 keeps them forever).
    int auditRetentionDays = 365;
};

/// Accepts TLS connections and runs one ServerSession per client. Runs on the thread
/// that created it, entirely event-driven; it never needs a GUI.
class Server : public QObject {
    Q_OBJECT

public:
    explicit Server(ServerConfig config, QObject *parent = nullptr);
    ~Server() override;

    /// Starts listening. Returns false with a readable message on failure.
    bool start(QString *errorMessage = nullptr);
    /// Sends Goodbye{ServerShuttingDown} to every client, closes connections, then
    /// emits stopped().
    void stop();

    bool isListening() const;
    quint16 serverPort() const;
    /// Connections that completed the TLS handshake and have not closed yet.
    int sessionCount() const;
    /// Users who are logged in.
    int onlineUserCount() const;
    QString certificateFingerprint() const;
    /// The UDP voice port, or 0 if voice uses only the TLS connections.
    quint16 udpPort() const;
    VoiceRouter::Stats voiceStats() const;

signals:
    void sessionEstablished(quint32 sessionId, const opentalk::protocol::Hello &client);
    void sessionClosed(quint32 sessionId, const opentalk::protocol::CloseInfo &info);
    void stopped();

private:
    class Listener;
    class Peer;

    void acceptConnection(qintptr socketDescriptor);
    void removePeer(quint32 id);
    void startSession(Peer *peer);
    void finishStopIfDone();

    ServerConfig m_config;
    std::unique_ptr<Listener> m_listener;
    std::unique_ptr<PasswordWorker> m_passwords;
    std::unique_ptr<ServerCore> m_core;
    std::unique_ptr<VoiceRouter> m_voice;
    std::unique_ptr<QUdpSocket> m_udp;
    QHash<quint32, Peer *> m_peers;
    QHash<QHostAddress, int> m_connectionsPerAddress;
    quint32 m_nextId = 1;
    QTimer m_tickTimer;
    QTimer m_shutdownTimer;
    QElapsedTimer m_clock;
    bool m_stopping = false;
};

} // namespace opentalk::server

Q_DECLARE_METATYPE(opentalk::protocol::Hello)
