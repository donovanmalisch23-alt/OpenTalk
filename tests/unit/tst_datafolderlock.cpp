// SPDX-License-Identifier: GPL-3.0-only
// One OpenTalk per data folder, and noticing when the previous one did not close
// properly. A crash is simulated by running this program again as a child that takes
// the lock and ends without cleaning up.

#include "opentalk/storage/DataFolderLock.h"

#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <cstdlib>
#include <memory>

using opentalk::storage::DataFolderLock;

class TestDataFolderLock : public QObject {
    Q_OBJECT

private slots:
    void onlyOneCopyAtATime()
    {
        QTemporaryDir dir;
        auto first = std::make_unique<DataFolderLock>(dir.path());
        QCOMPARE(first->acquire(), DataFolderLock::Result::Acquired);
        DataFolderLock second(dir.path());
        QCOMPARE(second.acquire(), DataFolderLock::Result::InUse);
        first.reset(); // the first copy closes properly
        DataFolderLock third(dir.path());
        QCOMPARE(third.acquire(), DataFolderLock::Result::Acquired);
    }

    void aCrashIsNoticedNextTime()
    {
        QTemporaryDir dir;
        QProcess crashing;
        crashing.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--hold-and-crash"), dir.path()});
        QVERIFY(crashing.waitForFinished(10'000));
        QCOMPARE(crashing.exitCode(), 3);
        DataFolderLock next(dir.path());
        QCOMPARE(next.acquire(), DataFolderLock::Result::AcquiredAfterUnexpectedExit);
    }
};

int main(int argc, char *argv[])
{
    if (argc == 3 && qstrcmp(argv[1], "--hold-and-crash") == 0) {
        QCoreApplication app(argc, argv);
        DataFolderLock lock(QString::fromLocal8Bit(argv[2]));
        if (lock.acquire() != DataFolderLock::Result::Acquired) {
            return 1;
        }
        std::_Exit(3); // no destructors: the lock file stays behind, as after a crash
    }
    QCoreApplication app(argc, argv);
    TestDataFolderLock test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_datafolderlock.moc"
