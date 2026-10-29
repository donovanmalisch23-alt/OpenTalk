// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/server/ServerDatabase.h"

#include "opentalk/logging/Logging.h"

#include <QAtomicInteger>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>
#include <limits>

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

QString capabilityList(const QSet<moderation::Capability> &grants)
{
    QStringList names;
    for (moderation::Capability c : moderation::allCapabilities()) {
        if (grants.contains(c)) {
            names.append(moderation::capabilityName(c));
        }
    }
    // Never a null string: SQLite would store NULL, which the column does not allow.
    return names.isEmpty() ? QStringLiteral("") : names.join(QLatin1Char(','));
}

QSet<moderation::Capability> capabilitySet(const QString &text)
{
    QSet<moderation::Capability> result;
    for (const QString &name : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        // Names from a newer version are ignored rather than rejected.
        if (const auto c = moderation::capabilityFromName(name.trimmed())) {
            result.insert(*c);
        }
    }
    return result;
}

bool insertAuditRow(QSqlDatabase db, const moderation::AuditEntry &e)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO audit_log (sequence, time_ms, actor_account, actor_name, action, target, "
                             "channel_id, reason, details, previous_hash, hash) "
                             "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(static_cast<qint64>(e.sequence));
    q.addBindValue(e.timeMs);
    q.addBindValue(e.actorAccountId);
    q.addBindValue(e.actorName.isNull() ? QStringLiteral("") : e.actorName);
    q.addBindValue(e.action);
    q.addBindValue(e.target.isNull() ? QStringLiteral("") : e.target);
    q.addBindValue(e.channelId);
    q.addBindValue(e.reason.isNull() ? QStringLiteral("") : e.reason);
    q.addBindValue(e.details.isNull() ? QStringLiteral("") : e.details);
    q.addBindValue(e.previousHash.isNull() ? QByteArray("") : e.previousHash);
    q.addBindValue(e.hash);
    return q.exec();
}

