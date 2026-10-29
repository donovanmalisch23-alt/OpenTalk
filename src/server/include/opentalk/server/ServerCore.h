// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/moderation/Appeals.h"
#include "opentalk/moderation/Audit.h"
#include "opentalk/moderation/Permissions.h"
#include "opentalk/moderation/Sanctions.h"
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

/// Server behaviour after the protocol handshake: login, channels, presence, text, who
/// hears whose voice, and moderation and administration. Every permission is checked
/// here, on the server, through moderation::PermissionResolver (roles, ranks and
/// per-channel rules; see docs/security/moderation-model.md).
///
/// It performs no I/O. The network layer reports sessions and their messages, and sends
/// what this object asks it to. That keeps all rules unit-testable without sockets.
class ServerCore : public QObject {
    Q_OBJECT

public:
    struct Config {
        bool allowGuests = false;
        QString serverName = QStringLiteral("OpenTalk server");
        int maxUsers = 200;
        quint32 maxVoiceBitrate = 128'000;
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
        /// Minimum length of passwords set through the administration requests.
        int minPasswordLength = 8;
        /// Audit entries older than this are removed at start-up (0 keeps everything).
        int auditRetentionDays = 365; ///< 0 keeps the audit log forever.
        /// Automatic backups of the database into this folder (empty: none).
        QString backupFolder;
        int backupIntervalHours = 24;
        int backupsToKeep = 7;
    };

    /// Live numbers only the network layer knows, for the statistics request.
    using StatsProvider = std::function<void(protocol::ServerStatsInfo &)>;

    /// `database` may be null (no accounts, no persistent channels, and moderation data
    /// kept in memory only). `nowMs` is a monotonic clock; `wallClockMs` gives UTC time
    /// for messages, sanctions and the audit log.
    ServerCore(Config config, ServerDatabase *database, PasswordWorker &passwords, std::function<qint64()> nowMs,
               std::function<qint64()> wallClockMs, QObject *parent = nullptr);
    ~ServerCore() override;

    /// Loads channels, roles, rules, sanctions, reports, appeals and settings, checks the
    /// audit chain and records the start. Call once before the first session.
    bool load(QString *errorMessage = nullptr);
    /// Kept for callers of the Phase 3 interface.
    bool loadChannels(QString *errorMessage = nullptr) { return load(errorMessage); }

    void sessionStarted(quint32 sessionId, const QHostAddress &address);
    void sessionEnded(quint32 sessionId);
    void handleMessage(quint32 sessionId, const protocol::Message &message);
    /// Call about once per second: lifts sanctions whose time is up.
    void tick();

    int onlineUserCount() const;
    QList<protocol::ChannelInfo> channels() const;
    /// Sessions that should hear `stream` from `sessionId` now: the other users in its
    /// channel, if it may talk there and is not muted. Empty otherwise.
    QList<quint32> voiceRecipients(quint32 sessionId,
                                   protocol::AudioStream stream = protocol::AudioStream::Voice) const;
    const protocol::ServerSettingsInfo &settings() const { return m_settings; }
    const moderation::PermissionResolver &permissions() const { return m_permissions; }
    moderation::AuditVerification auditStatus() const { return m_auditStatus; }
    void setStatsProvider(StatsProvider provider) { m_statsProvider = std::move(provider); }

signals:
    void sendTo(quint32 sessionId, const opentalk::protocol::Message &message);
    /// Send Error{code} (or Goodbye for kicks) to the session and close it.
    void closeSession(quint32 sessionId, opentalk::protocol::ErrorCode code, const QString &detail);
    /// The session logged in and has received its state snapshot.
    void userOnline(quint32 sessionId);
    /// Server name, guest access, user limit or voice bit rate changed.
    void settingsChanged(const opentalk::protocol::ServerSettingsInfo &settings);

private:
    enum class ClientState { AwaitingAuth, Authenticating, Online };

