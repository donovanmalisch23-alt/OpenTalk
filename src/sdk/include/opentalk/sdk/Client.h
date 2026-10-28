// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/sdk/Version.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

#include <memory>

namespace opentalk::sdk {

struct ServerAddress {
    QString host;
    quint16 port = 10350;
};

/// How to recognise the genuine server.
struct ServerIdentity {
    /// SHA-256 fingerprint the server must present (from the server administrator or a
    /// previous connection). Leave empty to require a certificate from a public authority.
    QString pinnedFingerprint;
};

/// How to log in. Guests need the server to allow them.
struct Login {
    enum class Mode { Guest, Account };
    Mode mode = Mode::Guest;
    QString name; ///< Nickname (guest) or username (account).
    QString password;
};

struct ServerInfo {
    QString name;
    QString softwareVersion;
    int protocolVersion = 0;
    QString certificateFingerprint;
};

enum class ClientState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting, ///< The connection was lost; the SDK is trying again.
};

/// Built-in roles; `Custom` covers roles a server defines itself (see User::roleId).
enum class Role { Guest, User, Admin, Owner, Moderator, Restricted, Custom };

struct Channel {
    quint32 id = 0;
    quint32 parentId = 0; ///< 0 for the root channel.
    QString name;         ///< Empty for the root channel.
    QString topic;
    bool persistent = false;
    bool hasPassword = false;
    int maxUsers = 0; ///< 0 = unlimited.
};

struct User {
    quint32 id = 0; ///< Valid while the user stays connected.
    QString name;
    Role role = Role::Guest;
    quint32 channelId = 0;
};

struct TextMessage {
    enum class Kind { Channel, Direct, ServerWide };
    Kind kind = Kind::Channel;
    quint32 fromUserId = 0;
    QString fromName;      ///< Empty if the sender already left.
    quint32 channelId = 0; ///< For Kind::Channel.
    QString text;
    QDateTime time; ///< Server time, UTC.
};

struct DisconnectReason {
    enum class Kind {
        UserRequested,
        ServerClosed,
        UntrustedCertificate, ///< certificateFingerprint says what the server presented.
        VersionMismatch,
        LoginFailed,
        ProtocolError,
        Timeout,
        NetworkError,
    };
    Kind kind = Kind::NetworkError;
    /// Plain-English explanation, suitable for logs and simple user interfaces.
    QString message;
    QString certificateFingerprint;
};

/// A connection to an OpenTalk server.
///
/// Use it from a thread with a running Qt event loop; all signals are delivered on the
/// thread that created the object. Call Client::initialize() once before the first
/// connection.
///
/// Draft API (SDK 0.x): connection and login, channels, users and text. Audio is added
/// when the server supports it.
class Client : public QObject {
    Q_OBJECT

public:
    explicit Client(QObject *parent = nullptr);
    ~Client() override;

    /// Sets up TLS. Returns false with a message if the system cannot provide TLS 1.3.
    static bool initialize(QString *errorMessage = nullptr);

    /// Name and version of your program, shown to server administrators.
    void setApplication(const QString &name, const QString &version);

    void connectToServer(const ServerAddress &address, const ServerIdentity &identity, const Login &login);
    /// Ends the connection; also stops automatic reconnection.
    void disconnectFromServer();

    ClientState state() const;
    /// Valid while connected.
    ServerInfo serverInfo() const;
    User self() const;
    QList<Channel> channels() const;
    QList<User> users() const;

    /// Requests return an ID (0 if not connected); the outcome arrives as
    /// requestFinished() with that ID.
    quint64 joinChannel(quint32 channelId, const QString &password = {});
    quint64 sendChannelMessage(const QString &text);
    quint64 sendDirectMessage(quint32 userId, const QString &text);

    /// Measures the round-trip time; the answer arrives as pingReplied().
    bool ping();

signals:
    void stateChanged(opentalk::sdk::ClientState state);
    /// Logged in. Also emitted after each automatic reconnection.
    void connected(const opentalk::sdk::ServerInfo &server);
    void disconnected(const opentalk::sdk::DisconnectReason &reason);
    void reconnecting(int attempt, int delayMs);
    void pingReplied(qint64 milliseconds);
    void requestFinished(quint64 requestId, bool succeeded, const QString &errorMessage);
    void channelsChanged();
    void userJoined(const opentalk::sdk::User &user);
    void userLeft(const opentalk::sdk::User &user);
    void userMoved(const opentalk::sdk::User &user);
    void textReceived(const opentalk::sdk::TextMessage &message);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace opentalk::sdk
