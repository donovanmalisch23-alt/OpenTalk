// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/ui/MainWindow.h"

#include "opentalk/logging/Logging.h"
#include "opentalk/storage/AppPaths.h"
#include "opentalk/storage/ClientSettings.h"
#include "opentalk/storage/SettingsStore.h"
#include "opentalk/ui/SettingsDialog.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QStatusBar>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace opentalk::ui {
namespace {

constexpr int kPaneCount = 4;

/// Wraps `content` with a visible heading label that is also its accessible label.
QWidget *labelledPane(const QString &heading, QWidget *content)
{
    auto *container = new QWidget;
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(6, 6, 6, 6);
    auto *label = new QLabel(heading, container);
    label->setBuddy(content);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    layout->addWidget(label);
    layout->addWidget(content, 1);
    return container;
}

} // namespace

MainWindow::MainWindow(storage::SettingsStore &settings, const storage::AppPaths &paths, QWidget *parent)
    : QMainWindow(parent)
    , m_settings(settings)
    , m_paths(paths)
{
    setWindowTitle(QStringLiteral("OpenTalk"));
    buildCentralArea();
    buildMenus();

    m_statusLabel = new QLabel(this);
    m_statusLabel->setAccessibleName(tr("Connection status"));
    statusBar()->addWidget(m_statusLabel, 1);
    setStatusText(tr("Not connected"));

    restoreLayout();
    m_channelTree->setFocus(Qt::OtherFocusReason);
}

void MainWindow::buildCentralArea()
{
    m_channelTree = new QTreeWidget;
    m_channelTree->setAccessibleName(tr("Channels"));
    m_channelTree->setHeaderHidden(true);
    m_channelTree->setRootIsDecorated(true);
    auto *placeholder = new QTreeWidgetItem(m_channelTree, {tr("Not connected to a server")});
    placeholder->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    m_channelTree->setCurrentItem(placeholder);

    m_userList = new QListWidget;
    m_userList->setAccessibleName(tr("Users"));
    auto *noUsers = new QListWidgetItem(tr("No users (not connected)"), m_userList);
    noUsers->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    m_userList->setCurrentItem(noUsers);

    m_history = new QPlainTextEdit;
    m_history->setReadOnly(true);
    // Keyboard users must be able to move through and select history text.
    m_history->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    m_history->setAccessibleName(tr("Conversation history"));
    m_history->setPlainText(tr("Welcome to OpenTalk %1, an early development build.\n"
                               "Connecting to servers, voice and text chat are not implemented yet. "
                               "This build provides the window layout, keyboard navigation and settings.")
                                .arg(QStringLiteral(OPENTALK_VERSION_STRING)));

    m_messageInput = new QLineEdit;
    m_messageInput->setAccessibleName(tr("Message"));
    m_messageInput->setAccessibleDescription(tr("Type a message and press Enter to send it to the current channel."));
    connect(m_messageInput, &QLineEdit::returnPressed, this, &MainWindow::handleMessageInput);

    auto *conversation = new QWidget;
    auto *conversationLayout = new QVBoxLayout(conversation);
    conversationLayout->setContentsMargins(0, 0, 0, 0);
    conversationLayout->addWidget(labelledPane(tr("Hi&story"), m_history), 1);
    conversationLayout->addWidget(labelledPane(tr("&Message"), m_messageInput));

    m_mainSplitter = new QSplitter(Qt::Horizontal);
    m_mainSplitter->setObjectName(QStringLiteral("mainSplitter"));
    m_mainSplitter->setChildrenCollapsible(false);
    m_mainSplitter->addWidget(labelledPane(tr("&Channels"), m_channelTree));
    m_mainSplitter->addWidget(conversation);
    m_mainSplitter->addWidget(labelledPane(tr("&Users"), m_userList));
    m_mainSplitter->setStretchFactor(0, 1);
    m_mainSplitter->setStretchFactor(1, 3);
    m_mainSplitter->setStretchFactor(2, 1);
    setCentralWidget(m_mainSplitter);

    // Tab order follows the visual order: channels, history, message, users.
    setTabOrder(m_channelTree, m_history);
    setTabOrder(m_history, m_messageInput);
    setTabOrder(m_messageInput, m_userList);
}

