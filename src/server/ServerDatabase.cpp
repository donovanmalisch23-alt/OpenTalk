// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/server/ServerDatabase.h"

#include "opentalk/logging/Logging.h"

#include <QAtomicInteger>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace opentalk::server {
namespace {

QAtomicInteger<int> g_connectionCounter;

bool setError(QString *target, const QString &message)
{
    if (target != nullptr) {
        *target = message;
    }
    return false;
}

Account accountFrom(const QSqlQuery &q)
{
    Account a;
    a.id = q.value(0).toLongLong();
    a.username = q.value(1).toString();
    a.role = static_cast<protocol::Role>(q.value(2).toInt());
    a.passwordHash = q.value(3).toByteArray();
    a.disabled = q.value(4).toBool();
    return a;
}

constexpr const char *kAccountColumns = "id, username, role, password_hash, disabled";

} // namespace

ServerDatabase::ServerDatabase()
    : m_connectionName(QStringLiteral("opentalk-server-db-%1").arg(g_connectionCounter.fetchAndAddRelaxed(1)))
{
}

ServerDatabase::~ServerDatabase()
{
    close();
}

bool ServerDatabase::isOpen() const
{
    return QSqlDatabase::contains(m_connectionName) && QSqlDatabase::database(m_connectionName, false).isOpen();
}

void ServerDatabase::close()
{
    if (QSqlDatabase::contains(m_connectionName)) {
        {
            QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
            db.close();
        }
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

bool ServerDatabase::open(const QString &path, QString *errorMessage)
{
    close();
    m_path = path;
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        return setError(errorMessage, QStringLiteral("Qt's SQLite driver is not available"));
    }
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(path);
    if (!db.open()) {
        return setError(errorMessage, QStringLiteral("cannot open %1: %2").arg(path, db.lastError().text()));
    }
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    q.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    if (!q.exec(QStringLiteral("PRAGMA user_version")) || !q.next()) {
        return setError(errorMessage, QStringLiteral("%1 is not a valid database").arg(path));
    }
    const int version = q.value(0).toInt();
    q.finish();
    if (version > kSchemaVersion) {
        close();
        return setError(errorMessage, QStringLiteral("%1 was created by a newer OpenTalk (database version %2; this "
                                                     "version understands %3). It was not changed.")
                                          .arg(path)
                                          .arg(version)
                                          .arg(kSchemaVersion));
    }
    if (version < kSchemaVersion) {
        if (version > 0) {
            const QString backup = path + QStringLiteral(".v%1-backup").arg(version);
            QFile::remove(backup);
            if (!QFile::copy(path, backup)) {
                close();
                return setError(errorMessage, QStringLiteral("could not back up %1 before upgrading it").arg(path));
            }
        }
        if (!migrate(version, errorMessage)) {
            close();
            return false;
        }
    }
    return true;
}

bool ServerDatabase::migrate(int fromVersion, QString *errorMessage)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.transaction()) {
        return setError(errorMessage, db.lastError().text());
    }
    QSqlQuery q(db);
    auto run = [&q](const char *sql) { return q.exec(QString::fromLatin1(sql)); };
    bool ok = true;
    if (fromVersion < 1) {
        ok = run("CREATE TABLE accounts ("
                 " id INTEGER PRIMARY KEY,"
                 " username TEXT NOT NULL UNIQUE COLLATE NOCASE,"
                 " role INTEGER NOT NULL,"
                 " password_hash BLOB NOT NULL,"
                 " disabled INTEGER NOT NULL DEFAULT 0,"
                 " created_at TEXT NOT NULL)") &&
             run("CREATE TABLE channels ("
                 " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                 " parent_id INTEGER NOT NULL DEFAULT 0,"
                 " name TEXT NOT NULL,"
                 " topic TEXT NOT NULL DEFAULT '',"
                 " password_hash BLOB,"
                 " max_users INTEGER NOT NULL DEFAULT 0,"
                 " created_at TEXT NOT NULL)") &&
             // Channel IDs below 1000 are reserved (1 is the root), so stored channels start at 1000.
             run("INSERT INTO sqlite_sequence (name, seq) VALUES ('channels', 999)");
    }
    ok = ok && q.exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion));
    if (!ok) {
        const QString error = q.lastError().text();
        db.rollback();
        return setError(errorMessage, QStringLiteral("database upgrade failed: %1").arg(error));
    }
    if (!db.commit()) {
        return setError(errorMessage, db.lastError().text());
    }
    qCInfo(lcServer) << "Database schema upgraded from version" << fromVersion << "to" << kSchemaVersion;
    return true;
}

bool ServerDatabase::isValidUsername(const QString &username)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9._-]{1,32}$"));
    return pattern.match(username).hasMatch();
}

std::optional<Account> ServerDatabase::findAccount(const QString &username) const
{
    if (!isValidUsername(username)) {
        return std::nullopt;
    }
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("SELECT %1 FROM accounts WHERE username = ?").arg(QLatin1String(kAccountColumns)));
    q.addBindValue(username);
    if (!q.exec() || !q.next()) {
        return std::nullopt;
    }
    return accountFrom(q);
}

