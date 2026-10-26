// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/logging/Logging.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace opentalk::logging;

class TestLogging : public QObject {
    Q_OBJECT

private slots:
    void formatsOneLine()
    {
        const QDateTime time(QDate(2026, 10, 26), QTime(12, 30, 5, 123), QTimeZone::utc());
        QCOMPARE(formatLine(QtWarningMsg, "opentalk.storage", QStringLiteral("disk full"), time),
                 QStringLiteral("2026-10-26T12:30:05.123Z WARN  opentalk.storage: disk full"));
    }

    void newlinesAreEscaped()
    {
        const QDateTime time(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::utc());
        const QString line = formatLine(QtInfoMsg, "x", QStringLiteral("a\nfake line\r"), time);
        QVERIFY(!line.contains(QLatin1Char('\n')));
        QVERIFY(line.endsWith(QStringLiteral("a\\nfake line\\r")));
    }

    void levelNamesRoundTrip()
    {
        for (Level level : {Level::Debug, Level::Info, Level::Warning}) {
            QCOMPARE(levelFromString(levelToString(level)), std::optional<Level>(level));
        }
        QCOMPARE(levelFromString(QStringLiteral("verbose")), std::nullopt);
    }

    void writesAndFiltersByLevel()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("test.log"));
        QVERIFY(install({path, Level::Warning, false}));
        qCInfo(lcApp) << "hidden-info";
        qCWarning(lcApp) << "visible-warning";
        setMinimumLevel(Level::Debug);
        qCDebug(lcApp) << "visible-debug";
        shutdown();

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString contents = QString::fromUtf8(file.readAll());
        QVERIFY(!contents.contains(QStringLiteral("hidden-info")));
        QVERIFY(contents.contains(QStringLiteral("opentalk.app: visible-warning")));
        QVERIFY(contents.contains(QStringLiteral("visible-debug")));
    }
};

QTEST_GUILESS_MAIN(TestLogging)
#include "tst_logging.moc"