void MainWindow::buildMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    m_settingsAction = fileMenu->addAction(tr("&Settings..."), this, &MainWindow::openSettings);
    m_settingsAction->setShortcut(QKeySequence(Qt::Key_F4));
    fileMenu->addSeparator();
    QAction *exitAction = fileMenu->addAction(tr("E&xit"), this, &QWidget::close);
    exitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    const QList<std::pair<QString, Pane>> panes = {
        {tr("Go to &channels"), Pane::Channels},
        {tr("Go to &users"), Pane::Users},
        {tr("Go to conversation &history"), Pane::History},
        {tr("Go to &message input"), Pane::MessageInput},
    };
    int index = 1;
    for (const auto &[text, targetPane] : panes) {
        QAction *action = viewMenu->addAction(text, this, [this, pane = targetPane] { focusPane(pane); });
        action->setShortcut(QKeySequence(Qt::CTRL | static_cast<Qt::Key>(Qt::Key_0 + index)));
        m_paneActions.append(action);
        ++index;
    }
    viewMenu->addSeparator();
    QAction *next = viewMenu->addAction(tr("&Next pane"), this, &MainWindow::focusNextPane);
    next->setShortcut(QKeySequence(Qt::Key_F6));
    QAction *previous = viewMenu->addAction(tr("&Previous pane"), this, &MainWindow::focusPreviousPane);
    previous->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F6));

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About OpenTalk"), this, [this] {
        QMessageBox::about(this, tr("About OpenTalk"),
                           tr("<h3>OpenTalk %1</h3>"
                              "<p>Open-source, self-hosted voice and text communication.</p>"
                              "<p>Copyright the OpenTalk contributors. OpenTalk is free software, licensed under the "
                              "GNU General Public License version 3 only. It comes with no warranty.</p>"
                              "<p>Source code: <a href=\"https://github.com/donovanmalisch23-alt/OpenTalk\">"
                              "github.com/donovanmalisch23-alt/OpenTalk</a></p>"
                              "<p>Data folder (%2 mode): %3</p>")
                               .arg(QStringLiteral(OPENTALK_VERSION_STRING), storage::toString(m_paths.mode()),
                                    QDir::toNativeSeparators(m_paths.dataRoot()).toHtmlEscaped()));
    });
    helpMenu->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);
}

QWidget *MainWindow::paneWidget(Pane pane) const
{
    switch (pane) {
    case Pane::Channels:
        return m_channelTree;
    case Pane::Users:
        return m_userList;
    case Pane::History:
        return m_history;
    case Pane::MessageInput:
        return m_messageInput;
    }
    return nullptr;
}

void MainWindow::focusPane(Pane pane)
{
    if (QWidget *widget = paneWidget(pane)) {
        widget->setFocus(Qt::ShortcutFocusReason);
    }
}

void MainWindow::moveFocusBy(int step)
{
    // Pane order for F6 matches the visual left-to-right, top-to-bottom order.
    static const Pane order[kPaneCount] = {Pane::Channels, Pane::History, Pane::MessageInput, Pane::Users};
    int current = -1;
    QWidget *focused = QApplication::focusWidget();
    for (int i = 0; i < kPaneCount; ++i) {
        QWidget *pane = paneWidget(order[i]);
        if (focused != nullptr && (focused == pane || pane->isAncestorOf(focused))) {
            current = i;
            break;
        }
    }
    const int next = current < 0 ? (step > 0 ? 0 : kPaneCount - 1) : (current + step + kPaneCount) % kPaneCount;
    focusPane(order[next]);
}

void MainWindow::focusNextPane()
{
    moveFocusBy(1);
}

void MainWindow::focusPreviousPane()
{
    moveFocusBy(-1);
}

void MainWindow::setStatusText(const QString &text)
{
    m_statusLabel->setText(text);
    qCInfo(lcUi).noquote() << "Status:" << text;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    if (m_settings.value(storage::client::kAnnounceStatusChanges).toBool()) {
        QAccessibleAnnouncementEvent event(m_statusLabel, text);
        QAccessible::updateAccessibility(&event);
    }
#endif
}

QString MainWindow::statusText() const
{
    return m_statusLabel->text();
}

void MainWindow::handleMessageInput()
{
    if (m_messageInput->text().trimmed().isEmpty()) {
        return;
    }
    // Keep the typed text so nothing is lost; sending arrives with the protocol work.
    setStatusText(tr("Message not sent: you are not connected to a server."));
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(m_settings, m_paths, this);
    dialog.exec();
}

void MainWindow::restoreLayout()
{
    if (!m_settings.value(storage::client::kRestoreWindowLayout).toBool()) {
        resize(1000, 650);
        return;
    }
    const QByteArray geometry = m_settings.value(storage::client::kMainWindowGeometry).toByteArray();
    if (geometry.isEmpty() || !restoreGeometry(geometry)) {
        resize(1000, 650);
    }
    const QByteArray splitter = m_settings.value(storage::client::kMainSplitterState).toByteArray();
    if (!splitter.isEmpty()) {
        m_mainSplitter->restoreState(splitter);
    }
}

void MainWindow::saveLayout()
{
    m_settings.setValue(storage::client::kMainWindowGeometry, saveGeometry());
    m_settings.setValue(storage::client::kMainSplitterState, m_mainSplitter->saveState());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveLayout();
    if (!m_settings.isReadOnly()) {
        m_settings.save();
    }
    QMainWindow::closeEvent(event);
}

} // namespace opentalk::ui
