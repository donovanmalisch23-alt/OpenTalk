// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/moderation/Appeals.h"
#include "opentalk/moderation/Audit.h"
#include "opentalk/moderation/Permissions.h"
#include "opentalk/moderation/Sanctions.h"
#include "opentalk/protocol/Messages.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

namespace opentalk::server {

struct Account {
    qint64 id = 0;
    QString username; ///< As typed when created; lookups ignore case.
    protocol::Role role = protocol::Role::User;
    QByteArray passwordHash;
    bool disabled = false;
    bool bot = false; ///< A program rather than a person; shown as such to everyone.
};

/// A channel that survives server restarts.
struct StoredChannel {
    quint32 id = 0;
    quint32 parentId = 0; ///< 0 for channels directly under the root.
    QString name;
    QString topic;
    QByteArray passwordHash; ///< Empty when the channel has no password.
    quint16 maxUsers = 0;    ///< 0 means unlimited.
};

/// The server's persistent data in one SQLite file: accounts, persistent channels,
/// roles and permission rules, sanctions, reports, appeals, the audit log and settings.
///
/// - The schema is versioned (PRAGMA user_version). Before an upgrade the file is copied
///   to "<file>.v<N>-backup", and the upgrade runs in a transaction.
/// - A database from a newer OpenTalk is refused rather than modified.
/// - Owned by one thread; all calls are quick local SQLite operations.
class ServerDatabase {
public:
    ServerDatabase();
    ~ServerDatabase();
    ServerDatabase(const ServerDatabase &) = delete;
    ServerDatabase &operator=(const ServerDatabase &) = delete;

    static constexpr int kSchemaVersion = 3;

    bool open(const QString &path, QString *errorMessage = nullptr);
    void close();
    bool isOpen() const;
    QString path() const { return m_path; }

    /// Writes a consistent copy of the whole database to `path` with SQLite's
    /// VACUUM INTO. It is safe while the server is running, even from another process.
    /// The copy is written next to `path` first and then renamed, so a half-written
    /// backup never has the final name. An existing file at `path` is replaced.
    bool backupTo(const QString &path, QString *errorMessage = nullptr) const;
    /// Checks that a file is a usable OpenTalk backup: SQLite, passes SQLite's integrity
    /// check, has OpenTalk's tables and comes from this or an older OpenTalk. Returns its
    /// schema version.
    static std::optional<int> checkBackup(const QString &path, QString *errorMessage = nullptr);

    /// Usernames: 1-32 characters from A-Z, a-z, 0-9, '.', '_' and '-'. Restricting the
    /// alphabet avoids look-alike names that differ only in invisible or similar letters.
    static bool isValidUsername(const QString &username);

    std::optional<Account> findAccount(const QString &username) const;
    QList<Account> accounts() const;
    bool createAccount(const QString &username, const QByteArray &passwordHash, protocol::Role role,
                       QString *errorMessage = nullptr);
    bool setPasswordHash(const QString &username, const QByteArray &passwordHash);
    bool setRole(const QString &username, protocol::Role role);
    /// Marks an account as a bot (or not).
    bool setBot(const QString &username, bool bot);
    bool deleteAccount(const QString &username);
    int adminCount() const;
    std::optional<Account> findAccountById(qint64 id) const;
    std::optional<Account> owner() const;
    /// Makes `username` the owner; the previous owner becomes an administrator.
    bool setOwner(const QString &username, QString *errorMessage = nullptr);

    QList<moderation::RoleDefinition> roles() const;
    /// Inserts (id 0: a new ID from 100 up is assigned) or replaces a role.
    bool saveRole(moderation::RoleDefinition &role, QString *errorMessage = nullptr);
    bool deleteRole(moderation::RoleId id);
    int accountsWithRole(moderation::RoleId id) const;

    QList<moderation::PermissionRule> rules() const;
    bool insertRule(moderation::PermissionRule &rule);
    bool deleteRule(quint64 id);
    void deleteRulesForChannel(quint32 channelId);

    QList<moderation::Sanction> sanctions() const;
    bool insertSanction(moderation::Sanction &sanction);
    bool updateSanction(const moderation::Sanction &sanction);

    QList<moderation::Report> reports() const;
    bool insertReport(moderation::Report &report);
    bool updateReport(const moderation::Report &report);

    QList<moderation::Appeal> appeals() const;
    bool insertAppeal(moderation::Appeal &appeal);
    bool updateAppeal(const moderation::Appeal &appeal);

    /// Appends an entry to the hash-chained audit log (sequence and hashes are filled in).
    bool appendAudit(moderation::AuditEntry &entry);
    /// Newest first, entries with sequence < `beforeSequence` (0 = from the newest).
    QList<moderation::AuditEntry> auditEntries(quint64 beforeSequence, int limit) const;
    quint64 auditCount() const;
    std::optional<moderation::AuditEntry> lastAuditEntry() const;
    /// Checks the whole chain, starting after the last pruned entry.
    moderation::AuditVerification verifyAudit() const;
    /// Removes entries older than `cutoffMs`, keeping the chain checkable from the first
    /// remaining entry. Returns the number removed.
    int pruneAudit(qint64 cutoffMs);

    QString setting(const QString &key, const QString &defaultValue = {}) const;
    bool setSetting(const QString &key, const QString &value);

    QList<StoredChannel> channels() const;
    /// Stores a new channel and sets `channel.id`. Returns false on failure.
    bool insertChannel(StoredChannel &channel, QString *errorMessage = nullptr);
    bool deleteChannel(quint32 id);
    /// Changes name, topic, password hash and limit of a stored channel.
    bool updateChannel(const StoredChannel &channel);

private:
    bool migrate(int fromVersion, QString *errorMessage);

    QString m_connectionName;
    QString m_path;
};

QString toString(protocol::Role role);
std::optional<protocol::Role> roleFromString(const QString &text);

} // namespace opentalk::server