QString text(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

Account accountFrom(const QSqlQuery &q)
{
    Account a;
    a.id = q.value(0).toLongLong();
    a.username = q.value(1).toString();
    a.role = static_cast<protocol::Role>(q.value(2).toInt());
    a.passwordHash = q.value(3).toByteArray();
    a.disabled = q.value(4).toBool();
    a.bot = q.value(5).toBool();
    return a;
}

constexpr const char *kAccountColumns = "id, username, role, password_hash, disabled, bot";

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

bool ServerDatabase::backupTo(const QString &path, QString *errorMessage) const
{
    if (!isOpen()) {
        return setError(errorMessage, QStringLiteral("the database is not open"));
    }
    const QString target = QFileInfo(path).absoluteFilePath();
    const QString partial = target + QStringLiteral(".partial");
    QFile::remove(partial);
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    QString quoted = partial;
    quoted.replace(QLatin1Char('\''), QStringLiteral("''"));
    if (!q.exec(QStringLiteral("VACUUM INTO '%1'").arg(quoted))) {
        QFile::remove(partial);
        return setError(errorMessage, QStringLiteral("cannot write %1: %2").arg(path, q.lastError().text()));
    }
    QFile::remove(target);
    if (!QFile::rename(partial, target)) {
        QFile::remove(partial);
        return setError(errorMessage, QStringLiteral("cannot create %1").arg(path));
    }
    return true;
}

std::optional<int> ServerDatabase::checkBackup(const QString &path, QString *errorMessage)
{
    const auto fail = [errorMessage](const QString &message) -> std::optional<int> {
        setError(errorMessage, message);
        return std::nullopt;
    };
    if (!QFileInfo(path).isFile()) {
        return fail(QStringLiteral("%1 does not exist").arg(path));
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.read(16) != QByteArray("SQLite format 3\0", 16)) {
        return fail(QStringLiteral("%1 is not an OpenTalk database").arg(path));
    }
    file.close();
    const QString name = QStringLiteral("opentalk-backup-check-%1").arg(g_connectionCounter.fetchAndAddRelaxed(1));
    std::optional<int> result;
    QString problem;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!db.open()) {
            problem = QStringLiteral("cannot open %1: %2").arg(path, db.lastError().text());
        } else {
            QSqlQuery q(db);
            if (!q.exec(QStringLiteral("PRAGMA integrity_check")) || !q.next() ||
                q.value(0).toString() != QLatin1String("ok")) {
                problem = QStringLiteral("%1 is damaged (SQLite's integrity check failed)").arg(path);
            } else if (!q.exec(QStringLiteral("SELECT count(*) FROM accounts")) || !q.next()) {
                problem = QStringLiteral("%1 is not an OpenTalk database").arg(path);
            } else if (!q.exec(QStringLiteral("PRAGMA user_version")) || !q.next()) {
                problem = QStringLiteral("cannot read the version of %1").arg(path);
            } else if (const int version = q.value(0).toInt(); version > kSchemaVersion) {
                problem = QStringLiteral("%1 comes from a newer OpenTalk (database version %2; this one knows %3)")
                              .arg(path)
                              .arg(version)
                              .arg(kSchemaVersion);
            } else {
                result = version;
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    if (!result) {
        return fail(problem);
    }
    return result;
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
    if (ok && fromVersion < 2) {
        ok = run("CREATE TABLE roles ("
                 " id INTEGER PRIMARY KEY,"
                 " name TEXT NOT NULL UNIQUE COLLATE NOCASE,"
                 " rank INTEGER NOT NULL,"
                 " capabilities TEXT NOT NULL,"
                 " built_in INTEGER NOT NULL DEFAULT 0)") &&
             run("CREATE TABLE permission_rules ("
                 " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                 " channel_id INTEGER NOT NULL,"
                 " subject_type INTEGER NOT NULL,"
                 " subject_id INTEGER NOT NULL,"
                 " capability INTEGER NOT NULL,"
                 " allow INTEGER NOT NULL)") &&
             run("CREATE TABLE sanctions ("
                 " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                 " type INTEGER NOT NULL, channel_id INTEGER NOT NULL,"
                 " target_account INTEGER NOT NULL, target_name TEXT NOT NULL, target_address TEXT NOT NULL,"
                 " target_role INTEGER NOT NULL,"
                 " actor_account INTEGER NOT NULL, actor_name TEXT NOT NULL, reason TEXT NOT NULL,"
                 " created_ms INTEGER NOT NULL, expires_ms INTEGER NOT NULL, state INTEGER NOT NULL,"
                 " resolved_by TEXT NOT NULL, resolution TEXT NOT NULL, resolved_ms INTEGER NOT NULL)") &&
             run("CREATE TABLE reports ("
                 " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                 " reporter_account INTEGER NOT NULL, reporter_name TEXT NOT NULL,"
                 " target_account INTEGER NOT NULL, target_name TEXT NOT NULL, target_address TEXT NOT NULL,"
                 " reason TEXT NOT NULL, created_ms INTEGER NOT NULL, state INTEGER NOT NULL,"
                 " handled_by TEXT NOT NULL, resolution TEXT NOT NULL, handled_ms INTEGER NOT NULL)") &&
             run("CREATE TABLE appeals ("
                 " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                 " sanction_id INTEGER NOT NULL UNIQUE,"
                 " appellant_account INTEGER NOT NULL, appellant_name TEXT NOT NULL, text TEXT NOT NULL,"
                 " created_ms INTEGER NOT NULL, state INTEGER NOT NULL,"
                 " decided_by TEXT NOT NULL, decision_reason TEXT NOT NULL, decided_ms INTEGER NOT NULL)") &&
             run("CREATE TABLE audit_log ("
                 " sequence INTEGER PRIMARY KEY,"
                 " time_ms INTEGER NOT NULL, actor_account INTEGER NOT NULL, actor_name TEXT NOT NULL,"
                 " action TEXT NOT NULL, target TEXT NOT NULL, channel_id INTEGER NOT NULL,"
                 " reason TEXT NOT NULL, details TEXT NOT NULL,"
                 " previous_hash BLOB NOT NULL, hash BLOB NOT NULL)") &&
             run("CREATE TABLE settings (key TEXT PRIMARY KEY, value TEXT NOT NULL)");
        for (const moderation::RoleDefinition &role : moderation::builtInRoles()) {
            QSqlQuery insert(db);
            insert.prepare(QStringLiteral("INSERT INTO roles (id, name, rank, capabilities, built_in) "
                                          "VALUES (?, ?, ?, ?, 1)"));
            insert.addBindValue(role.id);
            insert.addBindValue(role.name);
            insert.addBindValue(role.rank);
            insert.addBindValue(capabilityList(role.grants));
            ok = ok && insert.exec();
        }
        // Protocol 1 servers had administrators but no owner: the earliest administrator
        // becomes the owner.
        QString promoted;
        if (ok && fromVersion >= 1) {
            QSqlQuery first(db);
            if (first.exec(QStringLiteral("SELECT username FROM accounts WHERE role = 2 AND disabled = 0 "
                                          "ORDER BY created_at, id LIMIT 1")) &&
                first.next()) {
                promoted = first.value(0).toString();
                QSqlQuery update(db);
                update.prepare(QStringLiteral("UPDATE accounts SET role = 3 WHERE username = ?"));
                update.addBindValue(promoted);
                ok = update.exec();
            }
        }
        if (ok) {
            moderation::AuditEntry entry;
            entry.timeMs = QDateTime::currentMSecsSinceEpoch();
            entry.action = QStringLiteral("server.database-upgraded");
            entry.details = QStringLiteral("from=%1; to=%2").arg(fromVersion).arg(kSchemaVersion) +
                            (promoted.isEmpty() ? QString() : QStringLiteral("; owner=%1").arg(promoted));
            entry = moderation::chainAuditEntry(entry, std::nullopt);
            ok = insertAuditRow(db, entry);
        }
    }
    if (ok && fromVersion < 3) {
        ok = run("ALTER TABLE accounts ADD COLUMN bot INTEGER NOT NULL DEFAULT 0");
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
        return setError(errorMessage, QStringLiteral("accounts need a password and a role other than guest"));
    }
    if (findAccount(username)) {
        return setError(errorMessage, QStringLiteral("an account named %1 already exists").arg(username));
    }
    if (role == protocol::Role::Owner && owner()) {
        return setError(errorMessage, QStringLiteral("this server already has an owner"));
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

bool ServerDatabase::setBot(const QString &username, bool bot)
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("UPDATE accounts SET bot = ? WHERE username = ?"));
    q.addBindValue(bot ? 1 : 0);
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

bool ServerDatabase::updateChannel(const StoredChannel &channel)
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("UPDATE channels SET name = ?, topic = ?, password_hash = ?, max_users = ? WHERE id = ?"));
    q.addBindValue(text(channel.name));
    q.addBindValue(text(channel.topic));
    q.addBindValue(channel.passwordHash.isEmpty() ? QVariant(QMetaType(QMetaType::QByteArray))
                                                  : QVariant(channel.passwordHash));
    q.addBindValue(channel.maxUsers);
    q.addBindValue(channel.id);
    return q.exec() && q.numRowsAffected() == 1;
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
    case protocol::Role::Owner:
        return QStringLiteral("owner");
    case protocol::Role::Moderator:
        return QStringLiteral("moderator");
    case protocol::Role::Restricted:
        return QStringLiteral("restricted");
    }
    return QStringLiteral("role %1").arg(static_cast<quint32>(role));
}