QList<Account> ServerDatabase::accounts() const
{
    QList<Account> result;
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    if (q.exec(QStringLiteral("SELECT %1 FROM accounts ORDER BY username COLLATE NOCASE")
                   .arg(QLatin1String(kAccountColumns)))) {
        while (q.next()) {
            result.append(accountFrom(q));
        }
    }
    return result;
}

bool ServerDatabase::createAccount(const QString &username, const QByteArray &passwordHash, protocol::Role role,
                                   QString *errorMessage)
{
    if (!isValidUsername(username)) {
        return setError(errorMessage, QStringLiteral("usernames are 1 to 32 characters: letters A-Z, digits, '.', '_' "
                                                     "or '-'"));
    }
    if (role == protocol::Role::Guest || passwordHash.isEmpty()) {
        return setError(errorMessage, QStringLiteral("accounts need a password and the role user or admin"));
    }
    if (findAccount(username)) {
        return setError(errorMessage, QStringLiteral("an account named %1 already exists").arg(username));
    }
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("INSERT INTO accounts (username, role, password_hash, created_at) VALUES (?, ?, ?, ?)"));
    q.addBindValue(username);
    q.addBindValue(static_cast<int>(role));
    q.addBindValue(passwordHash);
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!q.exec()) {
        return setError(errorMessage, q.lastError().text());
    }
    return true;
}

bool ServerDatabase::setPasswordHash(const QString &username, const QByteArray &passwordHash)
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("UPDATE accounts SET password_hash = ? WHERE username = ?"));
    q.addBindValue(passwordHash);
    q.addBindValue(username);
    return q.exec() && q.numRowsAffected() == 1;
}

bool ServerDatabase::setRole(const QString &username, protocol::Role role)
{
    if (role == protocol::Role::Guest) {
        return false;
    }
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("UPDATE accounts SET role = ? WHERE username = ?"));
    q.addBindValue(static_cast<int>(role));
    q.addBindValue(username);
    return q.exec() && q.numRowsAffected() == 1;
}

bool ServerDatabase::deleteAccount(const QString &username)
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("DELETE FROM accounts WHERE username = ?"));
    q.addBindValue(username);
    return q.exec() && q.numRowsAffected() == 1;
}

int ServerDatabase::adminCount() const
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM accounts WHERE role = ? AND disabled = 0"));
    q.addBindValue(static_cast<int>(protocol::Role::Admin));
    return (q.exec() && q.next()) ? q.value(0).toInt() : 0;
}

QList<StoredChannel> ServerDatabase::channels() const
{
    QList<StoredChannel> result;
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    if (q.exec(
            QStringLiteral("SELECT id, parent_id, name, topic, password_hash, max_users FROM channels ORDER BY id"))) {
        while (q.next()) {
            StoredChannel c;
            c.id = q.value(0).toUInt();
            c.parentId = q.value(1).toUInt();
            c.name = q.value(2).toString();
            c.topic = q.value(3).toString();
            c.passwordHash = q.value(4).toByteArray();
            c.maxUsers = static_cast<quint16>(q.value(5).toUInt());
            result.append(c);
        }
    }
    return result;
}

bool ServerDatabase::insertChannel(StoredChannel &channel, QString *errorMessage)
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("INSERT INTO channels (parent_id, name, topic, password_hash, max_users, created_at) "
                             "VALUES (?, ?, ?, ?, ?, ?)"));
    q.addBindValue(channel.parentId);
    // A null QString would be stored as SQL NULL; the columns require text.
    q.addBindValue(channel.name.isNull() ? QStringLiteral("") : channel.name);
    q.addBindValue(channel.topic.isNull() ? QStringLiteral("") : channel.topic);
    q.addBindValue(channel.passwordHash.isEmpty() ? QVariant(QMetaType(QMetaType::QByteArray))
                                                  : QVariant(channel.passwordHash));
    q.addBindValue(channel.maxUsers);
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!q.exec()) {
        return setError(errorMessage, q.lastError().text());
    }
    channel.id = q.lastInsertId().toUInt();
    return true;
}

bool ServerDatabase::deleteChannel(quint32 id)
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("DELETE FROM channels WHERE id = ?"));
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() == 1;
}

QString toString(protocol::Role role)
{
    switch (role) {
    case protocol::Role::Guest:
        return QStringLiteral("guest");
    case protocol::Role::User:
        return QStringLiteral("user");
    case protocol::Role::Admin:
        return QStringLiteral("admin");
    }
    return QStringLiteral("unknown");
}

std::optional<protocol::Role> roleFromString(const QString &text)
{
    if (text == QLatin1String("user")) {
        return protocol::Role::User;
    }
    if (text == QLatin1String("admin")) {
        return protocol::Role::Admin;
    }
    return std::nullopt;
}

} // namespace opentalk::server
