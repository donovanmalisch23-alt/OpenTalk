// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/client/Connection.h"

#include "opentalk/logging/Logging.h"
#include "opentalk/security/Tls.h"

#include <QRandomGenerator>
#include <QSslCipher>
#include <QSslSocket>

#include <algorithm>
#include <type_traits>

namespace opentalk::client {

using protocol::CloseInfo;
using protocol::ErrorCode;
using Reason = DisconnectInfo::Reason;

namespace {

constexpr int kMaxReconnectDelayMs = 30'000;

/// Connection problems worth retrying after an established connection was lost.
bool isRecoverable(const DisconnectInfo &info)
{
    switch (info.reason) {
    case Reason::NetworkError:
    case Reason::Timeout:
        return true;
    case Reason::ServerClosed:
        return info.protocolCode == ErrorCode::ServerShuttingDown || info.protocolCode == ErrorCode::Timeout ||
               info.protocolCode == ErrorCode::ServerFull || info.protocolCode == ErrorCode::InternalError;
    default:
        return false;
    }
}

} // namespace

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
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &Connection::startAttempt);
    connect(&m_serverState, &ClientState::textReceived, this, &Connection::textReceived);
    connect(&m_voice, &VoiceTransport::audioReceived, this, &Connection::audioReceived);
    connect(&m_voice, &VoiceTransport::pathChanged, this, &Connection::voicePathChanged);
    connect(&m_voice, &VoiceTransport::tunnelSendRequested, this, [this](const protocol::AudioData &audio) {
        if (m_session && m_state == ConnectionState::Connected) {
            m_session->sendMessage(audio);
        }
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

void Connection::teardown()
{
    m_voice.stop();
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
    m_reconnectTimer.stop();
    teardown();
    m_options = options;
    m_everConnected = false;
    m_reconnectAttempt = 0;
    m_finished = false;
    startAttempt();
}

void Connection::startAttempt()
{
    teardown();
    m_pendingFailure.reset();
    m_pendingRequests.clear();
    m_serverFingerprint.clear();
    m_welcome = {};
    m_loggedIn = false;
    m_serverState.clear();
    m_clock.start();

    m_socket = new QSslSocket(this);
    m_socket->setSslConfiguration(security::clientTlsConfiguration());
    m_session = new protocol::ClientSession(m_options.session, this);

    // Session -> socket.
    connect(m_session, &protocol::Session::sendRequested, m_socket,
            [socket = m_socket](const QByteArray &bytes) { socket->write(bytes); });
    connect(m_session, &protocol::Session::closeRequested, m_socket, [socket = m_socket] {
        socket->disconnectFromHost(); // flushes pending writes first
    });
    connect(m_session, &protocol::ClientSession::established, this, [this](const protocol::Welcome &welcome) {
        m_welcome = welcome;
        const Credentials &c = m_options.credentials;
        m_session->sendMessage(protocol::Authenticate{c.mode == Credentials::Mode::Guest
                                                          ? protocol::Authenticate::Mode::Guest
                                                          : protocol::Authenticate::Mode::Account,
                                                      c.name, c.password});
    });
    connect(m_session, &protocol::ClientSession::roundTripMeasured, this, &Connection::roundTripMeasured);
    connect(m_session, &protocol::ClientSession::errorReceived, this, &Connection::onServerError);
    connect(m_session, &protocol::Session::applicationMessage, this, &Connection::onApplicationMessage);
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

    m_finished = false;
    setState(ConnectionState::Connecting);
    m_connectTimer.start(m_options.connectTimeoutMs);
    qCInfo(lcClient) << "Connecting to port" << m_options.port << "attempt" << (m_reconnectAttempt + 1);
    qCDebug(lcClient).noquote() << "Host:" << m_options.host;
    m_socket->connectToHostEncrypted(m_options.host, m_options.port);
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
    // Send small messages (and tunnelled voice) at once instead of batching them.
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
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

void Connection::onApplicationMessage(const protocol::Message &message)
{
    if (const auto *auth = std::get_if<protocol::AuthResult>(&message)) {
        if (!m_loggedIn) {
            m_serverState.setSelf(*auth);
        }
        return;
    }
    if (const auto *done = std::get_if<protocol::RequestDone>(&message)) {
        if (m_pendingRequests.remove(done->requestId)) {
            emit requestFinished(done->requestId, std::nullopt, done->resultId);
        }
        return;
    }
    if (!m_loggedIn) {
        if (std::holds_alternative<protocol::StateSnapshot>(message) && m_serverState.selfId() != 0) {
            m_serverState.apply(message);
            m_loggedIn = true;
            m_everConnected = true;
            m_reconnectAttempt = 0;
            setState(ConnectionState::Connected);
            qCInfo(lcClient).noquote() << "Logged in to" << m_welcome.serverName << "as" << m_serverState.selfName();
            emit connected(m_welcome);
        }
        return; // nothing else is meaningful before the snapshot
    }
    if (const auto *audio = std::get_if<protocol::AudioData>(&message)) {
        m_voice.receiveTunnel(*audio);
        return;
    }
    // Data answers to requests.
    const std::optional<quint64> answered = std::visit(
        [](const auto &m) -> std::optional<quint64> {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, protocol::ListResult> || std::is_same_v<T, protocol::AuditPage> ||
                          std::is_same_v<T, protocol::ServerSettingsResult> ||
                          std::is_same_v<T, protocol::ServerStatsResult>) {
                return m.requestId;
            } else {
                return std::nullopt;
            }
        },
        message);
    if (answered) {
        if (m_pendingRequests.remove(*answered)) {
            emit responseReceived(*answered, message);
        }
        return;
    }
    if (const auto *setup = std::get_if<protocol::AudioSetup>(&message)) {
        m_voice.start(*setup, m_socket ? m_socket->peerAddress() : QHostAddress(), m_options.allowUdp);
        emit voiceReady();
        return;
    }
    m_serverState.apply(message);
}

bool Connection::sendAudio(protocol::AudioStream stream, quint16 sequence, const QByteArray &opus)
{
    if (m_state != ConnectionState::Connected || !m_voice.isActive() || opus.isEmpty() ||
        opus.size() > protocol::kMaxAudioPayloadBytes) {
        return false;
    }
    m_voice.send(protocol::AudioData{stream, sequence, 0, opus});
    return true;
}

void Connection::onServerError(const protocol::Error &error)
{
    if (error.requestId && m_pendingRequests.remove(*error.requestId)) {
        emit requestFinished(*error.requestId, error, std::nullopt);
        return;
    }
    if (!m_loggedIn && (error.code == ErrorCode::AuthenticationFailed || error.code == ErrorCode::NameInUse)) {
        // The server closes the connection right after this.
        m_pendingFailure = DisconnectInfo{Reason::LoginFailed, error.code, error.detail, {}};
        return;
    }
    qCInfo(lcClient).noquote() << "Server reported" << protocol::toString(error.code) << "-" << error.detail;
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

quint64 Connection::sendRequestWithId(protocol::Message message, quint64 requestId)
{
    if (m_state != ConnectionState::Connected || !m_session) {
        return 0;
    }
    m_pendingRequests.insert(requestId);
    m_session->sendMessage(message);
    return requestId;
}

quint64 Connection::joinChannel(quint32 channelId, const QString &password)
{
    const quint64 id = nextRequestId();
    return sendRequestWithId(protocol::JoinChannel{id, channelId, password}, id);
}

quint64 Connection::createChannel(const protocol::CreateChannel &request)
{
    protocol::CreateChannel copy = request;
    copy.requestId = nextRequestId();
    return sendRequestWithId(copy, copy.requestId);
}

quint64 Connection::deleteChannel(quint32 channelId)
{
    const quint64 id = nextRequestId();
    return sendRequestWithId(protocol::DeleteChannel{id, channelId}, id);
}

quint64 Connection::sendText(protocol::TextTarget target, quint32 targetId, const QString &text)
{
    if (!protocol::isValidChatText(text)) {
        return 0;
    }
    const quint64 id = nextRequestId();
    return sendRequestWithId(protocol::SendText{id, target, targetId, text}, id);
}

quint64 Connection::sendRequest(protocol::Message request)
{
    const quint64 id = nextRequestId();
    const bool isRequest = std::visit(
        [id](auto &m) {
            // A requires-expression, not an assignment.
            if constexpr (requires { m.requestId = quint64{0}; }) { // NOLINT(bugprone-assignment-in-if-condition)
                m.requestId = id;
                return true;
            } else {
                return false;
            }
        },
        request);
    return isRequest ? sendRequestWithId(std::move(request), id) : 0;
}

bool Connection::measureRoundTrip()
{
    return m_session && m_session->ping(m_clock.elapsed());
}

void Connection::disconnectFromServer()
{
    if (m_finished && m_state != ConnectionState::Reconnecting) {
        return;
    }
    const DisconnectInfo userRequested{Reason::UserRequested, ErrorCode::None, QStringLiteral("disconnected"), {}};
    if (m_state == ConnectionState::Reconnecting) {
        m_reconnectTimer.stop();
        m_everConnected = false;
        m_finished = false;
        finish(userRequested);
        return;
    }
    m_everConnected = false; // never reconnect after the user asked to disconnect
    if (m_session && m_session->state() == protocol::SessionState::Established) {
        m_session->disconnectFromServer(); // sends Goodbye; socket closes after flushing
    } else {
        m_pendingFailure = userRequested;
        if (m_socket) {
            m_socket->abort();
        }
        finish(userRequested);
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
    // Requests in flight will never be answered on this connection.
    const QSet<quint64> pending = std::exchange(m_pendingRequests, {});
    for (quint64 id : pending) {
        emit requestFinished(id, protocol::Error{ErrorCode::Timeout, id, QStringLiteral("connection lost")},
                             std::nullopt);
    }

    if (m_everConnected && m_options.autoReconnect && isRecoverable(info)) {
        ++m_reconnectAttempt;
        const int base = std::min(kMaxReconnectDelayMs, 1000 << std::min(m_reconnectAttempt - 1, 5));
        const int delay = base + static_cast<int>(QRandomGenerator::global()->bounded(500));
        setState(ConnectionState::Reconnecting);
        qCInfo(lcClient).noquote() << "Connection lost:" << info.detail << "- reconnecting in" << delay << "ms";
        m_reconnectTimer.start(delay);
        emit connectionLost(info, m_reconnectAttempt, delay);
        m_finished = false; // the connection as a whole continues
        return;
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
    case ConnectionState::Reconnecting:
        return QStringLiteral("reconnecting");
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
    case Reason::LoginFailed:
        return QStringLiteral("login failed");
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
