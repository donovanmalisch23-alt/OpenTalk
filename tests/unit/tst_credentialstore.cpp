// SPDX-License-Identifier: GPL-3.0-only
// Remembered passwords live only in the operating system's credential store. On Windows
// this exercises the real Credential Manager; elsewhere the store must say it is not
// available rather than pretend.

#include "opentalk/storage/CredentialStore.h"

#include <QRandomGenerator>
#include <QTest>

using namespace opentalk::storage;

class TestCredentialStore : public QObject {
    Q_OBJECT

private slots:
    void memoryStore()
    {
        MemoryCredentialStore store;
        QVERIFY(store.isAvailable());
        QVERIFY(!store.readPassword(QStringLiteral("a")));
        QVERIFY(store.writePassword(QStringLiteral("a"), QStringLiteral("alice"), QStringLiteral("secret"), nullptr));
        QCOMPARE(*store.readPassword(QStringLiteral("a")), QStringLiteral("secret"));
        QVERIFY(store.removePassword(QStringLiteral("a")));
        QVERIFY(store.removePassword(QStringLiteral("a"))); // already gone: still fine
        QCOMPARE(store.size(), 0);
    }

    void systemStore()
    {
        const auto store = createSystemCredentialStore();
#ifdef Q_OS_WIN
        QVERIFY(store->isAvailable());
        QCOMPARE(store->name(), QStringLiteral("Windows Credential Manager"));
        const QString key = QStringLiteral("test-%1").arg(QRandomGenerator::system()->generate64(), 0, 16);
        const QString password = QStringLiteral("pässwörd \U0001F511 with spaces");
        QString error;
        if (!store->writePassword(key, QStringLiteral("alice"), password, &error)) {
            // Some service accounts have no credential store at all.
            QSKIP(qPrintable(QStringLiteral("Credential Manager is not usable here: %1").arg(error)));
        }
        const auto read = store->readPassword(key);
        QVERIFY(store->removePassword(key));
        QVERIFY(read.has_value());
        QCOMPARE(*read, password);
        QVERIFY(!store->readPassword(key));
        QVERIFY(store->removePassword(key));
#else
        QVERIFY(!store->isAvailable());
        QString error;
        QVERIFY(!store->writePassword(QStringLiteral("k"), QStringLiteral("alice"), QStringLiteral("x"), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!store->readPassword(QStringLiteral("k")));
        QVERIFY(store->removePassword(QStringLiteral("k")));
#endif
    }
};

QTEST_GUILESS_MAIN(TestCredentialStore)
#include "tst_credentialstore.moc"