    struct Client {
        quint32 id = 0;
        QHostAddress address;
        ClientState state = ClientState::AwaitingAuth;
        protocol::UserInfo user;              ///< user.role is the account's role
        qint64 accountId = 0;                 ///< 0 for guests
        bool bot = false;                     ///< A bot account
        moderation::RoleId effectiveRole = 0; ///< Restricted while under a restriction
        QList<quint16> capabilities;          ///< Last sent to the client
        QList<quint16> channelCapabilities;   ///< Last sent to the client
        double textTokens = 0;
        qint64 textRefillMs = 0;
    };

    struct Channel {
        protocol::ChannelInfo info;
        QByteArray passwordHash;
        quint32 ownerId = 0; ///< Creator of a temporary channel.
        QSet<quint32> members;
    };

    // ServerCore.cpp: sessions, login, channels, text, voice
    void authenticate(Client &client, const protocol::Authenticate &request);
    void loginFailed(quint32 sessionId, const QHostAddress &address, const QString &detail);
    void goOnline(Client &client, const QString &name, protocol::Role role, qint64 accountId, bool bot = false);
    void joinChannel(Client &client, const protocol::JoinChannel &request);
    void createChannel(Client &client, const protocol::CreateChannel &request);
    void finishCreateChannel(quint32 clientId, const protocol::CreateChannel &request, const QByteArray &passwordHash);
    void deleteChannel(Client &client, const protocol::DeleteChannel &request);
    void editChannel(Client &client, const protocol::EditChannel &request);
    void finishEditChannel(quint32 clientId, const protocol::EditChannel &request, const QByteArray &passwordHash);
    void sendText(Client &client, const protocol::SendText &request);
    void setStatus(Client &client, const protocol::SetStatus &request);
    /// The per-user chat flood limit (shared by messages and status changes).
    bool takeTextToken(Client &client);
    void moveUser(Client &client, quint32 channelId);
    void removeIfEmptyTemporary(quint32 channelId);

    // ServerModeration.cpp: moderation and administration requests
    bool handleAdminMessage(Client &client, const protocol::Message &message);
    void moveOther(Client &actor, const protocol::MoveUser &request);
    void applySanction(Client &actor, const protocol::ApplySanction &request);
    void revokeSanction(Client &actor, const protocol::RevokeSanction &request);
    void list(Client &actor, const protocol::ListRequest &request);
    void setUserRole(Client &actor, const protocol::SetUserRole &request);
    void reportUser(Client &actor, const protocol::ReportUser &request);
    void resolveReport(Client &actor, const protocol::ResolveReport &request);
    void submitAppeal(Client &actor, const protocol::SubmitAppeal &request);
    void decideAppeal(Client &actor, const protocol::DecideAppeal &request);
    void readAudit(Client &actor, const protocol::ReadAudit &request);
    void createAccount(Client &actor, const protocol::CreateAccount &request);
    void deleteAccount(Client &actor, const protocol::DeleteAccount &request);
    void setAccountPassword(Client &actor, const protocol::SetAccountPassword &request);
    void saveRole(Client &actor, const protocol::SaveRole &request);
    void deleteRole(Client &actor, const protocol::DeleteRole &request);
    void addRule(Client &actor, const protocol::AddRule &request);
    void deleteRule(Client &actor, const protocol::DeleteRule &request);
    void serverSettings(Client &actor, const protocol::ServerSettingsRequest &request);
    void serverStats(Client &actor, const protocol::GetServerStats &request);
    void transferOwnership(Client &actor, const protocol::TransferOwnership &request);

