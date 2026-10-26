// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/storage/AppPaths.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace opentalk::storage;

class TestAppPaths : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void installedModeUsesStandardLocation()
    {
        QTemporaryDir appDir;
        const AppPaths paths = AppPaths::resolve(appDir.path());
        QVERIFY(paths.mode() == DataMode::Installed);
        QCOMPARE(paths.dataRoot(),
                 QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)));
    }

    void portableMarkerKeepsDataBesideApplication()
    {
        QTemporaryDir appDir;
        QFile marker(QDir(appDir.path()).filePath(AppPaths::portableMarkerFileName()));
        QVERIFY(marker.open(QIODevice::WriteOnly));
        marker.close();

        const AppPaths paths = AppPaths::resolve(appDir.path());
        QVERIFY(paths.mode() == DataMode::Portable);
        QCOMPARE(paths.dataRoot(), QDir::cleanPath(appDir.path() + QStringLiteral("/data")));
        QCOMPARE(paths.clientSettingsFile(), paths.dataRoot() + QStringLiteral("/config/client-settings.json"));
    }

    void overrideWinsOverPortableMarker()
    {
        QTemporaryDir appDir;
        QTemporaryDir custom;
        QFile marker(QDir(appDir.path()).filePath(AppPaths::portableMarkerFileName()));
        QVERIFY(marker.open(QIODevice::WriteOnly));
        marker.close();

        const AppPaths paths = AppPaths::resolve(appDir.path(), custom.path());
        QVERIFY(paths.mode() == DataMode::Custom);
        QCOMPARE(paths.dataRoot(), QDir::cleanPath(custom.path()));
    }

    void ensureDirectoriesCreatesLayout()
    {
        QTemporaryDir root;
        const AppPaths paths = AppPaths::resolve({}, root.filePath(QStringLiteral("nested/data")));
        QString error;
        QVERIFY2(paths.ensureDirectories(&error), qPrintable(error));
        QVERIFY(QDir(paths.configDir()).exists());
        QVERIFY(QDir(paths.logDir()).exists());
    }
};

QTEST_GUILESS_MAIN(TestAppPaths)
#include "tst_apppaths.moc"
