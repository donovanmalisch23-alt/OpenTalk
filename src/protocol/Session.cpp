// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/protocol/Session.h"

#include <algorithm>
#include <utility>

namespace opentalk::protocol {

// ---------------------------------------------------------------------------- Session

Session::Session(quint32 maxIncomingPayload, QObject *parent)
    : QObject(parent)
    , m_decoder(maxIncomingPayload)
{
}

void Session::setState(SessionState state)
{
    if (m_state != state) {
        m_state = state;
        emit stateChanged(state);
    }
}

void Session::send(const Message &message)
{
    if (m_state == SessionState::Closed) {
        return;
    }
    const QByteArray bytes = encodeMessage(message);
    if (bytes.isEmpty()) {
        // Only possible if this side built an oversized message: a bug, not peer input.
        qWarning("opentalk: dropped an outgoing message that exceeds the size limit");
        return;
    }
    emit sendRequested(bytes);
}

void Session::sendMessage(const Message &message)
{
    if (m_state == SessionState::Established) {
        send(message);
    }
}

void Session::closeWithError(ErrorCode code, const QString &detail)
{
    failProtocol(code, detail);
}

void Session::closeWith(const CloseInfo &info, const std::optional<Message> &finalMessage)
{
    if (m_state == SessionState::Closed) {
        return;
    }
    if (finalMessage) {
        send(*finalMessage);
    }
    setState(SessionState::Closed);
    emit closeRequested();
    emit closed(info);
}

void Session::failProtocol(ErrorCode code, const QString &detail)
{
    closeWith({CloseInfo::Initiator::Local, code, detail}, Error{code, std::nullopt, detail});
}

bool Session::admitFrame(qint64)
{
    return true;
}

void Session::receive(QByteArrayView bytes, qint64 nowMs)
{
    if (m_state == SessionState::Closed || m_state == SessionState::Idle) {
        return;
    }
    m_lastReceivedMs = nowMs;

    QList<Frame> frames;
    const DecodeError framingError = m_decoder.feed(bytes, frames);
    for (const Frame &frame : std::as_const(frames)) {
        if (m_state == SessionState::Closed || !admitFrame(nowMs)) {
            return;
        }
        MessageDecodeResult result = decodeMessage(frame.type, frame.payload);
        if (auto *failure = std::get_if<MessageDecodeFailure>(&result)) {
            if (failure->code == ErrorCode::UnknownMessage && m_state == SessionState::Established) {
                // Newer peers may send types we do not know; report but keep the session.
                send(Error{ErrorCode::UnknownMessage, std::nullopt, failure->detail});
                continue;
            }
            failProtocol(failure->code, failure->detail);
            return;
        }
        handleMessage(std::get<Message>(result), nowMs);
    }
    if (framingError != DecodeError::None && m_state != SessionState::Closed) {
        failProtocol(ErrorCode::ProtocolViolation, framingError == DecodeError::PayloadTooLarge
                                                       ? QStringLiteral("frame exceeds the size limit")
                                                       : QStringLiteral("reserved frame flags set"));
    }
}

void Session::tick(qint64 nowMs)
{
    if (m_state == SessionState::Handshaking || m_state == SessionState::Established) {
        handleTick(nowMs);
    }
}

void Session::transportClosed(const QString &detail)
{
    if (m_state == SessionState::Closed) {
        return;
    }
    setState(SessionState::Closed);
    emit closed({CloseInfo::Initiator::Transport, ErrorCode::None, detail});
}

// ---------------------------------------------------------------------- ServerSession

ServerSession::ServerSession(Config config, qint64 nowMs, QObject *parent)
    : Session(config.maxPayloadSize, parent)
    , m_config(std::move(config))
    , m_startedMs(nowMs)
    , m_tokens(m_config.messageBurst)
    , m_lastRefillMs(nowMs)
{
    setState(SessionState::Handshaking);
}

bool ServerSession::admitFrame(qint64 nowMs)
{
    const double elapsedSeconds = static_cast<double>(std::max<qint64>(0, nowMs - m_lastRefillMs)) / 1000.0;
    m_tokens = std::min(m_config.messageBurst, m_tokens + elapsedSeconds * m_config.messagesPerSecond);
    m_lastRefillMs = nowMs;
    if (m_tokens < 1.0) {
        failProtocol(ErrorCode::RateLimited, QStringLiteral("too many messages"));
        return false;
    }
    m_tokens -= 1.0;
    return true;
}

void ServerSession::handleMessage(const Message &message, qint64)
{
    if (state() == SessionState::Handshaking) {
        const auto *hello = std::get_if<Hello>(&message);
        if (hello == nullptr) {
            failProtocol(ErrorCode::ProtocolViolation, QStringLiteral("expected Hello"));
            return;
        }
        const std::optional<quint16> version = negotiateVersion(m_config.versions, hello->versions);
        if (!version) {
            failProtocol(ErrorCode::UnsupportedVersion, QStringLiteral("server supports protocol versions %1 to %2")
                                                            .arg(m_config.versions.minimum)
                                                            .arg(m_config.versions.maximum));
            return;
        }
        m_clientHello = *hello;
        m_version = *version;
        send(Welcome{*version, m_config.serverName, m_config.serverVersion, m_config.maxPayloadSize,
                     m_config.heartbeatIntervalSeconds, m_config.guestsAllowed});
        setState(SessionState::Established);
        emit established();
        return;
    }

    if (const auto *ping = std::get_if<Ping>(&message)) {
        send(Pong{ping->token});
    } else if (std::holds_alternative<Pong>(message) || std::holds_alternative<Error>(message)) {
        // Pongs only prove liveness (already recorded); client Errors are informational.
    } else if (const auto *goodbye = std::get_if<Goodbye>(&message)) {
        closeWith({CloseInfo::Initiator::Peer, goodbye->reason, QStringLiteral("client said goodbye")}, std::nullopt);
    } else if (isClientToServer(messageType(message)) && !std::holds_alternative<Hello>(message)) {
        emit applicationMessage(message);
    } else {
        failProtocol(ErrorCode::ProtocolViolation, QStringLiteral("unexpected message after handshake"));
    }
}

void ServerSession::handleTick(qint64 nowMs)
{
    if (state() == SessionState::Handshaking && nowMs - m_startedMs > m_config.handshakeTimeoutMs) {
        failProtocol(ErrorCode::Timeout, QStringLiteral("no Hello received in time"));
    } else if (state() == SessionState::Established && nowMs - lastReceivedMs() > m_config.idleTimeoutMs) {
        closeWith({CloseInfo::Initiator::Local, ErrorCode::Timeout, QStringLiteral("client stopped responding")},
                  Goodbye{ErrorCode::Timeout});
    }
}

void ServerSession::shutdown(ErrorCode reason)
{
    closeWith({CloseInfo::Initiator::Local, reason, QStringLiteral("server closed the session")}, Goodbye{reason});
}

// ---------------------------------------------------------------------- ClientSession

ClientSession::ClientSession(Config config, QObject *parent)
    : Session(kMaxServerPayloadSize, parent)
    , m_config(std::move(config))
{
}

void ClientSession::start(qint64 nowMs)
{
    if (state() != SessionState::Idle) {
        return;
    }
    m_startedMs = nowMs;
    setState(SessionState::Handshaking);
    send(Hello{m_config.versions, m_config.clientName, m_config.clientVersion});
}

void ClientSession::disconnectFromServer()
{
    if (state() == SessionState::Idle) {
        setState(SessionState::Closed);
        emit closed({CloseInfo::Initiator::Local, ErrorCode::None, QStringLiteral("disconnected")});
        return;
    }
    closeWith({CloseInfo::Initiator::Local, ErrorCode::None, QStringLiteral("disconnected")}, Goodbye{ErrorCode::None});
}

bool ClientSession::ping(qint64 nowMs)
{
    if (state() != SessionState::Established || m_outstandingPing) {
        return false;
    }
    sendPing(nowMs);
    return true;
}

void ClientSession::sendPing(qint64 nowMs)
{
    m_outstandingPing = m_nextPingToken++;
    m_lastPingSentMs = nowMs;
    send(Ping{*m_outstandingPing});
}

void ClientSession::handleMessage(const Message &message, qint64 nowMs)
{
    if (const auto *error = std::get_if<Error>(&message)) {
        if (state() == SessionState::Handshaking) {
            // An Error during the handshake (for example UnsupportedVersion) is fatal.
            closeWith({CloseInfo::Initiator::Peer, error->code, error->detail}, std::nullopt);
        } else {
            emit errorReceived(*error);
        }
        return;
    }
    if (const auto *goodbye = std::get_if<Goodbye>(&message)) {
        closeWith({CloseInfo::Initiator::Peer, goodbye->reason, QStringLiteral("server said goodbye")}, std::nullopt);
        return;
    }

    if (state() == SessionState::Handshaking) {
        const auto *welcome = std::get_if<Welcome>(&message);
        if (welcome == nullptr) {
            failProtocol(ErrorCode::ProtocolViolation, QStringLiteral("expected Welcome"));
            return;
        }
        // Reject a version we never offered: protects against downgrade attempts.
        if (welcome->version < m_config.versions.minimum || welcome->version > m_config.versions.maximum) {
            failProtocol(ErrorCode::UnsupportedVersion, QStringLiteral("server chose a version we did not offer"));
            return;
        }
        m_welcome = *welcome;
        m_lastPingSentMs = nowMs;
        setState(SessionState::Established);
        emit established(m_welcome);
        return;
    }

    if (const auto *ping = std::get_if<Ping>(&message)) {
        send(Pong{ping->token});
    } else if (const auto *pong = std::get_if<Pong>(&message)) {
        if (m_outstandingPing && pong->token == *m_outstandingPing) {
            m_outstandingPing.reset();
            m_lastRoundTripMs = nowMs - m_lastPingSentMs;
            emit roundTripMeasured(m_lastRoundTripMs);
        }
    } else if (isServerToClient(messageType(message)) && !std::holds_alternative<Welcome>(message)) {
        emit applicationMessage(message);
    } else {
        failProtocol(ErrorCode::ProtocolViolation, QStringLiteral("unexpected message after handshake"));
    }
}

void ClientSession::handleTick(qint64 nowMs)
{
    if (state() == SessionState::Handshaking) {
        if (nowMs - m_startedMs > m_config.handshakeTimeoutMs) {
            closeWith({CloseInfo::Initiator::Local, ErrorCode::Timeout, QStringLiteral("server did not answer Hello")},
                      std::nullopt);
        }
        return;
    }
    const qint64 intervalMs = static_cast<qint64>(m_welcome.heartbeatIntervalSeconds) * 1000;
    if (nowMs - lastReceivedMs() > intervalMs * m_config.missedHeartbeatsBeforeTimeout) {
        closeWith({CloseInfo::Initiator::Local, ErrorCode::Timeout, QStringLiteral("server stopped responding")},
                  std::nullopt);
        return;
    }
    if (nowMs - m_lastPingSentMs >= intervalMs) {
        sendPing(nowMs);
    }
}

QString toString(SessionState state)
{
    switch (state) {
    case SessionState::Idle:
        return QStringLiteral("idle");
    case SessionState::Handshaking:
        return QStringLiteral("handshaking");
    case SessionState::Established:
        return QStringLiteral("established");
    case SessionState::Closed:
        return QStringLiteral("closed");
    }
    return QStringLiteral("unknown");
}

} // namespace opentalk::protocol
