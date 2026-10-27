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
        QVERIFY(error.contains(QStringLiteral("already exists")));
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
    }
};

QTEST_GUILESS_MAIN(TestServerDatabase)
#include "tst_serverdatabase.moc"
