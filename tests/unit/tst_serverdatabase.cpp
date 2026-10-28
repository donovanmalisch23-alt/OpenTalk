// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/server/ServerDatabase.h"

#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace opentalk;
using namespace opentalk::server;

class TestServerDatabase : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString path(const QString &name = QStringLiteral("server.db")) const { return m_dir.filePath(name); }

private slots:
    void usernameRules()
    {
        QVERIFY(ServerDatabase::isValidUsername(QStringLiteral("alice")));
        QVERIFY(ServerDatabase::isValidUsername(QStringLiteral("Bob_99.x-y")));
        QVERIFY(!ServerDatabase::isValidUsername(QString()));
        QVERIFY(!ServerDatabase::isValidUsername(QString(33, QLatin1Char('a'))));
        QVERIFY(!ServerDatabase::isValidUsername(QStringLiteral("has space")));
        QVERIFY(!ServerDatabase::isValidUsername(QStringLiteral("аlice"))); // Cyrillic "а"
    }

    void accountsRoundTripAndIgnoreCase()
    {
        ServerDatabase db;
        QString error;
        QVERIFY2(db.open(path(QStringLiteral("accounts.db")), &error), qPrintable(error));
        QVERIFY(db.createAccount(QStringLiteral("Alice"), "hash1", protocol::Role::Admin, &error));
        QVERIFY(!db.createAccount(QStringLiteral("alice"), "hash2", protocol::Role::User, &error));
        QVERIFY2(error.contains(QStringLiteral("already exists")), qPrintable(error));
        QVERIFY(!db.createAccount(QStringLiteral("guesty"), "hash", protocol::Role::Guest));
        QVERIFY(!db.createAccount(QStringLiteral("nopass"), QByteArray(), protocol::Role::User));

        const auto found = db.findAccount(QStringLiteral("ALICE"));
        QVERIFY(found.has_value());
        QCOMPARE(found->username, QStringLiteral("Alice"));
        QVERIFY(found->role == protocol::Role::Admin);
        QCOMPARE(found->passwordHash, QByteArray("hash1"));
        QCOMPARE(db.adminCount(), 1);

        QVERIFY(db.setPasswordHash(QStringLiteral("alice"), "hash3"));
        QCOMPARE(db.findAccount(QStringLiteral("alice"))->passwordHash, QByteArray("hash3"));
        QVERIFY(db.setRole(QStringLiteral("alice"), protocol::Role::User));
        QCOMPARE(db.adminCount(), 0);
        QVERIFY(!db.findAccount(QStringLiteral("alice"))->bot);
        QVERIFY(db.setBot(QStringLiteral("alice"), true));
        QVERIFY(db.findAccount(QStringLiteral("alice"))->bot);
        QVERIFY(db.accounts().first().bot);
        QVERIFY(!db.setBot(QStringLiteral("nobody"), true));
        QVERIFY(db.deleteAccount(QStringLiteral("alice")));
        QVERIFY(!db.findAccount(QStringLiteral("alice")));
        QVERIFY(!db.deleteAccount(QStringLiteral("alice")));
    }

    void dataSurvivesReopen()
    {
        {
            ServerDatabase db;
            QVERIFY(db.open(path(QStringLiteral("persist.db"))));
            QVERIFY(db.createAccount(QStringLiteral("carol"), "h", protocol::Role::User));
            StoredChannel c{0, 0, QStringLiteral("Lobby"), QStringLiteral("Say hi"), QByteArray(), 10};
            QVERIFY(db.insertChannel(c));
            QCOMPARE(c.id, quint32(1000)); // IDs below 1000 are reserved
            StoredChannel child{0, c.id, QStringLiteral("Quiet room"), QString(), "pwhash", 0};
            QString insertError;
            QVERIFY2(db.insertChannel(child, &insertError), qPrintable(insertError));
            QCOMPARE(child.id, quint32(1001));
        }
        ServerDatabase db;
        QVERIFY(db.open(path(QStringLiteral("persist.db"))));
        QVERIFY(db.findAccount(QStringLiteral("carol")).has_value());
        const QList<StoredChannel> channels = db.channels();
        QCOMPARE(channels.size(), 2);
        QCOMPARE(channels[0].name, QStringLiteral("Lobby"));
        QCOMPARE(channels[0].maxUsers, quint16(10));
        QVERIFY(channels[0].passwordHash.isEmpty());
        QCOMPARE(channels[1].parentId, quint32(1000));
        QCOMPARE(channels[1].passwordHash, QByteArray("pwhash"));
        QVERIFY(db.deleteChannel(1001));
        QCOMPARE(db.channels().size(), 1);
    }

    void newerDatabaseIsRefusedAndUntouched()
    {
        const QString file = path(QStringLiteral("newer.db"));
        {
            QSqlDatabase raw = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("raw"));
            raw.setDatabaseName(file);
            QVERIFY(raw.open());
            QSqlQuery(raw).exec(QStringLiteral("PRAGMA user_version = 99"));
            raw.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("raw"));
        ServerDatabase db;
        QString error;
        QVERIFY(!db.open(file, &error));
        QVERIFY(error.contains(QStringLiteral("newer")));
        QVERIFY(!db.isOpen());
    }

    void roleNames()
    {
        QVERIFY(roleFromString(QStringLiteral("admin")) == protocol::Role::Admin);
        QVERIFY(roleFromString(QStringLiteral("user")) == protocol::Role::User);
        QVERIFY(!roleFromString(QStringLiteral("guest")));
        QCOMPARE(toString(protocol::Role::Admin), QStringLiteral("admin"));
        QVERIFY(roleFromString(QStringLiteral("owner")) == protocol::Role::Owner);
        QVERIFY(roleFromString(QStringLiteral("moderator")) == protocol::Role::Moderator);
    }

    void upgradesAPhase3DatabaseAndAppointsAnOwner()
    {
        const QString file = path(QStringLiteral("v1.db"));
        {
            // The exact version 1 schema, with two administrators and a user.
            QSqlDatabase raw = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v1"));
            raw.setDatabaseName(file);
            QVERIFY(raw.open());
            QSqlQuery q(raw);
            QVERIFY(
                q.exec(QStringLiteral("CREATE TABLE accounts (id INTEGER PRIMARY KEY, username TEXT NOT NULL UNIQUE "
                                      "COLLATE NOCASE, role INTEGER NOT NULL, password_hash BLOB NOT NULL, disabled "
                                      "INTEGER NOT NULL DEFAULT 0, created_at TEXT NOT NULL)")));
            QVERIFY(
                q.exec(QStringLiteral("CREATE TABLE channels (id INTEGER PRIMARY KEY AUTOINCREMENT, parent_id INTEGER "
                                      "NOT NULL DEFAULT 0, name TEXT NOT NULL, topic TEXT NOT NULL DEFAULT '', "
                                      "password_hash BLOB, max_users INTEGER NOT NULL DEFAULT 0, created_at TEXT NOT "
                                      "NULL)")));
            QVERIFY(q.exec(QStringLiteral("INSERT INTO accounts (username, role, password_hash, created_at) VALUES "
                                          "('second', 2, x'01', '2026-05-02T00:00:00Z'), "
                                          "('first', 2, x'01', '2026-05-01T00:00:00Z'), "
                                          "('member', 1, x'01', '2026-04-01T00:00:00Z')")));
            QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 1")));
            raw.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("v1"));

        ServerDatabase db;
        QString error;
        QVERIFY2(db.open(file, &error), qPrintable(error));
        QVERIFY(QFile::exists(file + QStringLiteral(".v1-backup")));
        // The earliest administrator owns the server now; everyone else is unchanged.
        QCOMPARE(db.owner()->username, QStringLiteral("first"));
        QVERIFY(db.findAccount(QStringLiteral("second"))->role == protocol::Role::Admin);
        QVERIFY(db.findAccount(QStringLiteral("member"))->role == protocol::Role::User);
        QVERIFY(!db.findAccount(QStringLiteral("member"))->bot); // schema 3 added the column
        QCOMPARE(db.roles().size(), moderation::builtInRoles().size());
        const auto audit = db.auditEntries(0, 10);
        QCOMPARE(audit.size(), 1);
        QCOMPARE(audit.first().action, QStringLiteral("server.database-upgraded"));
        QVERIFY(audit.first().details.contains(QStringLiteral("owner=first")));
        QVERIFY(db.verifyAudit().intact);
    }

    void moderationDataRoundTrips()
    {
        ServerDatabase db;
        QVERIFY(db.open(path(QStringLiteral("moderation.db"))));
        moderation::RoleDefinition role{0, QStringLiteral("Greeter"), 20, {moderation::Capability::UserWarn}, false};
        QVERIFY(db.saveRole(role));
        QCOMPARE(role.id, moderation::roles::kFirstCustom);
        moderation::RoleDefinition duplicate{0, QStringLiteral("greeter"), 21, {}, false};
        QString error;
        QVERIFY(!db.saveRole(duplicate, &error));
        QVERIFY2(error.contains(QStringLiteral("already exists")), qPrintable(error));
        QVERIFY(!db.deleteRole(moderation::roles::kUser)); // built in

        moderation::PermissionRule rule{
            0, 1000, moderation::PermissionRule::Subject::Role, 1, moderation::Capability::VoiceTransmit, false};
        QVERIFY(db.insertRule(rule));
        QCOMPARE(db.rules().size(), 1);
        QVERIFY(db.rules().first() == rule);
        db.deleteRulesForChannel(1000);
        QVERIFY(db.rules().isEmpty());

        moderation::Sanction ban;
        ban.type = moderation::SanctionType::Ban;
        ban.targetAccountId = 5;
        ban.targetName = QStringLiteral("troll");
        ban.reason = QStringLiteral("spam");
        ban.createdMs = 1000;
        ban.expiresMs = 2000;
        QVERIFY(db.insertSanction(ban));
        ban.state = moderation::SanctionState::Revoked;
        ban.resolvedBy = QStringLiteral("mod");
        QVERIFY(db.updateSanction(ban));
        QVERIFY(db.sanctions().first() == ban);

        QVERIFY(db.setSetting(QStringLiteral("server.name"), QStringLiteral("Café")));
        QCOMPARE(db.setting(QStringLiteral("server.name")), QStringLiteral("Café"));
        QCOMPARE(db.setting(QStringLiteral("missing"), QStringLiteral("default")), QStringLiteral("default"));
    }

    void auditPruningKeepsTheChainCheckable()
    {
        ServerDatabase db;
        QVERIFY(db.open(path(QStringLiteral("audit.db"))));
        for (int i = 0; i < 10; ++i) {
            moderation::AuditEntry e;
            e.timeMs = 1000 * (i + 1);
            e.action = QStringLiteral("test.%1").arg(i);
            QVERIFY(db.appendAudit(e));
        }
        const quint64 total = db.auditCount();
        QVERIFY(db.pruneAudit(5500) > 0); // removes entries older than the cut-off
        QVERIFY(db.auditCount() < total);
        QVERIFY(db.verifyAudit().intact); // still checkable from the anchor
        moderation::AuditEntry next;
        next.timeMs = 20000;
        next.action = QStringLiteral("after.prune");
        QVERIFY(db.appendAudit(next));
        QVERIFY(db.verifyAudit().intact);
    }
};

QTEST_GUILESS_MAIN(TestServerDatabase)
#include "tst_serverdatabase.moc"
