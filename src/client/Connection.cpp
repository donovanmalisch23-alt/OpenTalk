// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/client/Connection.h"

#include "opentalk/logging/Logging.h"
#include "opentalk/security/Tls.h"

#include <QSslCipher>
#include <QSslSocket>

namespace opentalk::client {

using protocol::CloseInfo;
using protocol::ErrorCode;
using Reason = DisconnectInfo::Reason;

Connection::Connection(QObject *parent)
    : QObject(parent)
{
    m_tickTimer.setInterval(1000);
    connect(&m_tickTimer, &QTimer::timeout, this, [this] {
        if (m_session) {
            m_session->tick(m_clock.elapsed());
        }
    });
    m_connectTimer.setSingleShot(true);
    connect(&m_connectTimer, &QTimer::timeout, this, [this] {
        m_pendingFailure = DisconnectInfo{
            Reason::Timeout, ErrorCode::Timeout, QStringLiteral("the server did not respond in time"), {}};
        if (m_socket) {
            m_socket->abort();
        }
        finish(*m_pendingFailure);
    });
}

Connection::~Connection()
{
    teardown();
}

void Connection::setState(ConnectionState state)
{
    if (m_state != state) {
        m_state = state;
        emit stateChanged(state);
    }
}

protocol::Welcome Connection::serverInfo() const
{
    return m_session ? m_session->welcome() : protocol::Welcome{};
}

void Connection::teardown()
{
    m_tickTimer.stop();
    m_connectTimer.stop();
    if (m_session) {
        m_session->disconnect(this);
        m_session->deleteLater();
    }
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
    }
    m_session.clear();
    m_socket.clear();
}

void Connection::connectToServer(const ConnectOptions &options)
{
    teardown();
    m_finished = false;
    m_pendingFailure.reset();
    m_serverFingerprint.clear();
    m_options = options;
    m_clock.start();

    m_socket = new QSslSocket(this);
    m_socket->setSslConfiguration(security::clientTlsConfiguration());
    m_session = new protocol::ClientSession(options.session, this);

    // Session -> socket.
    connect(m_session, &protocol::Session::sendRequested, m_socket,
            [socket = m_socket](const QByteArray &bytes) { socket->write(bytes); });
    connect(m_session, &protocol::Session::closeRequested, m_socket, [socket = m_socket] {
        socket->disconnectFromHost(); // flushes pending writes first
    });
    connect(m_session, &protocol::ClientSession::established, this, [this](const protocol::Welcome &welcome) {
        setState(ConnectionState::Connected);
        qCInfo(lcClient).noquote() << "Connected to" << welcome.serverName << "(protocol" << welcome.version << ")";
        emit connected(welcome);
    });
    connect(m_session, &protocol::ClientSession::roundTripMeasured, this, &Connection::roundTripMeasured);
    connect(m_session, &protocol::Session::closed, this, &Connection::onSessionClosed);

    // Socket -> session.
    connect(m_socket, &QSslSocket::sslErrors, this, &Connection::onSslErrors);
    connect(m_socket, &QSslSocket::encrypted, this, &Connection::onEncrypted);
    connect(m_socket, &QSslSocket::readyRead, this, [this] {
        if (m_session && m_socket) {
            m_session->receive(m_socket->readAll(), m_clock.elapsed());
        }
    });
    connect(m_socket, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (!m_pendingFailure && m_socket && m_state != ConnectionState::Connected &&
            m_state != ConnectionState::Handshaking) {
            m_pendingFailure = DisconnectInfo{Reason::NetworkError, ErrorCode::None, m_socket->errorString(), {}};
        }
        // Errors before the TCP connection exists never produce disconnected().
        if (m_socket && m_socket->state() == QAbstractSocket::UnconnectedState) {
            finish(m_pendingFailure.value_or(
                DisconnectInfo{Reason::NetworkError, ErrorCode::None, m_socket->errorString(), {}}));
        }
    });
    connect(m_socket, &QAbstractSocket::disconnected, this, [this] {
        if (m_session) {
            m_session->transportClosed(QStringLiteral("connection closed"));
        }
        finish(m_pendingFailure.value_or(
            DisconnectInfo{Reason::NetworkError, ErrorCode::None, QStringLiteral("connection closed"), {}}));
    });

    setState(ConnectionState::Connecting);
    m_connectTimer.start(options.connectTimeoutMs);
    qCInfo(lcClient) << "Connecting to port" << options.port;
    qCDebug(lcClient).noquote() << "Host:" << options.host;
    m_socket->connectToHostEncrypted(options.host, options.port);
}