std::optional<protocol::Role> roleFromString(const QString &text)
{
    for (protocol::Role role : {protocol::Role::User, protocol::Role::Admin, protocol::Role::Owner,
                                protocol::Role::Moderator, protocol::Role::Restricted}) {
        if (text == toString(role)) {
            return role;
        }
    }
    return std::nullopt;
}

// ------------------------------------------------------------- owner and roles

std::optional<Account> ServerDatabase::findAccountById(qint64 id) const
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("SELECT %1 FROM accounts WHERE id = ?").arg(QLatin1String(kAccountColumns)));
    q.addBindValue(id);
    if (!q.exec() || !q.next()) {
        return std::nullopt;
    }
    return accountFrom(q);
}

std::optional<Account> ServerDatabase::owner() const
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("SELECT %1 FROM accounts WHERE role = ? LIMIT 1").arg(QLatin1String(kAccountColumns)));
    q.addBindValue(static_cast<int>(protocol::Role::Owner));
    if (!q.exec() || !q.next()) {
        return std::nullopt;
    }
    return accountFrom(q);
}

bool ServerDatabase::setOwner(const QString &username, QString *errorMessage)
{
    const std::optional<Account> account = findAccount(username);
    if (!account) {
        return setError(errorMessage, QStringLiteral("there is no account named %1").arg(username));
    }
    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.transaction()) {
        return setError(errorMessage, db.lastError().text());
    }
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE accounts SET role = ? WHERE role = ?"));
    q.addBindValue(static_cast<int>(protocol::Role::Admin));
    q.addBindValue(static_cast<int>(protocol::Role::Owner));
    bool ok = q.exec();
    QSqlQuery promote(db);
    promote.prepare(QStringLiteral("UPDATE accounts SET role = ?, disabled = 0 WHERE id = ?"));
    promote.addBindValue(static_cast<int>(protocol::Role::Owner));
    promote.addBindValue(account->id);
    ok = ok && promote.exec();
    if (!ok) {
        const QString error = promote.lastError().text();
        db.rollback();
        return setError(errorMessage, error);
    }
    return db.commit() || setError(errorMessage, db.lastError().text());
}

