// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QList>
#include <QMainWindow>

class QAction;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QSplitter;
class QTreeWidget;

namespace opentalk::storage {
class AppPaths;
class SettingsStore;
} // namespace opentalk::storage

namespace opentalk::ui {

/// The primary window: channel tree, user list, conversation history, message input
/// and status bar. Keyboard access:
///   F6 / Shift+F6   next / previous pane
///   Ctrl+1 .. Ctrl+4 channels, users, history, message input
///   F4              settings
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    enum class Pane { Channels = 0, Users, History, MessageInput };

    MainWindow(storage::SettingsStore &settings, const storage::AppPaths &paths, QWidget *parent = nullptr);

    /// Shows `text` in the status bar and, if enabled in settings, asks the screen reader
    /// to announce it.
    void setStatusText(const QString &text);
    QString statusText() const;

    QWidget *paneWidget(Pane pane) const;
    QAction *settingsAction() const { return m_settingsAction; }
    QList<QAction *> paneActions() const { return m_paneActions; }

public slots:
    void focusPane(opentalk::ui::MainWindow::Pane pane);
    void focusNextPane();
    void focusPreviousPane();
    void openSettings();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildCentralArea();
    void buildMenus();
    void restoreLayout();
    void saveLayout();
    void moveFocusBy(int step);
    void handleMessageInput();

    storage::SettingsStore &m_settings;
    const storage::AppPaths &m_paths;

    QSplitter *m_mainSplitter = nullptr;
    QTreeWidget *m_channelTree = nullptr;
    QListWidget *m_userList = nullptr;
    QPlainTextEdit *m_history = nullptr;
    QLineEdit *m_messageInput = nullptr;
    QLabel *m_statusLabel = nullptr;
    QAction *m_settingsAction = nullptr;
    QList<QAction *> m_paneActions;
};

} // namespace opentalk::ui
