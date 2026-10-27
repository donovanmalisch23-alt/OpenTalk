// SPDX-License-Identifier: GPL-3.0-only
#pragma once

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

/// The server's persistent data in one SQLite file: accounts and persistent channels.
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

    static constexpr int kSchemaVersion = 1;

    bool open(const QString &path, QString *errorMessage = nullptr);
    void close();
    bool isOpen() const;

    /// Usernames: 1-32 characters from A-Z, a-z, 0-9, '.', '_' and '-'. Restricting the
    /// alphabet avoids look-alike names that differ only in invisible or similar letters.
    static bool isValidUsername(const QString &username);

    std::optional<Account> findAccount(const QString &username) const;
    QList<Account> accounts() const;
    bool createAccount(const QString &username, const QByteArray &passwordHash, protocol::Role role,
                       QString *errorMessage = nullptr);
    bool setPasswordHash(const QString &username, const QByteArray &passwordHash);
    bool setRole(const QString &username, protocol::Role role);
    bool deleteAccount(const QString &username);
    int adminCount() const;

    QList<StoredChannel> channels() const;
    /// Stores a new channel and sets `channel.id`. Returns false on failure.
    bool insertChannel(StoredChannel &channel, QString *errorMessage = nullptr);
    bool deleteChannel(quint32 id);

private:
    bool migrate(int fromVersion, QString *errorMessage);

    QString m_connectionName;
    QString m_path;
};

QString toString(protocol::Role role);
std::optional<protocol::Role> roleFromString(const QString &text);

} // namespace opentalk::server