QList<moderation::RoleDefinition> ServerDatabase::roles() const
{
    QList<moderation::RoleDefinition> result;
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    if (q.exec(QStringLiteral("SELECT id, name, rank, capabilities, built_in FROM roles ORDER BY rank DESC, id"))) {
        while (q.next()) {
            moderation::RoleDefinition role;
            role.id = q.value(0).toUInt();
            role.name = q.value(1).toString();
            role.rank = q.value(2).toInt();
            role.grants = capabilitySet(q.value(3).toString());
            role.builtIn = q.value(4).toBool();
            result.append(role);
        }
    }
    return result;
}

bool ServerDatabase::saveRole(moderation::RoleDefinition &role, QString *errorMessage)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (role.id == 0) {
        QSqlQuery next(db);
        if (!next.exec(QStringLiteral("SELECT MAX(id) FROM roles")) || !next.next()) {
            return setError(errorMessage, next.lastError().text());
        }
        role.id = std::max<moderation::RoleId>(moderation::roles::kFirstCustom, next.value(0).toUInt() + 1);
    }
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO roles (id, name, rank, capabilities, built_in) VALUES (?, ?, ?, ?, ?) "
                             "ON CONFLICT(id) DO UPDATE SET name = excluded.name, rank = excluded.rank, "
                             "capabilities = excluded.capabilities"));
    q.addBindValue(role.id);
    q.addBindValue(role.name);
    q.addBindValue(role.rank);
    q.addBindValue(capabilityList(role.grants));
    q.addBindValue(role.builtIn ? 1 : 0);
    if (!q.exec()) {
        return setError(errorMessage, q.lastError().text().contains(QLatin1String("UNIQUE"))
                                          ? QStringLiteral("a role with that name already exists")
                                          : q.lastError().text());
    }
    return true;
}

bool ServerDatabase::deleteRole(moderation::RoleId id)
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("DELETE FROM roles WHERE id = ? AND built_in = 0"));
    q.addBindValue(id);
    if (!q.exec() || q.numRowsAffected() != 1) {
        return false;
    }
    QSqlQuery rules(QSqlDatabase::database(m_connectionName, false));
    rules.prepare(QStringLiteral("DELETE FROM permission_rules WHERE subject_type = 1 AND subject_id = ?"));
    rules.addBindValue(id);
    rules.exec();
    return true;
}

int ServerDatabase::accountsWithRole(moderation::RoleId id) const
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM accounts WHERE role = ?"));
    q.addBindValue(id);
    return (q.exec() && q.next()) ? q.value(0).toInt() : 0;
}

