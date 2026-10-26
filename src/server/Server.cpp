// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/server/Server.h"

#include "opentalk/logging/Logging.h"
#include "opentalk/security/Tls.h"

#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>

namespace opentalk::server {

using protocol::CloseInfo;
using protocol::ErrorCode;
using protocol::ServerSession;

class Server::Listener : public QTcpServer {
public:
    explicit Listener(Server &server)
        : m_server(server)
    {
    }

protected:
    void incomingConnection(qintptr socketDescriptor) override { m_server.acceptConnection(socketDescriptor); }

private:
    Server &m_server;
};

/// One client connection: its socket, its TLS handshake timer and (once encrypted) its session.
class Server::Peer : public QObject {
public:
    Peer(quint64 peerId, QSslSocket *peerSocket, QObject *parent)
        : QObject(parent)
        , id(peerId)
        , socket(peerSocket)
    {
        socket->setParent(this);
        handshakeTimer.setSingleShot(true);
    }

    quint64 id;
    QSslSocket *socket;
    ServerSession *session = nullptr;
    QHostAddress address;
    QTimer handshakeTimer;
};

Server::Server(ServerConfig config, QObject *parent)
    : QObject(parent)
    , m_config(std::move(config))
{
    m_tickTimer.setInterval(1000);
    connect(&m_tickTimer, &QTimer::timeout, this, [this] {
        const qint64 now = m_clock.elapsed();
        const QList<Peer *> peers = m_peers.values();
        for (Peer *peer : peers) {
            if (peer->session != nullptr) {
                peer->session->tick(now);
            }
        }
    });
    m_shutdownTimer.setSingleShot(true);
    connect(&m_shutdownTimer, &QTimer::timeout, this, [this] {
        // Clients that did not close in time are cut off.
        const QList<quint64> ids = m_peers.keys();
        for (quint64 id : ids) {
            if (Peer *peer = m_peers.value(id)) {
                peer->socket->abort();
            }
            removePeer(id);
        }
    });
}

Server::~Server()
{
    for (Peer *peer : std::as_const(m_peers)) {
        peer->socket->disconnect(this);
        if (peer->session != nullptr) {
            peer->session->disconnect(this);
        }
    }
}

bool Server::start(QString *errorMessage)
{
    if (m_config.tls.localCertificate().isNull() || m_config.tls.privateKey().isNull()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("no TLS certificate and key are configured");
        }
        return false;
    }
    m_listener = std::make_unique<Listener>(*this);
    m_listener->setMaxPendingConnections(64);
    if (!m_listener->listen(m_config.address, m_config.port)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("cannot listen on %1 port %2: %3")
                                .arg(m_config.address.toString())
                                .arg(m_config.port)
                                .arg(m_listener->errorString());
        }
        m_listener.reset();
        return false;
    }
    m_stopping = false;
    m_clock.start();
    m_tickTimer.start();
    qCInfo(lcServer).noquote() << "Listening on" << m_config.address.toString() << "port" << serverPort();
    return true;
}

bool Server::isListening() const
{
    return m_listener && m_listener->isListening();
}

quint16 Server::serverPort() const
{
    return m_listener ? m_listener->serverPort() : 0;
}

int Server::sessionCount() const
{
    int count = 0;
    for (const Peer *peer : m_peers) {
        if (peer->session != nullptr && peer->session->state() != protocol::SessionState::Closed) {
            ++count;
        }
    }
    return count;
}

QString Server::certificateFingerprint() const
{
    return security::certificateFingerprint(m_config.tls.localCertificate());
}

