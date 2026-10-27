// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/protocol/Messages.h"
#include "opentalk/protocol/Session.h"

#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QSslConfiguration>
#include <QTimer>

#include <memory>

namespace opentalk::server {

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
    /// Accounts and persistent channels. Not owned; must outlive the server.
    ServerDatabase *database = nullptr;
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
    QString certificateFingerprint() const;

signals:
    void sessionEstablished(quint64 sessionId, const opentalk::protocol::Hello &client);
    void sessionClosed(quint64 sessionId, const opentalk::protocol::CloseInfo &info);
    void stopped();

private:
    class Listener;
    class Peer;

    void acceptConnection(qintptr socketDescriptor);
    void removePeer(quint64 id);
    void finishStopIfDone();

    ServerConfig m_config;
    std::unique_ptr<Listener> m_listener;
    QHash<quint64, Peer *> m_peers;
    QHash<QHostAddress, int> m_connectionsPerAddress;
    quint64 m_nextId = 1;
    QTimer m_tickTimer;
    QTimer m_shutdownTimer;
    QElapsedTimer m_clock;
    bool m_stopping = false;
};

} // namespace opentalk::server

Q_DECLARE_METATYPE(opentalk::protocol::Hello)