// ------------------------------------------------------------------- rules

QList<moderation::PermissionRule> ServerDatabase::rules() const
{
    QList<moderation::PermissionRule> result;
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    if (q.exec(QStringLiteral("SELECT id, channel_id, subject_type, subject_id, capability, allow "
                              "FROM permission_rules ORDER BY id"))) {
        while (q.next()) {
            const auto capability = moderation::capabilityFromValue(static_cast<quint16>(q.value(4).toUInt()));
            if (!capability) {
                continue;
            }
            moderation::PermissionRule rule;
            rule.id = q.value(0).toULongLong();
            rule.channelId = q.value(1).toUInt();
            rule.subject = q.value(2).toInt() == 0 ? moderation::PermissionRule::Subject::Account
                                                   : moderation::PermissionRule::Subject::Role;
            rule.subjectId = q.value(3).toLongLong();
            rule.capability = *capability;
            rule.allow = q.value(5).toBool();
            result.append(rule);
        }
    }
    return result;
}

bool ServerDatabase::insertRule(moderation::PermissionRule &rule)
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("INSERT INTO permission_rules (channel_id, subject_type, subject_id, capability, allow) "
                             "VALUES (?, ?, ?, ?, ?)"));
    q.addBindValue(rule.channelId);
    q.addBindValue(static_cast<int>(rule.subject));
    q.addBindValue(rule.subjectId);
    q.addBindValue(static_cast<int>(rule.capability));
    q.addBindValue(rule.allow ? 1 : 0);
    if (!q.exec()) {
        return false;
    }
    rule.id = q.lastInsertId().toULongLong();
    return true;
}

bool ServerDatabase::deleteRule(quint64 id)
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("DELETE FROM permission_rules WHERE id = ?"));
    q.addBindValue(static_cast<qint64>(id));
    return q.exec() && q.numRowsAffected() == 1;
}

void ServerDatabase::deleteRulesForChannel(quint32 channelId)
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("DELETE FROM permission_rules WHERE channel_id = ?"));
    q.addBindValue(channelId);
    q.exec();
}

// --------------------------------------------------------------- sanctions

QList<moderation::Sanction> ServerDatabase::sanctions() const
{
    QList<moderation::Sanction> result;
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    if (q.exec(QStringLiteral("SELECT id, type, channel_id, target_account, target_name, target_address, target_role, "
                              "actor_account, actor_name, reason, created_ms, expires_ms, state, resolved_by, "
                              "resolution, resolved_ms FROM sanctions ORDER BY id"))) {
        while (q.next()) {
            moderation::Sanction s;
            s.id = q.value(0).toULongLong();
            s.type = static_cast<moderation::SanctionType>(q.value(1).toInt());
            s.channelId = q.value(2).toUInt();
            s.targetAccountId = q.value(3).toLongLong();
            s.targetName = q.value(4).toString();
            s.targetAddress = q.value(5).toString();
            s.targetRole = q.value(6).toUInt();
            s.actorAccountId = q.value(7).toLongLong();
            s.actorName = q.value(8).toString();
            s.reason = q.value(9).toString();
            s.createdMs = q.value(10).toLongLong();
            s.expiresMs = q.value(11).toLongLong();
            s.state = static_cast<moderation::SanctionState>(q.value(12).toInt());
            s.resolvedBy = q.value(13).toString();
            s.resolution = q.value(14).toString();
            s.resolvedMs = q.value(15).toLongLong();
            result.append(s);
        }
    }
    return result;
}