void Server::acceptConnection(qintptr socketDescriptor)
{
    auto *socket = new QSslSocket;
    if (!socket->setSocketDescriptor(socketDescriptor) || m_stopping) {
        socket->abort();
        socket->deleteLater();
        return;
    }
    const QHostAddress address = socket->peerAddress();
    int &fromAddress = m_connectionsPerAddress[address];
    if (fromAddress >= m_config.maxConnectionsPerAddress) {
        qCWarning(lcServer) << "Dropping connection: too many connections from one address";
        qCDebug(lcServer).noquote() << "Address:" << address.toString();
        socket->abort();
        socket->deleteLater();
        if (fromAddress == 0) {
            m_connectionsPerAddress.remove(address);
        }
        return;
    }
    ++fromAddress;

    const quint64 id = m_nextId++;
    auto *peer = new Peer(id, socket, this);
    peer->address = address;
    m_peers.insert(id, peer);

    connect(&peer->handshakeTimer, &QTimer::timeout, socket, [socket] { socket->abort(); });
    connect(socket, &QSslSocket::encrypted, this, [this, peer] {
        peer->handshakeTimer.stop();
        peer->session = new ServerSession(m_config.session, m_clock.elapsed(), peer);
        ServerSession *session = peer->session;
        QSslSocket *s = peer->socket;
        connect(session, &protocol::Session::sendRequested, s, [s](const QByteArray &bytes) { s->write(bytes); });
        connect(session, &protocol::Session::closeRequested, s, [s] { s->disconnectFromHost(); });
        connect(session, &ServerSession::established, this, [this, peer] {
            const protocol::Hello &hello = peer->session->clientHello();
            qCInfo(lcServer).noquote() << "Session" << peer->id << "established:" << hello.clientName
                                       << hello.clientVersion << "protocol" << peer->session->negotiatedVersion();
            emit sessionEstablished(peer->id, hello);
        });
        connect(session, &protocol::Session::closed, this, [this, peer](const CloseInfo &info) {
            qCInfo(lcServer).noquote() << "Session" << peer->id << "closed:" << protocol::toString(info.code)
                                       << info.detail;
            emit sessionClosed(peer->id, info);
        });
        if (m_stopping) {
            session->shutdown(ErrorCode::ServerShuttingDown);
        } else if (sessionCount() > m_config.maxSessions) {
            session->shutdown(ErrorCode::ServerFull);
        }
    });
    connect(socket, &QSslSocket::readyRead, this, [this, peer] {
        const QByteArray bytes = peer->socket->readAll();
        if (peer->session != nullptr) {
            peer->session->receive(bytes, m_clock.elapsed());
        }
    });
    connect(socket, &QAbstractSocket::disconnected, this, [this, id] { removePeer(id); });
    connect(socket, &QAbstractSocket::errorOccurred, this, [this, id, socket](QAbstractSocket::SocketError error) {
        if (error != QAbstractSocket::RemoteHostClosedError) {
            qCDebug(lcServer).noquote() << "Connection" << id << "error:" << socket->errorString();
        }
        if (socket->state() == QAbstractSocket::UnconnectedState) {
            removePeer(id);
        }
    });

    socket->setSslConfiguration(m_config.tls);
    peer->handshakeTimer.start(m_config.tlsHandshakeTimeoutMs);
    socket->startServerEncryption();
}

void Server::removePeer(quint64 id)
{
    qCDebug(lcServer) << "Connection" << id << "removed";
    Peer *peer = m_peers.take(id);
    if (peer == nullptr) {
        return;
    }
    if (peer->session != nullptr) {
        peer->session->transportClosed(QStringLiteral("connection closed"));
    }
    peer->socket->disconnect(this);
    auto it = m_connectionsPerAddress.find(peer->address);
    if (it != m_connectionsPerAddress.end() && --(*it) <= 0) {
        m_connectionsPerAddress.erase(it);
    }
    peer->deleteLater();
    finishStopIfDone();
}

void Server::stop()
{
    if (m_stopping) {
        return;
    }
    m_stopping = true;
    qCInfo(lcServer) << "Stopping;" << m_peers.size() << "connection(s) open";
    if (m_listener) {
        m_listener->close();
    }
    const QList<Peer *> peers = m_peers.values();
    for (Peer *peer : peers) {
        if (peer->session != nullptr && peer->session->state() != protocol::SessionState::Closed) {
            peer->session->shutdown(ErrorCode::ServerShuttingDown);
        } else {
            peer->socket->abort();
        }
    }
    m_shutdownTimer.start(m_config.shutdownGraceMs);
    finishStopIfDone();
}

void Server::finishStopIfDone()
{
    if (!m_stopping || !m_peers.isEmpty() || !m_listener) {
        return;
    }
    m_shutdownTimer.stop();
    m_tickTimer.stop();
    m_listener.reset();
    qCInfo(lcServer) << "Stopped";
    // Always asynchronous, so callers can connect to stopped() after calling stop().
    QTimer::singleShot(0, this, &Server::stopped);
}

} // namespace opentalk::server
