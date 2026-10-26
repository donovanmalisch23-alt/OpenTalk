// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/protocol/Frame.h"
#include "opentalk/protocol/Messages.h"
#include "opentalk/protocol/Version.h"

#include <QByteArray>
#include <QObject>
#include <QString>

namespace opentalk::protocol {

/// Session state machines for both ends of a control connection.
///
/// They contain no I/O. The owner feeds them received bytes and the current time from
/// a monotonic clock, and they ask for bytes to be sent and for the connection to be
/// closed through signals. This keeps every protocol rule testable without sockets or
/// real time, and lets the same logic run over TLS, an in-memory pipe, or later IPC.
///
/// Thread ownership: a session lives on one thread and is not internally synchronised.

enum class SessionState {
    Idle,        ///< Client only: not started yet.
    Handshaking, ///< Waiting for Hello (server) or Welcome (client).
    Established, ///< Handshake complete.
    Closed,      ///< Finished; no further input is processed.
};

/// Why a session ended.
struct CloseInfo {
    enum class Initiator {
        Local,     ///< This side decided to close (user request, protocol error, timeout).
        Peer,      ///< The other side sent Goodbye or a fatal Error.
        Transport, ///< The connection dropped without an orderly goodbye.
    };
    Initiator initiator = Initiator::Local;
    ErrorCode code = ErrorCode::None;
    QString detail;
};

class Session : public QObject {
    Q_OBJECT

public:
    SessionState state() const { return m_state; }

    /// Bytes arrived from the transport. `nowMs` comes from a monotonic clock.
    void receive(QByteArrayView bytes, qint64 nowMs);

    /// Must be called regularly (about once per second) to drive heartbeats and timeouts.
    void tick(qint64 nowMs);

    /// The transport closed or failed. Ends the session if it is not closed already.
    void transportClosed(const QString &detail);

signals:
    /// Write these bytes to the transport, in order.
    void sendRequested(const QByteArray &bytes);
    /// Flush pending writes and close the transport. Emitted once.
    void closeRequested();
    void stateChanged(opentalk::protocol::SessionState state);
    /// Emitted exactly once when the session ends.
    void closed(const opentalk::protocol::CloseInfo &info);

protected:
    Session(quint32 maxIncomingPayload, QObject *parent);

    void send(const Message &message);
    void setState(SessionState state);
    /// Sends `finalMessage` (if any), then ends the session.
    void closeWith(const CloseInfo &info, const std::optional<Message> &finalMessage);
    void failProtocol(ErrorCode code, const QString &detail);

    virtual void handleMessage(const Message &message, qint64 nowMs) = 0;
    virtual void handleTick(qint64 nowMs) = 0;
    /// Called for every complete frame before decoding. Return false to stop processing.
    virtual bool admitFrame(qint64 nowMs);

    qint64 lastReceivedMs() const { return m_lastReceivedMs; }

private:
    FrameDecoder m_decoder;
    SessionState m_state = SessionState::Idle;
    qint64 m_lastReceivedMs = 0;
};

/// Server side of one client connection.
class ServerSession : public Session {
    Q_OBJECT

public:
    struct Config {
        QString serverName = QStringLiteral("OpenTalk server");
        QString serverVersion = QStringLiteral("unspecified");
        VersionRange versions = kSupportedVersions;
        quint32 maxPayloadSize = kDefaultMaxPayloadSize;
        quint16 heartbeatIntervalSeconds = 15;
        qint64 handshakeTimeoutMs = 10'000;
        /// Silence longer than this ends the session. Clients ping every heartbeat interval.
        qint64 idleTimeoutMs = 45'000;
        /// Token bucket for incoming messages: sustained rate and burst size.
        double messagesPerSecond = 50.0;
        double messageBurst = 100.0;
    };

    ServerSession(Config config, qint64 nowMs, QObject *parent = nullptr);

    /// The Hello the client sent; valid once the session is established.
    const Hello &clientHello() const { return m_clientHello; }
    quint16 negotiatedVersion() const { return m_version; }

    /// Sends Goodbye with `reason` and closes the session.
    void shutdown(ErrorCode reason);

signals:
    void established();

protected:
    void handleMessage(const Message &message, qint64 nowMs) override;
    void handleTick(qint64 nowMs) override;
    bool admitFrame(qint64 nowMs) override;

private:
    Config m_config;
    qint64 m_startedMs;
    Hello m_clientHello;
    quint16 m_version = 0;
    double m_tokens;
    qint64 m_lastRefillMs;
};

/// Client side of a connection to a server.
class ClientSession : public Session {
    Q_OBJECT

public:
    struct Config {
        QString clientName = QStringLiteral("OpenTalk");
        QString clientVersion = QStringLiteral("unspecified");
        VersionRange versions = kSupportedVersions;
        qint64 handshakeTimeoutMs = 10'000;
        /// Silence longer than this many heartbeat intervals ends the session.
        int missedHeartbeatsBeforeTimeout = 3;
    };

    explicit ClientSession(Config config, QObject *parent = nullptr);

    /// Sends Hello. Call once, after the transport is connected and encrypted.
    void start(qint64 nowMs);

    /// Sends Goodbye and closes.
    void disconnectFromServer();

    /// Sends a Ping now (if none is outstanding) to measure the round-trip time.
    /// Returns false if the session is not established or a ping is already in flight.
    bool ping(qint64 nowMs);

    /// What the server announced; valid once the session is established.
    const Welcome &welcome() const { return m_welcome; }

    /// Most recent round-trip time in milliseconds, or -1 if none was measured yet.
    qint64 lastRoundTripMs() const { return m_lastRoundTripMs; }

signals:
    void established(const opentalk::protocol::Welcome &welcome);
    void roundTripMeasured(qint64 milliseconds);
    /// A non-fatal Error message from the server.
    void errorReceived(const opentalk::protocol::Error &error);

protected:
    void handleMessage(const Message &message, qint64 nowMs) override;
    void handleTick(qint64 nowMs) override;

private:
    void sendPing(qint64 nowMs);

    Config m_config;
    Welcome m_welcome;
    qint64 m_startedMs = 0;
    quint64 m_nextPingToken = 1;
    std::optional<quint64> m_outstandingPing;
    qint64 m_lastPingSentMs = 0;
    qint64 m_lastRoundTripMs = -1;
};

QString toString(SessionState state);

} // namespace opentalk::protocol

Q_DECLARE_METATYPE(opentalk::protocol::CloseInfo)
Q_DECLARE_METATYPE(opentalk::protocol::Welcome)
Q_DECLARE_METATYPE(opentalk::protocol::Error)
