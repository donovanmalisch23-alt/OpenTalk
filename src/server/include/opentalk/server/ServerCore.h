// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/protocol/Messages.h"

#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>

#include <functional>

namespace opentalk::server {

class PasswordWorker;
class ServerDatabase;

/// Server behaviour after the protocol handshake: login, channels, presence and text,
/// with every permission checked here, on the server.
///
/// It performs no I/O. The network layer reports sessions and their messages, and sends
/// what this object asks it to. That keeps all rules unit-testable without sockets.
///
/// Phase 3 permissions (fixed roles; configurable roles arrive in Phase 5):
///
/// | Action                             | Guest | User | Admin |
/// |------------------------------------|-------|------|-------|
/// | Join channels, chat, direct message | yes   | yes  | yes   |
/// | Create temporary channels          | no    | yes  | yes   |
/// | Create persistent channels         | no    | no   | yes   |
/// | Delete channels                    | no    | no   | yes   |
/// | Server-wide messages               | no    | no   | yes   |
/// | Bypass channel passwords and limits | no    | no   | yes   |
class ServerCore : public QObject {
    Q_OBJECT

public:
    struct Config {
        bool allowGuests = false;
        int maxChannels = 1000;
        int maxTemporaryChannelsPerUser = 3;
        quint16 maxUsersPerChannelLimit = 1000;
        /// Chat flood control per user: sustained messages per second and burst size.
        double textPerSecond = 1.0;
        double textBurst = 5.0;
        /// Failed logins from one address within the window before further attempts
        /// from it are refused without checking the password.
        int maxFailedLoginsPerAddress = 10;
        qint64 failedLoginWindowMs = 10 * 60 * 1000;
    };

    /// `database` may be null (no accounts, no persistent channels). `nowMs` is a
    /// monotonic clock; `wallClockMs` gives the time stamped on text messages.
    ServerCore(Config config, ServerDatabase *database, PasswordWorker &passwords, std::function<qint64()> nowMs,
               std::function<qint64()> wallClockMs, QObject *parent = nullptr);
    ~ServerCore() override;

    /// Loads persistent channels from the database. Call once before the first session.
    bool loadChannels(QString *errorMessage = nullptr);

    void sessionStarted(quint32 sessionId, const QHostAddress &address);
    void sessionEnded(quint32 sessionId);
    void handleMessage(quint32 sessionId, const protocol::Message &message);

    int onlineUserCount() const;
    QList<protocol::ChannelInfo> channels() const;

signals:
    void sendTo(quint32 sessionId, const opentalk::protocol::Message &message);
    /// Send Error{code} to the session and close it.
    void closeSession(quint32 sessionId, opentalk::protocol::ErrorCode code, const QString &detail);

private:
    enum class ClientState { AwaitingAuth, Authenticating, Online };

    struct Client {
        quint32 id = 0;
        QHostAddress address;
        ClientState state = ClientState::AwaitingAuth;
        protocol::UserInfo user;
        double textTokens = 0;
        qint64 textRefillMs = 0;
    };

    struct Channel {
        protocol::ChannelInfo info;
        QByteArray passwordHash;
        quint32 ownerId = 0; ///< Creator of a temporary channel.
        QSet<quint32> members;
    };

    void authenticate(Client &client, const protocol::Authenticate &request);
    void loginFailed(quint32 sessionId, const QHostAddress &address, const QString &detail);
    void goOnline(Client &client, const QString &name, protocol::Role role);
    void joinChannel(Client &client, const protocol::JoinChannel &request);
    void createChannel(Client &client, const protocol::CreateChannel &request);
    void finishCreateChannel(quint32 clientId, const protocol::CreateChannel &request, const QByteArray &passwordHash);
    void deleteChannel(Client &client, const protocol::DeleteChannel &request);
    void sendText(Client &client, const protocol::SendText &request);

    void moveUser(Client &client, quint32 channelId);
    void removeIfEmptyTemporary(quint32 channelId);
    void broadcast(const protocol::Message &message, quint32 exceptSessionId = 0);
    void reply(quint32 sessionId, quint64 requestId, std::optional<quint32> resultId = std::nullopt);
    void refuse(quint32 sessionId, quint64 requestId, protocol::ErrorCode code, const QString &detail);
    bool nameTakenOnline(const QString &name) const;
    bool tooManyFailedLogins(const QHostAddress &address);
    Client *onlineClient(quint32 id);

    Config m_config;
    ServerDatabase *m_database;
    PasswordWorker &m_passwords;
    std::function<qint64()> m_now;
    std::function<qint64()> m_wallClock;
    QHash<quint32, Client> m_clients;
    QMap<quint32, Channel> m_channels;
    QHash<QHostAddress, QList<qint64>> m_failedLogins;
    quint32 m_nextTemporaryChannelId = 0x80000000u;
    /// A real Argon2id hash of a random password. Unknown usernames are checked against
    /// it, so a failed login takes as long whether or not the account exists.
    QByteArray m_dummyHash;
};

} // namespace opentalk::server
