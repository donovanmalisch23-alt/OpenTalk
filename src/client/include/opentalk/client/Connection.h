// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/protocol/Messages.h"
#include "opentalk/protocol/Session.h"
#include "opentalk/security/ServerTrust.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

class QSslSocket;

namespace opentalk::client {

struct ConnectOptions {
    QString host;
    quint16 port = 10350;
    security::ServerTrust trust;
    protocol::ClientSession::Config session;
    /// Maximum time for the TCP connection and TLS handshake.
    int connectTimeoutMs = 10'000;
};

enum class ConnectionState {
    Disconnected,
    Connecting,  ///< TCP connection and TLS handshake in progress.
    Handshaking, ///< Encrypted; OpenTalk Hello/Welcome in progress.
    Connected,
};

/// Why a connection ended or could not be made.
struct DisconnectInfo {
    enum class Reason {
        UserRequested,
        ServerClosed,         ///< The server ended the session (see protocolCode).
        UntrustedCertificate, ///< See presentedFingerprint and detail.
        VersionMismatch,
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

/// One connection to an OpenTalk server. Create it on the GUI or worker thread that
/// will use it; all signals are emitted on that thread.
class Connection : public QObject {
    Q_OBJECT

public:
    explicit Connection(QObject *parent = nullptr);
    ~Connection() override;

    /// Starts connecting. Any existing connection is closed first, without a
    /// disconnected() signal for it.
    void connectToServer(const ConnectOptions &options);
    void disconnectFromServer();

    /// Sends a ping to measure the round-trip time; the result arrives through
    /// roundTripMeasured(). Returns false if not connected or a ping is in flight.
    bool measureRoundTrip();

    ConnectionState state() const { return m_state; }
    /// What the server announced; valid while connected.
    protocol::Welcome serverInfo() const;
    /// SHA-256 fingerprint of the server certificate of the current connection.
    QString serverFingerprint() const { return m_serverFingerprint; }

signals:
    void stateChanged(opentalk::client::ConnectionState state);
    void connected(const opentalk::protocol::Welcome &server);
    /// Emitted exactly once per connectToServer() call that did not end in another call.
    void disconnected(const opentalk::client::DisconnectInfo &info);
    void roundTripMeasured(qint64 milliseconds);

private:
    void setState(ConnectionState state);
    void teardown();
    void finish(const DisconnectInfo &info);
    void onSslErrors(const QList<QSslError> &errors);
    void onEncrypted();
    void onSessionClosed(const protocol::CloseInfo &info);

    ConnectionState m_state = ConnectionState::Disconnected;
    ConnectOptions m_options;
    QPointer<QSslSocket> m_socket;
    QPointer<protocol::ClientSession> m_session;
    QTimer m_tickTimer;
    QTimer m_connectTimer;
    QElapsedTimer m_clock;
    QString m_serverFingerprint;
    /// Failure recorded before the socket closes, so the right reason is reported.
    std::optional<DisconnectInfo> m_pendingFailure;
    bool m_finished = true;
};

QString toString(ConnectionState state);
QString toString(DisconnectInfo::Reason reason);

} // namespace opentalk::client

Q_DECLARE_METATYPE(opentalk::client::DisconnectInfo)