bool ServerDatabase::insertSanction(moderation::Sanction &s)
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(
        QStringLiteral("INSERT INTO sanctions (type, channel_id, target_account, target_name, target_address, "
                       "target_role, actor_account, actor_name, reason, created_ms, expires_ms, state, "
                       "resolved_by, resolution, resolved_ms) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(static_cast<int>(s.type));
    q.addBindValue(s.channelId);
    q.addBindValue(s.targetAccountId);
    q.addBindValue(text(s.targetName));
    q.addBindValue(text(s.targetAddress));
    q.addBindValue(s.targetRole);
    q.addBindValue(s.actorAccountId);
    q.addBindValue(text(s.actorName));
    q.addBindValue(text(s.reason));
    q.addBindValue(s.createdMs);
    q.addBindValue(s.expiresMs);
    q.addBindValue(static_cast<int>(s.state));
    q.addBindValue(text(s.resolvedBy));
    q.addBindValue(text(s.resolution));
    q.addBindValue(s.resolvedMs);
    if (!q.exec()) {
        return false;
    }
    s.id = q.lastInsertId().toULongLong();
    return true;
}

bool ServerDatabase::updateSanction(const moderation::Sanction &s)
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("UPDATE sanctions SET expires_ms = ?, state = ?, resolved_by = ?, resolution = ?, "
                             "resolved_ms = ? WHERE id = ?"));
    q.addBindValue(s.expiresMs);
    q.addBindValue(static_cast<int>(s.state));
    q.addBindValue(text(s.resolvedBy));
    q.addBindValue(text(s.resolution));
    q.addBindValue(s.resolvedMs);
    q.addBindValue(static_cast<qint64>(s.id));
    return q.exec() && q.numRowsAffected() == 1;
}

// ---------------------------------------------------------- reports, appeals

QList<moderation::Report> ServerDatabase::reports() const
{
    QList<moderation::Report> result;
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    if (q.exec(QStringLiteral("SELECT id, reporter_account, reporter_name, target_account, target_name, "
                              "target_address, reason, created_ms, state, handled_by, resolution, handled_ms "
                              "FROM reports ORDER BY id"))) {
        while (q.next()) {
            moderation::Report r;
            r.id = q.value(0).toULongLong();
            r.reporterAccountId = q.value(1).toLongLong();
            r.reporterName = q.value(2).toString();
            r.targetAccountId = q.value(3).toLongLong();
            r.targetName = q.value(4).toString();
            r.targetAddress = q.value(5).toString();
            r.reason = q.value(6).toString();
            r.createdMs = q.value(7).toLongLong();
            r.state = static_cast<moderation::Report::State>(q.value(8).toInt());
            r.handledBy = q.value(9).toString();
            r.resolution = q.value(10).toString();
            r.handledMs = q.value(11).toLongLong();
            result.append(r);
        }
    }
    return result;
}

bool ServerDatabase::insertReport(moderation::Report &r)
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("INSERT INTO reports (reporter_account, reporter_name, target_account, target_name, "
                             "target_address, reason, created_ms, state, handled_by, resolution, handled_ms) "
                             "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(r.reporterAccountId);
    q.addBindValue(text(r.reporterName));
    q.addBindValue(r.targetAccountId);
    q.addBindValue(text(r.targetName));
    q.addBindValue(text(r.targetAddress));
    q.addBindValue(text(r.reason));
    q.addBindValue(r.createdMs);
    q.addBindValue(static_cast<int>(r.state));
    q.addBindValue(text(r.handledBy));
    q.addBindValue(text(r.resolution));
    q.addBindValue(r.handledMs);
    if (!q.exec()) {
        return false;
    }
    r.id = q.lastInsertId().toULongLong();
    return true;
}

bool ServerDatabase::updateReport(const moderation::Report &r)
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("UPDATE reports SET state = ?, handled_by = ?, resolution = ?, handled_ms = ? "
                             "WHERE id = ?"));
    q.addBindValue(static_cast<int>(r.state));
    q.addBindValue(text(r.handledBy));
    q.addBindValue(text(r.resolution));
    q.addBindValue(r.handledMs);
    q.addBindValue(static_cast<qint64>(r.id));
    return q.exec() && q.numRowsAffected() == 1;
}

