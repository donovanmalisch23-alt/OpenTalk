// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/server/Server.h"

#include "opentalk/logging/Logging.h"
#include "opentalk/security/Tls.h"
#include "opentalk/server/PasswordWorker.h"
#include "opentalk/server/ServerCore.h"

#include <QDateTime>
#include <QNetworkDatagram>
#include <QNetworkProxy>
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
    Peer(quint32 peerId, QSslSocket *peerSocket, QObject *parent)
        : QObject(parent)
        , id(peerId)
        , socket(peerSocket)
    {
        socket->setParent(this);
        handshakeTimer.setSingleShot(true);
    }

    quint32 id;
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
        if (m_core) {
            m_core->tick();
        }
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
        const QList<quint32> ids = m_peers.keys();
        for (quint32 id : ids) {
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
    m_passwords = std::make_unique<PasswordWorker>(m_config.passwordCost);
    ServerCore::Config coreConfig;
    coreConfig.allowGuests = m_config.allowGuests;
    coreConfig.serverName = m_config.session.serverName;
    coreConfig.maxUsers = m_config.maxSessions;
    coreConfig.maxVoiceBitrate = m_config.voice.maxBitrate;
    coreConfig.auditRetentionDays = m_config.auditRetentionDays;
    coreConfig.backupFolder = m_config.backupFolder;
    coreConfig.backupIntervalHours = m_config.backupIntervalHours;
    coreConfig.backupsToKeep = m_config.backupsToKeep;
    m_core = std::make_unique<ServerCore>(
        coreConfig, m_config.database, *m_passwords, [this] { return m_clock.elapsed(); },
        [] { return QDateTime::currentMSecsSinceEpoch(); });
    connect(m_core.get(), &ServerCore::settingsChanged, this, [this](const protocol::ServerSettingsInfo &settings) {
        // Applies to connections made from now on; people already online keep theirs.
        m_config.session.serverName = settings.name;
        m_config.session.guestsAllowed = settings.allowGuests;
        m_config.allowGuests = settings.allowGuests;
        m_config.maxSessions = settings.maxUsers;
        m_config.voice.maxBitrate = settings.maxVoiceBitrate;
        if (m_voice) {
            m_voice->setMaxBitrate(settings.maxVoiceBitrate);
        }
    });
    m_core->setStatsProvider([this](protocol::ServerStatsInfo &stats) {
        stats.connections = static_cast<quint32>(m_peers.size());
        stats.bytesIn = m_bytesIn;
        stats.bytesOut = m_bytesOut;
        stats.connectionErrors = m_connectionErrors;
        if (m_voice) {
            const VoiceRouter::Stats voice = m_voice->stats();
            stats.voicePacketsIn = voice.receivedUdp + voice.receivedTunnel;
            stats.voicePacketsOut = voice.sentUdp + voice.sentTunnel;
            stats.voicePacketsRejected = voice.rejected;
        }
    });
    QString coreError;
    if (!m_core->load(&coreError)) {
        if (errorMessage != nullptr) {
            *errorMessage = coreError;
        }
        m_core.reset();
        return false;
    }
    connect(m_core.get(), &ServerCore::sendTo, this, [this](quint32 id, const protocol::Message &message) {
        if (Peer *peer = m_peers.value(id); peer != nullptr && peer->session != nullptr) {
            peer->session->sendMessage(message);
        }
    });
    connect(m_core.get(), &ServerCore::closeSession, this, [this](quint32 id, ErrorCode code, const QString &detail) {
        if (Peer *peer = m_peers.value(id); peer != nullptr && peer->session != nullptr) {
            peer->session->closeWithError(code, detail);
        }
    });
    m_voice = std::make_unique<VoiceRouter>(
        m_config.voice,
        [this](quint32 id, protocol::AudioStream stream) { return m_core->voiceRecipients(id, stream); },
        [this] { return m_clock.elapsed(); });
    connect(m_core.get(), &ServerCore::userOnline, this, [this](quint32 id) {
        if (Peer *peer = m_peers.value(id); peer != nullptr && peer->session != nullptr) {
            peer->session->sendMessage(m_voice->addSession(id, udpPort()));
        }
    });
    connect(m_voice.get(), &VoiceRouter::sendDatagram, this,
            [this](const QByteArray &datagram, const QHostAddress &to, quint16 port) {
                if (m_udp) {
                    m_udp->writeDatagram(datagram, to, port);
                    m_bytesOut += static_cast<quint64>(datagram.size());
                }
            });
    connect(m_voice.get(), &VoiceRouter::sendTunnel, this, [this](quint32 id, const protocol::AudioData &audio) {
        Peer *peer = m_peers.value(id);
        if (peer != nullptr && peer->session != nullptr &&
            peer->socket->bytesToWrite() <= m_config.maxTunnelBacklogBytes) {
            peer->session->sendMessage(audio);
        }
    });

    m_clock.start();
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
    if (m_config.enableUdp) {
        m_udp = std::make_unique<QUdpSocket>();
        m_udp->setProxy(QNetworkProxy::NoProxy); // a server answers directly
        if (m_udp->bind(m_config.address, serverPort())) {
            connect(m_udp.get(), &QUdpSocket::readyRead, this, [this] {
                while (m_udp && m_udp->hasPendingDatagrams()) {
                    const QNetworkDatagram datagram = m_udp->receiveDatagram(2048);
                    m_bytesIn += static_cast<quint64>(datagram.data().size());
                    m_voice->handleDatagram(datagram.data(), datagram.senderAddress(),
                                            static_cast<quint16>(datagram.senderPort()));
                }
            });
        } else {
            qCWarning(lcServer).noquote() << "Voice will use TLS connections only; cannot use UDP port" << serverPort()
                                          << ":" << m_udp->errorString();
            m_udp.reset();
        }
    }
    m_stopping = false;
    m_tickTimer.start();
    qCInfo(lcServer).noquote() << "Listening on" << m_config.address.toString() << "port" << serverPort()
                               << (m_udp ? "(TCP and UDP)" : "(TCP only)");
    return true;
}