void Connection::onSslErrors(const QList<QSslError> &errors)
{
    const security::TrustEvaluation evaluation =
        security::evaluateServerCertificate(m_options.trust, m_socket->peerCertificate(), errors);
    m_serverFingerprint = evaluation.presentedFingerprint;
    if (evaluation.accepted) {
        m_socket->ignoreSslErrors(errors);
        return;
    }
    qCWarning(lcClient).noquote() << "Server certificate rejected:" << evaluation.reason;
    m_pendingFailure = DisconnectInfo{Reason::UntrustedCertificate, ErrorCode::None, evaluation.reason,
                                      evaluation.presentedFingerprint};
    // Not ignoring the errors makes Qt abort the handshake.
}

void Connection::onEncrypted()
{
    m_connectTimer.stop();
    // A certificate that passed normal validation never triggers sslErrors(), so the
    // pin must also be checked here.
    const security::TrustEvaluation evaluation =
        security::evaluateServerCertificate(m_options.trust, m_socket->peerCertificate(), {});
    m_serverFingerprint = evaluation.presentedFingerprint;
    if (!evaluation.accepted) {
        m_pendingFailure = DisconnectInfo{Reason::UntrustedCertificate, ErrorCode::None, evaluation.reason,
                                          evaluation.presentedFingerprint};
        m_socket->abort();
        finish(*m_pendingFailure);
        return;
    }
    qCInfo(lcClient).noquote() << "TLS established," << m_socket->sessionCipher().protocolString() << "server"
                               << m_serverFingerprint;
    setState(ConnectionState::Handshaking);
    m_tickTimer.start();
    m_session->start(m_clock.elapsed());
}

void Connection::onSessionClosed(const CloseInfo &info)
{
    if (m_pendingFailure) {
        return;
    }
    DisconnectInfo result{Reason::ProtocolError, info.code, info.detail, {}};
    switch (info.initiator) {
    case CloseInfo::Initiator::Local:
        if (info.code == ErrorCode::None) {
            result.reason = Reason::UserRequested;
        } else if (info.code == ErrorCode::Timeout) {
            result.reason = Reason::Timeout;
        } else if (info.code == ErrorCode::UnsupportedVersion) {
            result.reason = Reason::VersionMismatch;
        }
        break;
    case CloseInfo::Initiator::Peer:
        result.reason = info.code == ErrorCode::UnsupportedVersion ? Reason::VersionMismatch : Reason::ServerClosed;
        break;
    case CloseInfo::Initiator::Transport:
        result.reason = Reason::NetworkError;
        break;
    }
    m_pendingFailure = result;
}

bool Connection::measureRoundTrip()
{
    return m_session && m_session->ping(m_clock.elapsed());
}

void Connection::disconnectFromServer()
{
    if (m_finished) {
        return;
    }
    if (m_session && m_session->state() != protocol::SessionState::Idle) {
        m_session->disconnectFromServer(); // sends Goodbye; socket closes after flushing
    } else {
        m_pendingFailure = DisconnectInfo{Reason::UserRequested, ErrorCode::None, QStringLiteral("disconnected"), {}};
        finish(*m_pendingFailure);
    }
}

void Connection::finish(const DisconnectInfo &info)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    m_tickTimer.stop();
    m_connectTimer.stop();
    if (m_socket) {
        m_socket->disconnect(this);
        if (m_socket->state() != QAbstractSocket::UnconnectedState) {
            m_socket->disconnectFromHost();
        }
    }
    setState(ConnectionState::Disconnected);
    qCInfo(lcClient).noquote() << "Disconnected:" << toString(info.reason) << "-" << info.detail;
    emit disconnected(info);
}

QString toString(ConnectionState state)
{
    switch (state) {
    case ConnectionState::Disconnected:
        return QStringLiteral("disconnected");
    case ConnectionState::Connecting:
        return QStringLiteral("connecting");
    case ConnectionState::Handshaking:
        return QStringLiteral("handshaking");
    case ConnectionState::Connected:
        return QStringLiteral("connected");
    }
    return QStringLiteral("unknown");
}

QString toString(DisconnectInfo::Reason reason)
{
    switch (reason) {
    case Reason::UserRequested:
        return QStringLiteral("user requested");
    case Reason::ServerClosed:
        return QStringLiteral("server closed the connection");
    case Reason::UntrustedCertificate:
        return QStringLiteral("untrusted certificate");
    case Reason::VersionMismatch:
        return QStringLiteral("protocol version mismatch");
    case Reason::ProtocolError:
        return QStringLiteral("protocol error");
    case Reason::Timeout:
        return QStringLiteral("timeout");
    case Reason::NetworkError:
        return QStringLiteral("network error");
    }
    return QStringLiteral("unknown");
}

} // namespace opentalk::client