    // Helpers
    moderation::Actor actorOf(const Client &client) const;
    QList<quint32> chainOf(quint32 channelId) const;
    bool can(const Client &client, moderation::Capability capability, quint32 channelId = 0) const;
    moderation::SanctionSubject subjectOf(const Client &client) const;
    /// Whether `actor` may act on someone with `targetRole` using `capability` in
    /// `channelId`: only on lower ranks, except that a channel moderator (an account rule
    /// granting the capability in that channel) may also act on equals there.
    bool mayActOn(const Client &actor, moderation::RoleId targetRole, moderation::Capability capability,
                  quint32 channelId) const;
    QList<Client *> clientsMatching(const moderation::Sanction &sanction);
    /// Recomputes a user's effective role, flags and capabilities and tells everyone
    /// who needs to know.
    void refreshUser(Client &client);
    void refreshAllUsers();
    void sendSanctionNotice(const moderation::Sanction &sanction);
    void enforceSanction(const moderation::Sanction &sanction);
    void storeSanction(moderation::Sanction &sanction);
    void updateSanction(const moderation::Sanction &sanction);
    protocol::SanctionInfo sanctionInfo(const moderation::Sanction &sanction, bool withAddress) const;
    void reloadPermissions();
    /// Removes audit entries older than the retention period (at start-up, then daily).
    void pruneAuditLog();
    /// Writes an automatic backup when the configured interval has passed.
    void backupIfDue();
    QList<protocol::RoleInfo> roleInfos() const;
    void audit(const Client *actor, const QString &action, const QString &target, quint32 channelId = 0,
               const QString &reason = {}, const QString &details = {});
    void notifyHolders(moderation::Capability capability, const QString &text, qint64 exceptAccountId = 0);
    bool someoneElseCanDecideAppeals(qint64 exceptAccountId) const;
    std::optional<qint64> accountIdOf(const QString &name) const;
    QString accountNameOf(qint64 accountId) const;
    moderation::RoleId roleOfAccount(qint64 accountId) const;
    QString describeBan(const moderation::Sanction &ban) const;
    void applySettings(const protocol::ServerSettingsInfo &settings, bool persist);

    void broadcast(const protocol::Message &message, quint32 exceptSessionId = 0);
    void reply(quint32 sessionId, quint64 requestId, std::optional<quint32> resultId = std::nullopt);
    void refuse(quint32 sessionId, quint64 requestId, protocol::ErrorCode code, const QString &detail);
    void refuseUnauthorized(const Client &client, quint64 requestId, moderation::Capability capability,
                            quint32 channelId = 0);
    bool nameTakenOnline(const QString &name) const;
    bool tooManyFailedLogins(const QHostAddress &address);
    Client *onlineClient(quint32 id);
    const Client *onlineClient(quint32 id) const;
    Client *onlineAccount(qint64 accountId);

    Config m_config;
    ServerDatabase *m_database;
    PasswordWorker &m_passwords;
    std::function<qint64()> m_now;
    std::function<qint64()> m_wallClock;
    QHash<quint32, Client> m_clients;
    QMap<quint32, Channel> m_channels;
    QHash<QHostAddress, QList<qint64>> m_failedLogins;
    quint64 m_failedLoginCount = 0; ///< Since start, for the statistics.
    quint32 m_nextTemporaryChannelId = 0x80000000u;
    /// A real Argon2id hash of a random password. Unknown usernames are checked against
    /// it, so a failed login takes as long whether or not the account exists.
    QByteArray m_dummyHash;

    moderation::PermissionResolver m_permissions;
    moderation::SanctionBook m_sanctions;
    QList<moderation::Report> m_reports;
    QList<moderation::Appeal> m_appeals;
    /// Without a database: the audit log and IDs live in memory only.
    QList<moderation::AuditEntry> m_memoryAudit;
    quint64 m_nextMemoryId = 1;
    moderation::AuditVerification m_auditStatus;
    qint64 m_lastAuditPruneMs = 0;
    qint64 m_lastBackupMs = -1; ///< Wall-clock time of the newest backup; -1 until looked up.
    protocol::ServerSettingsInfo m_settings;
    StatsProvider m_statsProvider;
    qint64 m_startedMs = 0;
};

} // namespace opentalk::server
