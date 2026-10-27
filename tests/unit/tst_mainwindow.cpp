// SPDX-License-Identifier: GPL-3.0-only
// Automated accessibility and keyboard checks for the main window and settings dialog.
// These complement, and do not replace, manual testing with NVDA (docs/accessibility/plan.md).

#include "opentalk/storage/AppPaths.h"
#include "opentalk/storage/ClientSettings.h"
#include "opentalk/storage/ServerProfiles.h"
#include "opentalk/storage/SettingsStore.h"
#include "opentalk/ui/MainWindow.h"
#include "opentalk/ui/SettingsDialog.h"

#include <QAbstractSpinBox>
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>

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
        ui::MainWindow window(*m_settings, *m_paths, *m_profiles);
        window.show();
        const QStringList problems = unnamedFocusableWidgets(&window);
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral(", "))));
    }

    void paneShortcutsAreAssignedAndWork()
    {
        ui::MainWindow window(*m_settings, *m_paths, *m_profiles);
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
        ui::MainWindow window(*m_settings, *m_paths, *m_profiles);
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
        ui::MainWindow window(*m_settings, *m_paths, *m_profiles);
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
            ui::MainWindow window(*m_settings, *m_paths, *m_profiles);
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
        auto *combo = dialog.findChild<QComboBox *>();
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
};

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