quint16 Server::udpPort() const
{
    return m_udp ? m_udp->localPort() : 0;
}

VoiceRouter::Stats Server::voiceStats() const
{
    return m_voice ? m_voice->stats() : VoiceRouter::Stats{};
}

bool Server::isListening() const
{
    return m_listener && m_listener->isListening();
}

quint16 Server::serverPort() const
{
    return m_listener ? m_listener->serverPort() : 0;
}

int Server::onlineUserCount() const
{
    return m_core ? m_core->onlineUserCount() : 0;
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
    // Send small messages (and tunnelled voice) at once instead of batching them.
    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
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

    const quint32 id = m_nextId++;
    auto *peer = new Peer(id, socket, this);
    peer->address = address;
    m_peers.insert(id, peer);

    connect(&peer->handshakeTimer, &QTimer::timeout, socket, [socket] { socket->abort(); });
    connect(socket, &QSslSocket::encrypted, this, [this, peer] {
        peer->handshakeTimer.stop();
        peer->session = new ServerSession(m_config.session, m_clock.elapsed(), peer);
        ServerSession *session = peer->session;
        QSslSocket *s = peer->socket;
        connect(session, &protocol::Session::sendRequested, s, [this, s](const QByteArray &bytes) {
            m_bytesOut += static_cast<quint64>(bytes.size());
            s->write(bytes);
        });
        connect(session, &protocol::Session::closeRequested, s, [s] { s->disconnectFromHost(); });
        connect(session, &ServerSession::established, this, [this, peer] {
            const protocol::Hello &hello = peer->session->clientHello();
            qCInfo(lcServer).noquote() << "Session" << peer->id << "established:" << hello.clientName
                                       << hello.clientVersion << "protocol" << peer->session->negotiatedVersion();
            m_core->sessionStarted(peer->id, peer->address);
            emit sessionEstablished(peer->id, hello);
        });
        connect(session, &protocol::Session::applicationMessage, this, [this, peer](const protocol::Message &message) {
            if (const auto *audio = std::get_if<protocol::AudioData>(&message)) {
                m_voice->handleTunnel(peer->id, *audio);
            } else {
                m_core->handleMessage(peer->id, message);
            }
        });
        connect(session, &protocol::Session::closed, this, [this, peer](const CloseInfo &info) {
            qCInfo(lcServer).noquote() << "Session" << peer->id << "closed:" << protocol::toString(info.code)
                                       << info.detail;
            switch (info.code) {
            case ErrorCode::ProtocolViolation:
            case ErrorCode::UnsupportedVersion:
            case ErrorCode::MalformedMessage:
            case ErrorCode::RateLimited:
            case ErrorCode::Timeout:
            case ErrorCode::InternalError:
                ++m_connectionErrors;
                break;
            default:
                break;
            }
            m_voice->removeSession(peer->id);
            m_core->sessionEnded(peer->id);
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
        m_bytesIn += static_cast<quint64>(bytes.size());
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

void Server::removePeer(quint32 id)
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
    m_udp.reset();
    qCInfo(lcServer) << "Stopped";
    // Always asynchronous, so callers can connect to stopped() after calling stop().
    QTimer::singleShot(0, this, &Server::stopped);
}

} // namespace opentalk::server
