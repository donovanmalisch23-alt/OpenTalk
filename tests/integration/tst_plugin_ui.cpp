// SPDX-License-Identifier: GPL-3.0-only
// Plugins through the desktop app: installing from a folder, the consent dialog,
// the Plugins menu, notifications in the history, turning off and removing, and the
// accessibility of every plugin dialog.

#include "../unit/AudioTestUtil.h"
#include "UiTestUtil.h"

#include "opentalk/plugins/PluginHost.h"
#include "opentalk/plugins/PluginRegistry.h"
#include "opentalk/storage/AppPaths.h"
#include "opentalk/storage/ClientSettings.h"
#include "opentalk/storage/ServerProfiles.h"
#include "opentalk/storage/SettingsStore.h"
#include "opentalk/ui/MainWindow.h"
#include "opentalk/ui/PluginDialogs.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

using namespace opentalk;
using namespace opentalk::test;
using State = plugins::PluginProcess::State;

namespace {

const QString kTestPlugin = QStringLiteral(OPENTALK_TEST_PLUGIN);
constexpr int kWaitMs = 10'000;
const QString kId = QStringLiteral("org.test.echo");

QStringList menuTexts(QMenu *menu)
{
    QStringList texts;
    for (QAction *a : menu->actions()) {
        if (!a->isSeparator()) {
            texts << a->text();
        }
    }
    return texts;
}

} // namespace

class TestPluginUi : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    std::unique_ptr<storage::AppPaths> m_paths;
    std::unique_ptr<storage::SettingsStore> m_settings;
    std::unique_ptr<storage::ServerProfileStore> m_profiles;
    std::unique_ptr<ui::MainWindow> m_window;
    QString m_source;

    QString makeSource(const QStringList &capabilities)
    {
        const QString folder = m_dir.filePath(QStringLiteral("download/echo"));
        QDir(folder).removeRecursively();
        QDir().mkpath(folder);
        const QString program = QFileInfo(kTestPlugin).fileName();
        if (!QFile::copy(kTestPlugin, QDir(folder).filePath(program))) {
            return {};
        }
        QFile file(QDir(folder).filePath(QStringLiteral("plugin.json")));
        if (!file.open(QIODevice::WriteOnly)) {
            return {};
        }
        file.write(
            QJsonDocument(QJsonObject{{QStringLiteral("id"), kId},
                                      {QStringLiteral("name"), QStringLiteral("Test echo")},
                                      {QStringLiteral("version"), QStringLiteral("2.0")},
                                      {QStringLiteral("description"), QStringLiteral("Answers pings.")},
                                      {QStringLiteral("author"), QStringLiteral("Tests & Co")},
                                      {QStringLiteral("license"), QStringLiteral("GPL-3.0-only")},
                                      {QStringLiteral("executable"), program},
                                      {QStringLiteral("arguments"), QJsonArray{QStringLiteral("echo")}},
                                      {QStringLiteral("capabilities"), QJsonArray::fromStringList(capabilities)}})
                .toJson());
        return folder;
    }

