// SPDX-License-Identifier: GPL-3.0-only
// Automated accessibility and keyboard checks for the main window and settings dialog.
// These complement, and do not replace, manual testing with NVDA (docs/accessibility/plan.md).

#include "opentalk/storage/AppPaths.h"
#include "opentalk/storage/ClientSettings.h"
#include "opentalk/storage/ServerProfiles.h"
#include "opentalk/storage/SettingsStore.h"
#include "opentalk/ui/AudioController.h"
#include "opentalk/ui/MainWindow.h"
#include "opentalk/ui/SettingsDialog.h"
#include "opentalk/ui/SupportFileDialog.h"
#include "opentalk/ui/SupportReport.h"

#include <QAbstractSpinBox>
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace opentalk;

namespace {

/// Returns the focusable, visible widgets under `root` that a screen reader would
/// announce without a name.
QStringList unnamedFocusableWidgets(QWidget *root)
{
    QStringList problems;
    const QList<QWidget *> widgets = root->findChildren<QWidget *>();
    for (QWidget *widget : widgets) {
        if (widget->focusPolicy() == Qt::NoFocus || !widget->isVisibleTo(root)) {
            continue;
        }
        // A spin box's internal editor is not exposed separately; the spin box itself is
        // checked (and named) as its own focusable widget.
        if (qobject_cast<QAbstractSpinBox *>(widget->parentWidget()) != nullptr) {
            continue;
        }
        QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(widget);
        const QString name = iface != nullptr ? iface->text(QAccessible::Name).trimmed() : QString();
        if (name.isEmpty()) {
            problems << QString::fromLatin1(widget->metaObject()->className()) + QLatin1Char(' ') +
                            widget->objectName();
        }
    }
    return problems;
}

} // namespace

class TestMainWindow : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    std::unique_ptr<storage::AppPaths> m_paths;
    std::unique_ptr<storage::SettingsStore> m_settings;
    std::unique_ptr<storage::ServerProfileStore> m_profiles;