QList<moderation::Appeal> ServerDatabase::appeals() const
{
    QList<moderation::Appeal> result;
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    if (q.exec(QStringLiteral("SELECT id, sanction_id, appellant_account, appellant_name, text, created_ms, state, "
                              "decided_by, decision_reason, decided_ms FROM appeals ORDER BY id"))) {
        while (q.next()) {
            moderation::Appeal a;
            a.id = q.value(0).toULongLong();
            a.sanctionId = q.value(1).toULongLong();
            a.appellantAccountId = q.value(2).toLongLong();
            a.appellantName = q.value(3).toString();
            a.text = q.value(4).toString();
            a.createdMs = q.value(5).toLongLong();
            a.state = static_cast<moderation::Appeal::State>(q.value(6).toInt());
            a.decidedBy = q.value(7).toString();
            a.decisionReason = q.value(8).toString();
            a.decidedMs = q.value(9).toLongLong();
            result.append(a);
        }
    }
    return result;
}

bool ServerDatabase::insertAppeal(moderation::Appeal &a)
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("INSERT INTO appeals (sanction_id, appellant_account, appellant_name, text, created_ms, "
                             "state, decided_by, decision_reason, decided_ms) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(static_cast<qint64>(a.sanctionId));
    q.addBindValue(a.appellantAccountId);
    q.addBindValue(text(a.appellantName));
    q.addBindValue(text(a.text));
    q.addBindValue(a.createdMs);
    q.addBindValue(static_cast<int>(a.state));
    q.addBindValue(text(a.decidedBy));
    q.addBindValue(text(a.decisionReason));
    q.addBindValue(a.decidedMs);
    if (!q.exec()) {
        return false;
    }
    a.id = q.lastInsertId().toULongLong();
    return true;
}

bool ServerDatabase::updateAppeal(const moderation::Appeal &a)
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("UPDATE appeals SET state = ?, decided_by = ?, decision_reason = ?, decided_ms = ? "
                             "WHERE id = ?"));
    q.addBindValue(static_cast<int>(a.state));
    q.addBindValue(text(a.decidedBy));
    q.addBindValue(text(a.decisionReason));
    q.addBindValue(a.decidedMs);
    q.addBindValue(static_cast<qint64>(a.id));
    return q.exec() && q.numRowsAffected() == 1;
}

// -------------------------------------------------------------------- audit

namespace {

moderation::AuditEntry auditFrom(const QSqlQuery &q)
{
    moderation::AuditEntry e;
    e.sequence = q.value(0).toULongLong();
    e.timeMs = q.value(1).toLongLong();
    e.actorAccountId = q.value(2).toLongLong();
    e.actorName = q.value(3).toString();
    e.action = q.value(4).toString();
    e.target = q.value(5).toString();
    e.channelId = q.value(6).toUInt();
    e.reason = q.value(7).toString();
    e.details = q.value(8).toString();
    e.previousHash = q.value(9).toByteArray();
    e.hash = q.value(10).toByteArray();
    return e;
}

constexpr const char *kAuditColumns =
    "sequence, time_ms, actor_account, actor_name, action, target, channel_id, reason, details, previous_hash, hash";

} // namespace

bool ServerDatabase::appendAudit(moderation::AuditEntry &entry)
{
    entry = moderation::chainAuditEntry(entry, lastAuditEntry());
    return insertAuditRow(QSqlDatabase::database(m_connectionName, false), entry);
}

std::optional<moderation::AuditEntry> ServerDatabase::lastAuditEntry() const
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    if (!q.exec(QStringLiteral("SELECT %1 FROM audit_log ORDER BY sequence DESC LIMIT 1")
                    .arg(QLatin1String(kAuditColumns))) ||
        !q.next()) {
        // Everything may have been pruned: continue the chain from the recorded anchor.
        const QString sequence = setting(QStringLiteral("audit.anchorSequence"));
        if (sequence.isEmpty()) {
            return std::nullopt;
        }
        moderation::AuditEntry anchor;
        anchor.sequence = sequence.toULongLong();
        anchor.hash = QByteArray::fromHex(setting(QStringLiteral("audit.anchorHash")).toLatin1());
        return anchor;
    }
    return auditFrom(q);
}