private slots:
    void init()
    {
        m_paths =
            std::make_unique<storage::AppPaths>(storage::AppPaths::resolve({}, m_dir.filePath(QStringLiteral("app"))));
        QVERIFY(m_paths->ensureDirectories());
        m_settings = std::make_unique<storage::SettingsStore>(storage::client::schema(), m_paths->clientSettingsFile());
        m_settings->load();
        m_profiles = std::make_unique<storage::ServerProfileStore>(m_paths->serverProfilesFile());
        m_window = std::make_unique<ui::MainWindow>(*m_settings, *m_paths, *m_profiles,
                                                    std::make_unique<audio::SyntheticAudioBackend>());
        m_window->show();
        m_source = makeSource(
            {QStringLiteral("notifications.show"), QStringLiteral("commands.add"), QStringLiteral("messages.private")});
        QVERIFY(!m_source.isEmpty());
    }

    void cleanup()
    {
        m_window->pluginHost().stopAll();
        m_window.reset();
        QDir(m_dir.filePath(QStringLiteral("app"))).removeRecursively();
    }

    void installTurnOnUseAndRemove()
    {
        QCOMPARE(menuTexts(m_window->pluginsMenu()), QStringList{QStringLiteral("&Manage plugins...")});
        ui::PluginManagerDialog manager(m_window->pluginRegistry(), m_window->pluginHost());
        manager.show();
        QCOMPARE(manager.pluginList()->topLevelItem(0)->text(0), QStringLiteral("No plugins installed"));
        QVERIFY(manager.installFrom(m_source));
        QCOMPARE(manager.statusText(), QStringLiteral("Installed Test echo. Turn it on to use it."));
        QCOMPARE(manager.pluginList()->currentItem()->data(0, Qt::AccessibleTextRole).toString(),
                 QStringLiteral("Test echo, version 2.0, off"));
        QVERIFY(unnamedFocusableWidgets(&manager).isEmpty());

        // Turning it on asks first; untick the private-messages permission.
        whenDialogOpens<ui::PluginConsentDialog>([](ui::PluginConsentDialog *consent) {
            QListWidget *list = consent->permissionList();
            QCOMPARE(list->count(), 3);
            for (int i = 0; i < list->count(); ++i) {
                QCOMPARE(list->item(i)->checkState(), Qt::Checked);
                if (list->item(i)->text().startsWith(QStringLiteral("Sensitive: Read your private messages"))) {
                    list->item(i)->setCheckState(Qt::Unchecked);
                }
            }
            QVERIFY(unnamedFocusableWidgets(consent).isEmpty());
            consent->accept();
        });
        buttonWithText(&manager, QStringLiteral("Turn &on..."))->click();
        QCOMPARE(manager.statusText(), QStringLiteral("Test echo is on."));
        QVERIFY(!m_window->pluginRegistry().plugin(kId)->granted.contains(plugins::PluginCapability::PrivateRead));
        QTRY_VERIFY_WITH_TIMEOUT(m_window->pluginHost().state(kId) == State::Running, kWaitMs);
        QTRY_COMPARE_WITH_TIMEOUT(manager.pluginList()->currentItem()->text(2), QStringLiteral("running"), kWaitMs);

        // Its command appears in the Plugins menu, and its answer in the history.
        QTRY_COMPARE_WITH_TIMEOUT(
            menuTexts(m_window->pluginsMenu()),
            (QStringList{QStringLiteral("&Manage plugins..."), QStringLiteral("Say ping (Test echo)")}), kWaitMs);
        m_window->pluginsMenu()->actions().last()->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(m_window->historyText().contains(QStringLiteral("Test echo: pong")), kWaitMs);

        // Its log can be read.
        whenDialogOpens<QDialog>([](QDialog *log) {
            QVERIFY(log->windowTitle().startsWith(QStringLiteral("Log of Test echo")));
            QVERIFY(unnamedFocusableWidgets(log).isEmpty());
            log->reject();
        });
        buttonWithText(&manager, QStringLiteral("Show &log"))->click();

        // Turned off: it stops and its command goes.
        buttonWithText(&manager, QStringLiteral("Turn o&ff"))->click();
        QCOMPARE(manager.statusText(), QStringLiteral("Test echo is off."));
        QTRY_VERIFY_WITH_TIMEOUT(m_window->pluginHost().state(kId) == State::Stopped, kWaitMs);
        QTRY_COMPARE_WITH_TIMEOUT(menuTexts(m_window->pluginsMenu()), QStringList{QStringLiteral("&Manage plugins...")},
                                  kWaitMs);

        // A new start of the app does not run it.
        ui::MainWindow second(*m_settings, *m_paths, *m_profiles, std::make_unique<audio::SyntheticAudioBackend>());
        second.startPlugins();
        QVERIFY(second.pluginHost().state(kId) == State::Stopped);

        // Removing asks, then deletes its folder.
        whenDialogOpens<QMessageBox>(
            [](QMessageBox *box) { buttonWithText(box, QStringLiteral("Remove &everything"))->click(); });
        buttonWithText(&manager, QStringLiteral("Re&move..."))->click();
        QCOMPARE(manager.statusText(), QStringLiteral("Removed Test echo."));
        QVERIFY(!QFileInfo::exists(m_paths->dataRoot() + QStringLiteral("/plugins/") + kId));
    }

    void enabledPluginsStartWithTheAppAndUpdatesNeedReview()
    {
        ui::PluginManagerDialog manager(m_window->pluginRegistry(), m_window->pluginHost());
        QVERIFY(manager.installFrom(m_source));
        QVERIFY(m_window->pluginRegistry().enable(kId, {plugins::PluginCapability::CommandsAdd}));

        // Next start: it runs by itself.
        m_window.reset();
        m_window = std::make_unique<ui::MainWindow>(*m_settings, *m_paths, *m_profiles,
                                                    std::make_unique<audio::SyntheticAudioBackend>());
        m_window->startPlugins();
        QTRY_VERIFY_WITH_TIMEOUT(m_window->pluginHost().state(kId) == State::Running, kWaitMs);

        // An update that asks for more stops it until the user reviews it.
        ui::PluginManagerDialog again(m_window->pluginRegistry(), m_window->pluginHost());
        QVERIFY(!makeSource({QStringLiteral("commands.add"), QStringLiteral("messages.send")}).isEmpty());
        QVERIFY(again.installFrom(m_source));
        QCOMPARE(again.statusText(),
                 QStringLiteral("Updated Test echo. It changed, so turn it on again to review its permissions."));
        QVERIFY(m_window->pluginHost().state(kId) != State::Running);
        QVERIFY(m_window->pluginRegistry().plugin(kId)->needsReview);
        // The review marks the new permission and leaves it unticked.
        whenDialogOpens<ui::PluginConsentDialog>([](ui::PluginConsentDialog *consent) {
            QListWidget *list = consent->permissionList();
            QCOMPARE(list->count(), 2);
            QCOMPARE(list->item(1)->text(), QStringLiteral("Sensitive: Send messages in your name. (new)"));
            QCOMPARE(list->item(1)->checkState(), Qt::Unchecked);
            consent->accept();
        });
        again.show();
        buttonWithText(&again, QStringLiteral("Turn &on..."))->click();
        QTRY_VERIFY_WITH_TIMEOUT(m_window->pluginHost().state(kId) == State::Running, kWaitMs);
        QCOMPARE(m_window->pluginRegistry().plugin(kId)->granted,
                 QList<plugins::PluginCapability>{plugins::PluginCapability::CommandsAdd});
    }
};

QTEST_MAIN(TestPluginUi)
#include "tst_plugin_ui.moc"