private slots:
    void init()
    {
        m_paths = std::make_unique<storage::AppPaths>(storage::AppPaths::resolve({}, m_dir.path()));
        QVERIFY(m_paths->ensureDirectories());
        QFile::remove(m_paths->clientSettingsFile());
        m_settings = std::make_unique<storage::SettingsStore>(storage::client::schema(), m_paths->clientSettingsFile());
        m_settings->load();
        m_profiles = std::make_unique<storage::ServerProfileStore>(m_paths->serverProfilesFile());
    }

    void everyFocusableControlHasAnAccessibleName()
    {
        ui::MainWindow window(*m_settings, *m_paths, *m_profiles, std::make_unique<audio::SyntheticAudioBackend>());
        window.show();
        const QStringList problems = unnamedFocusableWidgets(&window);
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral(", "))));
    }

    void paneShortcutsAreAssignedAndWork()
    {
        ui::MainWindow window(*m_settings, *m_paths, *m_profiles, std::make_unique<audio::SyntheticAudioBackend>());
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        const QList<QAction *> actions = window.paneActions();
        QCOMPARE(actions.size(), 4);
        const ui::MainWindow::Pane panes[] = {ui::MainWindow::Pane::Channels, ui::MainWindow::Pane::Users,
                                              ui::MainWindow::Pane::History, ui::MainWindow::Pane::MessageInput};
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(actions[i]->shortcut(), QKeySequence(Qt::CTRL | static_cast<Qt::Key>(Qt::Key_1 + i)));
            QTest::keyClick(&window, static_cast<Qt::Key>(Qt::Key_1 + i), Qt::ControlModifier);
            QCOMPARE(QApplication::focusWidget(), window.paneWidget(panes[i]));
        }
        QCOMPARE(window.settingsAction()->shortcut(), QKeySequence(Qt::Key_F4));
    }

    void f6CyclesThroughPanesInVisualOrder()
    {
        ui::MainWindow window(*m_settings, *m_paths, *m_profiles, std::make_unique<audio::SyntheticAudioBackend>());
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        using Pane = ui::MainWindow::Pane;
        window.focusPane(Pane::Channels);
        const Pane expected[] = {Pane::History, Pane::MessageInput, Pane::Users, Pane::Channels};
        for (Pane pane : expected) {
            QTest::keyClick(&window, Qt::Key_F6);
            QCOMPARE(QApplication::focusWidget(), window.paneWidget(pane));
        }
        QTest::keyClick(&window, Qt::Key_F6, Qt::ShiftModifier);
        QCOMPARE(QApplication::focusWidget(), window.paneWidget(Pane::Users));
    }

    void sendingWhileDisconnectedExplainsWhyAndKeepsText()
    {
        ui::MainWindow window(*m_settings, *m_paths, *m_profiles, std::make_unique<audio::SyntheticAudioBackend>());
        window.show();
        auto *input = qobject_cast<QLineEdit *>(window.paneWidget(ui::MainWindow::Pane::MessageInput));
        QVERIFY(input != nullptr);
        input->setText(QStringLiteral("hello"));
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(window.statusText().contains(QStringLiteral("not connected")));
        QCOMPARE(input->text(), QStringLiteral("hello"));
    }

    void closingSavesLayout()
    {
        {
            ui::MainWindow window(*m_settings, *m_paths, *m_profiles, std::make_unique<audio::SyntheticAudioBackend>());
            window.show();
            window.close();
        }
        storage::SettingsStore reloaded(storage::client::schema(), m_paths->clientSettingsFile());
        QVERIFY(reloaded.load().status == storage::LoadReport::Status::Loaded);
        QVERIFY(!reloaded.value(storage::client::kMainWindowGeometry).toByteArray().isEmpty());
    }

    void settingsDialogIsNamedAndNavigable()
    {
        ui::SettingsDialog dialog(*m_settings, *m_paths);
        dialog.show();
        dialog.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));

        for (int page = 0; page < dialog.pages()->count(); ++page) {
            dialog.categoryList()->setCurrentRow(page);
            const QStringList problems = unnamedFocusableWidgets(&dialog);
            QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral(", "))));
        }

        dialog.categoryList()->setCurrentRow(0);
        QCOMPARE(QApplication::focusWidget(), dialog.categoryList());
        QTest::keyClick(&dialog, Qt::Key_Tab, Qt::ControlModifier);
        QCOMPARE(dialog.pages()->currentIndex(), 1);
        QTest::keyClick(&dialog, Qt::Key_PageUp, Qt::ControlModifier);
        QCOMPARE(dialog.pages()->currentIndex(), 0);
        QTest::keyClick(&dialog, Qt::Key_PageUp, Qt::ControlModifier); // wraps around
        QCOMPARE(dialog.pages()->currentIndex(), dialog.pages()->count() - 1);
    }

    void settingsDialogAppliesAndSaves()
    {
        ui::SettingsDialog dialog(*m_settings, *m_paths);
        auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("logLevel"));
        QVERIFY(combo != nullptr);
        combo->setCurrentIndex(combo->findData(QStringLiteral("debug")));
        const QList<QCheckBox *> boxes = dialog.findChildren<QCheckBox *>();
        for (QCheckBox *box : boxes) {
            box->setChecked(false);
        }
        QVERIFY(dialog.apply());

        storage::SettingsStore reloaded(storage::client::schema(), m_paths->clientSettingsFile());
        reloaded.load();
        QCOMPARE(reloaded.value(storage::client::kLogLevel).toString(), QStringLiteral("debug"));
        QCOMPARE(reloaded.value(storage::client::kRestoreWindowLayout).toBool(), false);
        QCOMPARE(reloaded.value(storage::client::kAnnounceStatusChanges).toBool(), false);
    }

    void audioSettingsAreSaved()
    {
        ui::SettingsDialog dialog(*m_settings, *m_paths);
        dialog.setKeyCapture([] { return std::optional<QKeySequence>(QKeySequence(Qt::Key_F12)); });
        QVERIFY(dialog.selectCategory(QStringLiteral("Audio")));
        QPushButton *change = nullptr;
        for (QPushButton *button : dialog.findChildren<QPushButton *>()) {
            if (QString(button->text()).remove(QLatin1Char('&')).contains(QStringLiteral("Change key"))) {
                change = button;
            }
        }
        QVERIFY(change != nullptr);
        change->click();
        auto *mode = dialog.findChild<QComboBox *>(QStringLiteral("transmitMode"));
        QVERIFY(mode != nullptr);
        mode->setCurrentIndex(mode->findData(storage::client::kTransmitVoiceActivation));
        QVERIFY(dialog.apply());

        storage::SettingsStore reloaded(storage::client::schema(), m_paths->clientSettingsFile());
        reloaded.load();
        QCOMPARE(reloaded.value(storage::client::kPushToTalkKey).toString(), QStringLiteral("F12"));
        QCOMPARE(reloaded.value(storage::client::kTransmitMode).toString(), storage::client::kTransmitVoiceActivation);
    }

    void audioMenuTogglesAndAnnounces()
    {
        ui::MainWindow window(*m_settings, *m_paths, *m_profiles, std::make_unique<audio::SyntheticAudioBackend>());
        QStringList spoken;
        window.setAnnouncementHook([&spoken](const QString &text) { spoken.append(text); });
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QTest::keyClick(&window, Qt::Key_M, Qt::ControlModifier);
        QVERIFY(window.audio().isMicrophoneMuted());
        QCOMPARE(window.voiceStatusText(), QStringLiteral("Microphone muted"));
        QTest::keyClick(&window, Qt::Key_M, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(window.audio().isDeafened());
        QCOMPARE(window.voiceStatusText(), QStringLiteral("Sound muted, Microphone muted"));
        QTest::keyClick(&window, Qt::Key_M, Qt::ControlModifier);
        QTest::keyClick(&window, Qt::Key_M, Qt::ControlModifier | Qt::ShiftModifier);
        QTest::keyClick(&window, Qt::Key_T, Qt::ControlModifier);
        QVERIFY(window.audio().isTransmitLatched());
        QCOMPARE(window.voiceStatusText(), QStringLiteral("Talk on")); // not connected, so not sending
        QTRY_VERIFY(spoken.contains(QStringLiteral("Talking on")));
        QVERIFY(spoken.contains(QStringLiteral("Microphone muted")));
    }

    void supportReportHidesTheHomeFolder()
    {
        ui::SupportReportInput input;
        input.version = QStringLiteral("1.2.3");
        input.homeFolder = QStringLiteral("/home/alice");
        input.dataMode = QStringLiteral("installed");
        input.dataFolder = QStringLiteral("/home/alice/.local/share/OpenTalk");
        input.settings = {{QStringLiteral("media/lastFolder"), QStringLiteral("(left out)")}};
        input.log = QStringLiteral("line one\nopened /home/alice/music/song.mp3");
        const QString report =
            ui::buildSupportReport(input, QDateTime(QDate(2026, 10, 29), QTime(12, 30), QTimeZone::utc()));
        QVERIFY(report.contains(QStringLiteral("Created: 2026-10-29 12:30 UTC")));
        QVERIFY(report.contains(QStringLiteral("Version: 1.2.3")));
        QVERIFY(report.contains(QStringLiteral("media/lastFolder = (left out)")));
        QVERIFY(report.contains(QStringLiteral("Plugins ==\nInstalled: none")));
        QVERIFY(!report.contains(QStringLiteral("alice")));
        QVERIFY(report.contains(QStringLiteral("opened ~/music/song.mp3")));
    }

    void logTailKeepsTheLastLines()
    {
        const QString log = m_dir.filePath(QStringLiteral("tail.log"));
        QFile file(log);
        QVERIFY(file.open(QIODevice::WriteOnly));
        for (int i = 1; i <= 500; ++i) {
            file.write(QByteArray("line ") + QByteArray::number(i) + "\r\n");
        }
        file.close();
        const QStringList lines = ui::readLogTail(log, 200).split(QLatin1Char('\n'));
        QCOMPARE(lines.size(), 200);
        QCOMPARE(lines.first(), QStringLiteral("line 301"));
        QCOMPARE(lines.last(), QStringLiteral("line 500"));
        QVERIFY(ui::readLogTail(m_dir.filePath(QStringLiteral("missing.log")), 10).isEmpty());
    }

    void supportFileShowsWhatHelpsAndNothingPrivate()
    {
        m_settings->setValue(storage::client::kLocalServerAdminName, QStringLiteral("SecretAdminName"));
        m_settings->setValue(storage::client::kUserVolumes, QVariantMap{{QStringLiteral("Bobby Tables"), 150}});
        ui::MainWindow window(*m_settings, *m_paths, *m_profiles, std::make_unique<audio::SyntheticAudioBackend>());
        window.show();
        QString shown;
        QStringList unnamed;
        QString saved;
        const QString target = m_dir.filePath(QStringLiteral("support.txt"));
        window.setSupportDialogHook([&](ui::SupportFileDialog &dialog) {
            dialog.show();
            unnamed = unnamedFocusableWidgets(&dialog);
            shown = dialog.text();
            dialog.setFileChooser([&target](const QString &suggested) {
                return suggested.endsWith(QStringLiteral(".txt")) ? target : QString();
            });
            dialog.save();
            saved = dialog.resultText();
        });
        auto *action = window.findChild<QAction *>(QStringLiteral("supportFileAction"));
        QVERIFY(action != nullptr);
        action->trigger();
        QVERIFY2(unnamed.isEmpty(), qPrintable(unnamed.join(QStringLiteral(", "))));
        QVERIFY(shown.contains(QStringLiteral("OpenTalk support file")));
        QVERIFY(shown.contains(QStringLiteral("Connection: disconnected")));
        QVERIFY(shown.contains(QStringLiteral("Microphones:")));
        QVERIFY(shown.contains(QStringLiteral("audio/transmitMode = pushToTalk")));
        QVERIFY(shown.contains(QStringLiteral("localServer/adminName = (left out)")));
        QVERIFY(shown.contains(QStringLiteral("== End of the log ==")));
        QVERIFY(!shown.contains(QStringLiteral("SecretAdminName")));
        QVERIFY(!shown.contains(QStringLiteral("Bobby Tables")));
        QVERIFY(saved.startsWith(QStringLiteral("Saved as")));
        QFile file(target);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(file.readAll()), shown);
    }

    void settingsCanBeExportedAndImported()
    {
        ui::MainWindow window(*m_settings, *m_paths, *m_profiles, std::make_unique<audio::SyntheticAudioBackend>());
        const QString exported = m_dir.filePath(QStringLiteral("my-settings.json"));
        window.setSettingsFileChooser([&exported](bool) { return exported; });
        m_settings->setValue(storage::client::kVoiceBitrate, 48);
        window.findChild<QAction *>(QStringLiteral("exportSettingsAction"))->trigger();
        QVERIFY(QFile::exists(exported));
        QVERIFY(window.historyText().contains(QStringLiteral("Settings exported to")));

        m_settings->setValue(storage::client::kVoiceBitrate, 24);
        window.findChild<QAction *>(QStringLiteral("importSettingsAction"))->trigger();
        QCOMPARE(m_settings->value(storage::client::kVoiceBitrate).toInt(), 48);
        QVERIFY2(window.historyText().contains(QStringLiteral("Settings imported: 1 changed.")),
                 qPrintable(window.historyText()));
        QVERIFY(window.historyText().contains(QStringLiteral("Your previous settings were kept in")));
        const QStringList backups =
            QDir(m_paths->configDir()).entryList({QStringLiteral("client-settings.before-import-*.json")});
        QCOMPARE(backups.size(), 1);
        // The import is saved, so it lasts.
        storage::SettingsStore reloaded(storage::client::schema(), m_paths->clientSettingsFile());
        reloaded.load();
        QCOMPARE(reloaded.value(storage::client::kVoiceBitrate).toInt(), 48);
    }

    void anUnexpectedExitIsMentioned()
    {
        ui::MainWindow window(*m_settings, *m_paths, *m_profiles, std::make_unique<audio::SyntheticAudioBackend>());
        window.noteUnexpectedExit();
        QVERIFY(window.historyText().contains(QStringLiteral("did not close properly last time")));
    }
};

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