QList<moderation::AuditEntry> ServerDatabase::auditEntries(quint64 beforeSequence, int limit) const
{
    QList<moderation::AuditEntry> result;
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("SELECT %1 FROM audit_log WHERE sequence < ? ORDER BY sequence DESC LIMIT ?")
                  .arg(QLatin1String(kAuditColumns)));
    q.addBindValue(beforeSequence == 0 ? std::numeric_limits<qint64>::max() : static_cast<qint64>(beforeSequence));
    q.addBindValue(limit);
    if (q.exec()) {
        while (q.next()) {
            result.append(auditFrom(q));
        }
    }
    return result;
}

quint64 ServerDatabase::auditCount() const
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    return (q.exec(QStringLiteral("SELECT COUNT(*) FROM audit_log")) && q.next()) ? q.value(0).toULongLong() : 0;
}

moderation::AuditVerification ServerDatabase::verifyAudit() const
{
    std::optional<moderation::AuditEntry> before;
    const QString anchorSequence = setting(QStringLiteral("audit.anchorSequence"));
    if (!anchorSequence.isEmpty()) {
        moderation::AuditEntry anchor;
        anchor.sequence = anchorSequence.toULongLong();
        anchor.hash = QByteArray::fromHex(setting(QStringLiteral("audit.anchorHash")).toLatin1());
        before = anchor;
    }
    // Checked in batches so a long log does not need to fit in memory at once.
    moderation::AuditVerification total;
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.setForwardOnly(true);
    if (!q.exec(QStringLiteral("SELECT %1 FROM audit_log ORDER BY sequence").arg(QLatin1String(kAuditColumns)))) {
        total.intact = false;
        total.problem = q.lastError().text();
        return total;
    }
    QList<moderation::AuditEntry> batch;
    const auto check = [&] {
        moderation::AuditVerification part = moderation::verifyAuditChain(batch, before);
        total.checked += part.checked;
        if (!part.intact) {
            total.intact = false;
            total.firstBadSequence = part.firstBadSequence;
            total.problem = part.problem;
            return false;
        }
        if (!batch.isEmpty()) {
            before = batch.last();
        }
        batch.clear();
        return true;
    };
    while (q.next()) {
        batch.append(auditFrom(q));
        if (batch.size() >= 1000 && !check()) {
            return total;
        }
    }
    check();
    return total;
}

int ServerDatabase::pruneAudit(qint64 cutoffMs)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    QSqlQuery last(db);
    last.prepare(QStringLiteral("SELECT %1 FROM audit_log WHERE time_ms < ? ORDER BY sequence DESC LIMIT 1")
                     .arg(QLatin1String(kAuditColumns)));
    last.addBindValue(cutoffMs);
    if (!last.exec() || !last.next()) {
        return 0;
    }
    const moderation::AuditEntry anchor = auditFrom(last);
    last.finish();
    if (!db.transaction()) {
        return 0;
    }
    QSqlQuery remove(db);
    remove.prepare(QStringLiteral("DELETE FROM audit_log WHERE sequence <= ?"));
    remove.addBindValue(static_cast<qint64>(anchor.sequence));
    const bool ok = remove.exec() &&
                    setSetting(QStringLiteral("audit.anchorSequence"), QString::number(anchor.sequence)) &&
                    setSetting(QStringLiteral("audit.anchorHash"), QString::fromLatin1(anchor.hash.toHex()));
    const int removed = ok ? remove.numRowsAffected() : 0;
    if (!ok) {
        db.rollback();
        return 0;
    }
    db.commit();
    return removed;
}

// ----------------------------------------------------------------- settings

QString ServerDatabase::setting(const QString &key, const QString &defaultValue) const
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("SELECT value FROM settings WHERE key = ?"));
    q.addBindValue(key);
    return (q.exec() && q.next()) ? q.value(0).toString() : defaultValue;
}

bool ServerDatabase::setSetting(const QString &key, const QString &value)
{
    QSqlQuery q(QSqlDatabase::database(m_connectionName, false));
    q.prepare(QStringLiteral("INSERT INTO settings (key, value) VALUES (?, ?) "
                             "ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    q.addBindValue(key);
    q.addBindValue(text(value));
    return q.exec();
}

} // namespace opentalk::server
