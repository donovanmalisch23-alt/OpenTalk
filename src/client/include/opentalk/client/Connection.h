// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/client/ClientState.h"
#include "opentalk/client/VoiceTransport.h"
#include "opentalk/protocol/Messages.h"
#include "opentalk/protocol/Session.h"
#include "opentalk/security/ServerTrust.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTimer>

class QSslSocket;

namespace opentalk::client {

struct Credentials {
    enum class Mode { Account, Guest };
    Mode mode = Mode::Guest;
    QString name; ///< Username, or nickname for guests.
    QString password;
};

struct ConnectOptions {
    QString host;
    quint16 port = 10350;
    security::ServerTrust trust;
    Credentials credentials;
    protocol::ClientSession::Config session;
    /// Maximum time for the TCP connection and TLS handshake.
    int connectTimeoutMs = 10'000;
    /// After losing an established connection, try again with growing delays
    /// (1, 2, 4 ... up to 30 seconds) until it works or the user disconnects.
    bool autoReconnect = true;
    /// Use UDP for voice when it works. Without it voice always uses the TLS connection.
    bool allowUdp = true;
};

enum class ConnectionState {
    Disconnected,
    Connecting,   ///< TCP connection and TLS handshake in progress.
    Handshaking,  ///< Encrypted; protocol handshake and login in progress.
    Connected,    ///< Logged in; state() is up to date.
    Reconnecting, ///< Connection lost; waiting before the next attempt.
};

/// Why a connection ended or could not be made.
struct DisconnectInfo {
    enum class Reason {
        UserRequested,
        ServerClosed,         ///< The server ended the session (see protocolCode).
        UntrustedCertificate, ///< See presentedFingerprint and detail.
        VersionMismatch,
        LoginFailed, ///< Wrong credentials, guests not allowed, or name in use (see protocolCode).
        ProtocolError,
        Timeout,
        NetworkError, ///< Host not found, refused, reset, TLS failure.
    };
    Reason reason = Reason::NetworkError;
    protocol::ErrorCode protocolCode = protocol::ErrorCode::None;
    /// Plain-English explanation for logs and error dialogs.
    QString detail;
    /// Set when the server's certificate was rejected, so the user can choose to trust it.
    QString presentedFingerprint;
};

/// One connection to an OpenTalk server, from TCP connect through login, with requests
/// and automatic reconnection. Create it on the thread that will use it; all signals are
/// emitted on that thread.
class Connection : public QObject {
    Q_OBJECT

public:
    explicit Connection(QObject *parent = nullptr);
    ~Connection() override;

    /// Starts connecting. Any existing connection is closed first, without a
    /// disconnected() signal for it.
    void connectToServer(const ConnectOptions &options);
    /// Closes the connection, or stops reconnecting.
    void disconnectFromServer();

    /// Requests. Each returns a request ID (0 if not connected); the result arrives
    /// through requestFinished() with the same ID.
    quint64 joinChannel(quint32 channelId, const QString &password = {});
    quint64 createChannel(const protocol::CreateChannel &request); ///< requestId is filled in.
    quint64 deleteChannel(quint32 channelId);
    quint64 sendText(protocol::TextTarget target, quint32 targetId, const QString &text);

    /// Sends a ping to measure the round-trip time; the result arrives through
    /// roundTripMeasured(). Returns false if not connected or a ping is in flight.
    bool measureRoundTrip();

    /// Sends one Opus packet to the users in this user's channel. Returns false if
    /// voice is not set up yet (not connected).
    bool sendAudio(protocol::AudioStream stream, quint16 sequence, const QByteArray &opus);
    /// True once the server has set up voice for this connection.
    bool isVoiceReady() const { return m_voice.isActive(); }
    bool isVoiceUsingUdp() const { return m_voice.isUsingUdp(); }
    /// Highest bit rate the server forwards (0 before voice is ready).
    quint32 voiceMaxBitrate() const { return m_voice.maxBitrate(); }

    ConnectionState state() const { return m_state; }
    const ClientState &serverState() const { return m_serverState; }
    /// What the server announced; valid while connected.
    protocol::Welcome serverInfo() const { return m_welcome; }
    /// SHA-256 fingerprint of the server certificate of the current connection.
    QString serverFingerprint() const { return m_serverFingerprint; }
    const ConnectOptions &options() const { return m_options; }

signals:
    void stateChanged(opentalk::client::ConnectionState state);
    /// Logged in and the server state is loaded. Also emitted after each reconnection.
    void connected(const opentalk::protocol::Welcome &server);
    /// The connection is over and no reconnection will be attempted.
    void disconnected(const opentalk::client::DisconnectInfo &info);
    /// An established connection was lost; another attempt follows after `delayMs`.
    void connectionLost(const opentalk::client::DisconnectInfo &info, int attempt, int delayMs);
    void requestFinished(quint64 requestId, const std::optional<opentalk::protocol::Error> &error,
                         std::optional<quint32> resultId);
    void roundTripMeasured(qint64 milliseconds);
    /// Voice is set up (after each login) and audio can be sent.
    void voiceReady();
    void voicePathChanged(bool usingUdp);
    /// One Opus packet from another user.
    void audioReceived(const opentalk::protocol::AudioData &audio);

    // Forwarded from serverState() for convenience.
    void textReceived(const opentalk::protocol::TextMessage &message);

private:
    void startAttempt();
    void setState(ConnectionState state);
    void teardown();
    void finish(const DisconnectInfo &info);
    void onSslErrors(const QList<QSslError> &errors);
    void onEncrypted();
    void onSessionClosed(const protocol::CloseInfo &info);
    void onApplicationMessage(const protocol::Message &message);
    void onServerError(const protocol::Error &error);
    quint64 sendRequest(protocol::Message message, quint64 requestId);
    quint64 nextRequestId() { return m_nextRequestId++; }

    ConnectionState m_state = ConnectionState::Disconnected;
    ConnectOptions m_options;
    ClientState m_serverState;
    protocol::Welcome m_welcome;
    QPointer<QSslSocket> m_socket;
    QPointer<protocol::ClientSession> m_session;
    QTimer m_tickTimer;
    QTimer m_connectTimer;
    QTimer m_reconnectTimer;
    QElapsedTimer m_clock;
    QString m_serverFingerprint;
    QSet<quint64> m_pendingRequests;
    VoiceTransport m_voice;
    quint64 m_nextRequestId = 1;
    bool m_loggedIn = false;
    bool m_everConnected = false; ///< Since the last user-initiated connect.
    int m_reconnectAttempt = 0;
    /// Failure recorded before the socket closes, so the right reason is reported.
    std::optional<DisconnectInfo> m_pendingFailure;
    bool m_finished = true;
};

QString toString(ConnectionState state);
QString toString(DisconnectInfo::Reason reason);

} // namespace opentalk::client

Q_DECLARE_METATYPE(opentalk::client::DisconnectInfo)
