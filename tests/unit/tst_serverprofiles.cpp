// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/storage/ServerProfiles.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace opentalk::storage;

class TestServerProfiles : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString path() const { return m_dir.filePath(QStringLiteral("server-profiles.json")); }

    static ServerProfile sample()
    {
        ServerProfile p;
        p.name = QStringLiteral("Home");
        p.host = QStringLiteral("chat.example.org");
        p.port = 10350;
        p.guest = false;
        p.userName = QStringLiteral("alice");
        p.pinnedFingerprint = QStringLiteral("SHA256:AA");
        p.rememberPassword = true;
        return p;
    }

private slots:
    void init()
    {
        for (const QString &name : QDir(m_dir.path()).entryList(QDir::Files)) {
            QFile::remove(m_dir.filePath(name));
        }
    }

    void saveLoadUpsertRemove()
    {
        ServerProfileStore store(path());
        QVERIFY(store.load().isEmpty());
        const QString id = store.upsert(sample());
        QVERIFY(!id.isEmpty());
        QVERIFY(store.save());

        ServerProfileStore reloaded(path());
        QVERIFY(reloaded.load().isEmpty());
        QCOMPARE(reloaded.profiles().size(), 1);
        ServerProfile p = *reloaded.find(id);
        QCOMPARE(p.userName, QStringLiteral("alice"));
        QVERIFY(!p.guest);
        QVERIFY(p.rememberPassword); // the choice is kept; the password itself is not
        p.name = QStringLiteral("Renamed");
        QCOMPARE(reloaded.upsert(p), id);
        QCOMPARE(reloaded.profiles().size(), 1);
        QVERIFY(reloaded.remove(id));
        QVERIFY(reloaded.profiles().isEmpty());

        QFile file(path());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().contains("password")); // passwords are never written
    }

    void damagedFileIsSetAside()
    {
        QFile file(path());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[not, valid");
        file.close();
        ServerProfileStore store(path());
        const QStringList warnings = store.load();
        QCOMPARE(warnings.size(), 1);
        QVERIFY(!QFile::exists(path()));
        QCOMPARE(QDir(m_dir.path()).entryList({QStringLiteral("server-profiles.json.corrupt-*")}).size(), 1);
    }

    void invalidEntriesAreSkipped()
    {
        QFile file(path());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"format":"opentalk-servers","profiles":[
            {"id":"a","name":"Ok","host":"h","port":1},
            {"id":"b","name":"","host":"h","port":1},
            {"id":"c","name":"Bad port","host":"h","port":70000},
            "not an object"]})");
        file.close();
        ServerProfileStore store(path());
        QCOMPARE(store.load().size(), 3);
        QCOMPARE(store.profiles().size(), 1);
        QCOMPARE(store.profiles().first().name, QStringLiteral("Ok"));
    }
};

QTEST_GUILESS_MAIN(TestServerProfiles)
#include "tst_serverprofiles.moc"
