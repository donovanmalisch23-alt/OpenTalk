// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/ui/MainWindow.h"

#include "opentalk/logging/Logging.h"
#include "opentalk/storage/AppPaths.h"
#include "opentalk/storage/ClientSettings.h"
#include "opentalk/storage/SettingsStore.h"
#include "opentalk/ui/AnnouncementQueue.h"
#include "opentalk/ui/ChannelTree.h"
#include "opentalk/ui/ConnectDialog.h"
#include "opentalk/ui/SettingsDialog.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>

namespace opentalk::ui {
namespace {

constexpr int kPaneCount = 4;
constexpr int kUserIdRole = Qt::UserRole;
using Category = AnnouncementQueue::Category;
using protocol::Role;

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

QString timestamp(qint64 msSinceEpoch = 0)
{
    const QDateTime time =
        msSinceEpoch > 0 ? QDateTime::fromMSecsSinceEpoch(msSinceEpoch).toLocalTime() : QDateTime::currentDateTime();
    return time.toString(QStringLiteral("HH:mm"));
}

} // namespace

MainWindow::MainWindow(storage::SettingsStore &settings, const storage::AppPaths &paths,
                       storage::ServerProfileStore &profiles, QWidget *parent)
    : QMainWindow(parent)
    , m_settings(settings)
    , m_paths(paths)
    , m_profiles(profiles)
{
    setWindowTitle(QStringLiteral("OpenTalk"));
    buildCentralArea();
    buildMenus();

    m_statusLabel = new QLabel(this);
    m_statusLabel->setAccessibleName(tr("Connection status"));
    statusBar()->addWidget(m_statusLabel, 1);

    m_announcements = new AnnouncementQueue(
        [this](const QString &text) {
            if (m_announcementHook) {
                m_announcementHook(text);
            }
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
            QAccessibleAnnouncementEvent event(m_statusLabel, text);
            QAccessible::updateAccessibility(&event);
#endif
        },
        this);
    applyAnnouncementSettings();
    connect(&m_settings, &storage::SettingsStore::valueChanged, this, &MainWindow::applyAnnouncementSettings);

    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(0);
    connect(&m_refreshTimer, &QTimer::timeout, this, &MainWindow::refreshViews);

    m_trustPrompt = [this](const TrustQuestion &q) {
        QMessageBox box(this);
        box.setIcon(q.previousFingerprint.isEmpty() ? QMessageBox::Question : QMessageBox::Warning);
        box.setWindowTitle(q.previousFingerprint.isEmpty() ? tr("New server") : tr("Server certificate changed"));
        if (q.previousFingerprint.isEmpty()) {
            box.setText(tr("You have not connected to %1 before. Its certificate fingerprint is:\n\n%2\n\n"
                           "Trust it only if it matches what the server's administrator gave you.")
                            .arg(q.host, q.presentedFingerprint));
        } else {
            box.setText(tr("The certificate of %1 is different from last time.\n\nBefore: %2\nNow: %3\n\n"
                           "This happens when a server is reinstalled, but it can also mean someone is "
                           "intercepting your connection. Ask the server's administrator before trusting it.")
                            .arg(q.host, q.previousFingerprint, q.presentedFingerprint));
        }
        QPushButton *trust = box.addButton(tr("&Trust and connect"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        box.exec();
        return box.clickedButton() == trust;
    };
    m_errorReporter = [this](const QString &title, const QString &text) { QMessageBox::warning(this, title, text); };

    // Connection events.
    connect(&m_connection, &client::Connection::stateChanged, this, &MainWindow::updateActions);
    connect(&m_connection, &client::Connection::stateChanged, this, [this](client::ConnectionState state) {
        if (state == client::ConnectionState::Connecting && m_profile) {
            setStatusText(tr("Connecting to %1...").arg(m_profile->name));
        } else if (state == client::ConnectionState::Handshaking && m_profile) {
            setStatusText(tr("Logging in to %1...").arg(m_profile->name));
        }
    });
    connect(&m_connection, &client::Connection::connected, this, &MainWindow::onConnected);
    connect(&m_connection, &client::Connection::disconnected, this, &MainWindow::onDisconnected);
    connect(&m_connection, &client::Connection::connectionLost, this, &MainWindow::onConnectionLost);
    connect(&m_connection, &client::Connection::textReceived, this, &MainWindow::onTextReceived);
    connect(&m_connection, &client::Connection::requestFinished, this,
            [this](quint64 id, const std::optional<protocol::Error> &error, std::optional<quint32>) {
                onRequestFinished(id, error);
            });

    const client::ClientState &state = m_connection.serverState();
    connect(&state, &client::ClientState::reset, this, &MainWindow::scheduleRefresh);
    connect(&state, &client::ClientState::channelAdded, this, &MainWindow::scheduleRefresh);
    connect(&state, &client::ClientState::channelRemoved, this, &MainWindow::scheduleRefresh);
    connect(&state, &client::ClientState::userJoined, this, [this](const protocol::UserInfo &user) {
        scheduleRefresh();
        appendHistory(tr("%1 joined the server.").arg(user.name));
        m_announcements->userJoined(user.name);
    });
    connect(&state, &client::ClientState::userLeft, this, [this](const protocol::UserInfo &user) {
        scheduleRefresh();
        appendHistory(tr("%1 left the server.").arg(user.name));
        m_announcements->userLeft(user.name);
    });
    connect(&state, &client::ClientState::userMoved, this, [this](const protocol::UserInfo &user, quint32 from) {
        scheduleRefresh();
        const client::ClientState &s = m_connection.serverState();
        if (user.id == s.selfId()) {
            const QString text = tr("You are now in %1.").arg(channelPath(user.channelId));
            appendHistory(text);
            setStatusText(text);
            m_channelTree->selectChannel(user.channelId);
        } else if (user.channelId == s.currentChannelId()) {
            appendHistory(tr("%1 joined your channel.").arg(user.name));
            announce(static_cast<int>(Category::Presence), tr("%1 joined your channel").arg(user.name));
        } else if (from == s.currentChannelId()) {
            appendHistory(tr("%1 left your channel.").arg(user.name));
            announce(static_cast<int>(Category::Presence), tr("%1 left your channel").arg(user.name));
        }
    });

    m_localServerExecutable = client::LocalServer::defaultExecutable();
    connect(&m_localServer, &client::LocalServer::stateChanged, this, &MainWindow::updateActions);
    connect(&m_localServer, &client::LocalServer::started, this, &MainWindow::onLocalServerStarted);
    connect(&m_localServer, &client::LocalServer::failed, this, [this](const QString &message) {
        setStatusText(tr("Your server could not start."));
        reportError(tr("Could not host a server"), message);
    });
    connect(&m_localServer, &client::LocalServer::stopped, this,
            [this] { appendHistory(tr("Your server has stopped.")); });

    m_channelTree->showMessage(tr("Not connected to a server"));
    setStatusText(tr("Not connected"));
    updateActions();
    restoreLayout();
    m_channelTree->setFocus(Qt::OtherFocusReason);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildCentralArea()
{
    m_channelTree = new ChannelTree;
    m_channelTree->setAccessibleName(tr("Channels"));
    m_channelTree->setContextMenuPolicy(Qt::ActionsContextMenu);
    connect(m_channelTree, &ChannelTree::channelActivated, this, &MainWindow::joinChannel);
    connect(m_channelTree, &ChannelTree::userActivated, this, &MainWindow::sendPrivateMessage);
    connect(m_channelTree, &QTreeWidget::currentItemChanged, this, &MainWindow::updateActions);

    m_userList = new QListWidget;
    m_userList->setAccessibleName(tr("Users"));
    connect(m_userList, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *item) { sendPrivateMessage(item->data(kUserIdRole).toUInt()); });

    m_history = new QPlainTextEdit;
    m_history->setReadOnly(true);
    m_history->setMaximumBlockCount(5000);
    // Keyboard users must be able to move through and select history text.
    m_history->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    m_history->setAccessibleName(tr("Conversation history"));
    m_history->setPlainText(tr("Welcome to OpenTalk %1. Press Ctrl+O to connect to a server.")
                                .arg(QStringLiteral(OPENTALK_VERSION_STRING)));

    m_messageInput = new QLineEdit;
    m_messageInput->setMaxLength(static_cast<int>(protocol::kMaxTextLength));
    m_messageInput->setAccessibleName(tr("Message"));
    m_messageInput->setAccessibleDescription(tr("Type a message and press Enter to send it to your channel."));
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
    m_mainSplitter->setStretchFactor(0, 2);
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

    // Menu mnemonics avoid the pane labels' Alt+C, Alt+S, Alt+M and Alt+U.
    QMenu *serverMenu = menuBar()->addMenu(tr("Se&rver"));
    m_connectAction = serverMenu->addAction(tr("&Connect..."), this, &MainWindow::openConnectDialog);
    m_connectAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_O));
    m_disconnectAction = serverMenu->addAction(tr("&Disconnect"), this, &MainWindow::disconnectFromServer);
    m_disconnectAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    serverMenu->addSeparator();
    m_hostAction =
        serverMenu->addAction(tr("&Host a server on this computer..."), this, &MainWindow::openLocalServerDialog);
    m_stopHostingAction = serverMenu->addAction(tr("&Stop hosting"), this, &MainWindow::stopLocalServer);

    QMenu *channelMenu = menuBar()->addMenu(tr("Cha&nnel"));
    m_joinAction = channelMenu->addAction(tr("&Join selected channel"), this, [this] {
        if (const auto id = m_channelTree->currentChannelId()) {
            joinChannel(*id);
        }
    });
    m_createChannelAction = channelMenu->addAction(tr("&Create channel..."), this, &MainWindow::createChannel);
    m_createChannelAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
    m_deleteChannelAction = channelMenu->addAction(tr("&Delete channel..."), this, &MainWindow::deleteSelectedChannel);
    channelMenu->addSeparator();
    m_privateMessageAction = channelMenu->addAction(tr("Send &private message..."), this, [this] {
        std::optional<quint32> userId = m_channelTree->currentUserId();
        if (!userId && m_userList->hasFocus() && m_userList->currentItem() != nullptr) {
            userId = m_userList->currentItem()->data(kUserIdRole).toUInt();
        }
        if (userId) {
            sendPrivateMessage(*userId);
        }
    });
    m_privateMessageAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    // The same actions are the channel tree's context menu (Shift+F10 or the Menu key).
    m_channelTree->addActions({m_joinAction, m_createChannelAction, m_deleteChannelAction, m_privateMessageAction});

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

// ---------------------------------------------------------------------- navigation

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

// ------------------------------------------------------------ status and speech

void MainWindow::applyAnnouncementSettings()
{
    using namespace storage::client;
    m_announcements->setEnabled(Category::Status, m_settings.value(kAnnounceStatusChanges).toBool());
    m_announcements->setEnabled(Category::Message, m_settings.value(kAnnounceMessages).toBool());
    m_announcements->setEnabled(Category::Presence, m_settings.value(kAnnouncePresence).toBool());
}

void MainWindow::announce(int category, const QString &text)
{
    m_announcements->announce(static_cast<Category>(category), text);
}

void MainWindow::setStatusText(const QString &text)
{
    m_statusLabel->setText(text);
    qCInfo(lcUi).noquote() << "Status:" << text;
    announce(static_cast<int>(Category::Status), text);
}

QString MainWindow::statusText() const
{
    return m_statusLabel->text();
}

void MainWindow::appendHistory(const QString &line)
{
    // Plain text only: nothing a server or user sends is ever interpreted as markup.
    m_history->appendPlainText(QStringLiteral("[%1] %2").arg(timestamp(), line));
}

QString MainWindow::historyText() const
{
    return m_history->toPlainText();
}

void MainWindow::reportError(const QString &title, const QString &text)
{
    appendHistory(text);
    if (m_errorReporter) {
        m_errorReporter(title, text);
    }
}

// ---------------------------------------------------------------------- connection

QString MainWindow::serverDisplayName() const
{
    const QString announced = m_connection.serverInfo().serverName;
    return !announced.isEmpty() ? announced : (m_profile ? m_profile->name : QString());
}

QString MainWindow::userName(quint32 userId) const
{
    const auto user = m_connection.serverState().user(userId);
    return user ? user->name : tr("(someone who left)");
}

QString MainWindow::channelPath(quint32 channelId) const
{
    return m_connection.serverState().channelPath(channelId, serverDisplayName());
}

void MainWindow::openConnectDialog()
{
    ConnectDialog dialog(m_profiles, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const ConnectDialog::Choice choice = dialog.choice();
    connectToServer(choice.profile, choice.password, choice.save);
}

void MainWindow::connectToServer(const storage::ServerProfile &profile, const QString &password, bool save)
{
    m_profile = profile;
    m_password = password;
    m_saveProfile = save;
    if (save) {
        m_profile->id = m_profiles.upsert(*m_profile);
        m_profiles.save();
    }

    client::ConnectOptions options;
    options.host = profile.host;
    options.port = profile.port;
    options.trust = profile.pinnedFingerprint.isEmpty() ? security::ServerTrust::systemAuthorities()
                                                        : security::ServerTrust::pinned(profile.pinnedFingerprint);
    options.credentials.mode = profile.guest ? client::Credentials::Mode::Guest : client::Credentials::Mode::Account;
    options.credentials.name = profile.userName;
    options.credentials.password = password;
    options.session.clientName = QStringLiteral("OpenTalk");
    options.session.clientVersion = QStringLiteral(OPENTALK_VERSION_STRING);
    m_channelTree->showMessage(tr("Connecting to %1...").arg(profile.name));
    m_connection.connectToServer(options);
}

void MainWindow::disconnectFromServer()
{
    m_connection.disconnectFromServer();
}

// -------------------------------------------------------------- hosting a server

void MainWindow::openLocalServerDialog()
{
    using namespace storage::client;
    LocalServerDialog::Choice initial;
    initial.name = m_settings.value(kLocalServerName).toString();
    initial.port = static_cast<quint16>(m_settings.value(kLocalServerPort).toInt());
    initial.allowOtherComputers = m_settings.value(kLocalServerAllowOthers).toBool();
    initial.allowGuests = m_settings.value(kLocalServerAllowGuests).toBool();
    initial.adminName = m_settings.value(kLocalServerAdminName).toString();
    LocalServerDialog dialog(initial, this);
    if (dialog.exec() == QDialog::Accepted) {
        startLocalServer(dialog.choice());
    }
}

void MainWindow::startLocalServer(const LocalServerDialog::Choice &choice)
{
    if (m_localServer.state() != client::LocalServer::State::Stopped) {
        return;
    }
    using namespace storage::client;
    // Remember the choices for next time, but never the password.
    m_settings.setValue(kLocalServerName, choice.name);
    m_settings.setValue(kLocalServerPort, static_cast<int>(choice.port));
    m_settings.setValue(kLocalServerAllowOthers, choice.allowOtherComputers);
    m_settings.setValue(kLocalServerAllowGuests, choice.allowGuests);
    m_settings.setValue(kLocalServerAdminName, choice.adminName);
    if (!m_settings.isReadOnly()) {
        m_settings.save();
    }

    m_localChoice = choice;
    if (choice.adminName.isEmpty()) {
        launchLocalServer(choice);
        return;
    }
    setStatusText(tr("Preparing your server..."));
    const QString dataDir = m_paths.dataRoot() + QStringLiteral("/servers/local");
    QDir().mkpath(dataDir);
    m_localServer.createAccount(m_localServerExecutable, dataDir, choice.adminName, choice.adminPassword, true,
                                [this, choice](const QString &error) {
                                    // An existing account is fine: you are logging in to it again.
                                    if (!error.isEmpty() && !error.contains(QStringLiteral("already exists"))) {
                                        setStatusText(tr("Your server could not start."));
                                        reportError(tr("Could not host a server"),
                                                    tr("Could not create the administrator account: %1.").arg(error));
                                        return;
                                    }
                                    launchLocalServer(choice);
                                });
}

void MainWindow::launchLocalServer(const LocalServerDialog::Choice &choice)
{
    client::LocalServer::Options options;
    options.executable = m_localServerExecutable;
    options.dataDir = m_paths.dataRoot() + QStringLiteral("/servers/local");
    QDir().mkpath(options.dataDir);
    options.name = choice.name;
    options.port = choice.port;
    options.allowOtherComputers = choice.allowOtherComputers;
    options.allowGuests = choice.allowGuests;
    setStatusText(tr("Starting your server..."));
    m_localServer.start(options);
}

void MainWindow::onLocalServerStarted(quint16 port, const QString &fingerprint)
{
    if (!m_localChoice) {
        return;
    }
    const LocalServerDialog::Choice choice = *m_localChoice;
    appendHistory(tr("Your server \"%1\" is running on port %2. Its certificate fingerprint is %3.")
                      .arg(choice.name)
                      .arg(port)
                      .arg(fingerprint));
    if (choice.allowOtherComputers) {
        appendHistory(tr("Give other people your computer's address, port %1 and the fingerprint above.").arg(port));
    }
    storage::ServerProfile profile;
    profile.name = choice.name;
    profile.host = QStringLiteral("127.0.0.1");
    profile.port = port;
    profile.guest = choice.adminName.isEmpty();
    profile.userName = profile.guest ? tr("Host") : choice.adminName;
    // We started this server ourselves and read its fingerprint directly, so there is
    // nothing to confirm.
    profile.pinnedFingerprint = fingerprint;
    connectToServer(profile, choice.adminPassword, false);
}

bool MainWindow::isConnectedToLocalServer() const
{
    return m_profile && m_profile->host == QLatin1String("127.0.0.1") && m_profile->port == m_localServer.port() &&
           m_connection.state() != client::ConnectionState::Disconnected;
}

void MainWindow::stopLocalServer()
{
    if (isConnectedToLocalServer()) {
        m_connection.disconnectFromServer(); // otherwise the app would keep trying to reconnect
    }
    setStatusText(tr("Stopping your server..."));
    m_localServer.stop();
}

void MainWindow::onConnected()
{
    const client::ClientState &state = m_connection.serverState();
    setWindowTitle(tr("%1 - OpenTalk").arg(serverDisplayName()));
    refreshViews();
    m_channelTree->selectChannel(state.currentChannelId());
    const QString text = tr("Connected to %1 as %2. You are in %3.")
                             .arg(serverDisplayName(), state.selfName(), channelPath(state.currentChannelId()));
    appendHistory(text);
    setStatusText(text);
    m_channelTree->setFocus(Qt::OtherFocusReason);
}

void MainWindow::onDisconnected(const client::DisconnectInfo &info)
{
    setWindowTitle(QStringLiteral("OpenTalk"));
    m_channelTree->showMessage(tr("Not connected to a server"));
    m_userList->clear();
    m_requestDescriptions.clear();
    using Reason = client::DisconnectInfo::Reason;
    switch (info.reason) {
    case Reason::UserRequested:
        appendHistory(tr("Disconnected."));
        setStatusText(tr("Not connected"));
        return;
    case Reason::UntrustedCertificate:
        handleUntrustedCertificate(info);
        return;
    case Reason::LoginFailed:
        setStatusText(tr("Not connected"));
        reportError(tr("Could not log in"), info.protocolCode == protocol::ErrorCode::NameInUse
                                                ? tr("Could not log in: that name is already in use on this server.")
                                                : tr("Could not log in: %1.").arg(info.detail));
        return;
    case Reason::VersionMismatch:
        setStatusText(tr("Not connected"));
        reportError(tr("Cannot connect"), tr("This server uses a different version of the OpenTalk protocol."));
        return;
    case Reason::ServerClosed:
    case Reason::ProtocolError:
    case Reason::Timeout:
    case Reason::NetworkError:
        setStatusText(tr("Not connected"));
        reportError(tr("Connection problem"), tr("Disconnected: %1.").arg(info.detail));
        return;
    }
}

void MainWindow::onConnectionLost(const client::DisconnectInfo &info, int attempt, int delayMs)
{
    m_channelTree->showMessage(tr("Connection lost. Reconnecting..."));
    m_userList->clear();
    const int seconds = std::max(1, (delayMs + 500) / 1000);
    appendHistory(tr("Connection lost (%1).").arg(info.detail));
    setStatusText(tr("Connection lost. Trying again in %n second(s), attempt %1.", nullptr, seconds).arg(attempt));
}

void MainWindow::handleUntrustedCertificate(const client::DisconnectInfo &info)
{
    if (!m_profile || info.presentedFingerprint.isEmpty()) {
        setStatusText(tr("Not connected"));
        reportError(tr("Untrusted server"), tr("Could not verify the server: %1.").arg(info.detail));
        return;
    }
    const TrustQuestion question{m_profile->name, m_profile->host, info.presentedFingerprint,
                                 m_profile->pinnedFingerprint};
    if (!m_trustPrompt || !m_trustPrompt(question)) {
        appendHistory(tr("Did not connect: the server's certificate was not trusted."));
        setStatusText(tr("Not connected"));
        return;
    }
    storage::ServerProfile trusted = *m_profile;
    trusted.pinnedFingerprint = info.presentedFingerprint;
    appendHistory(tr("Trusted the certificate of %1 (%2).").arg(trusted.host, trusted.pinnedFingerprint));
    connectToServer(trusted, m_password, m_saveProfile);
}

// --------------------------------------------------------------------- the views

void MainWindow::scheduleRefresh()
{
    if (!m_refreshTimer.isActive()) {
        m_refreshTimer.start();
    }
}

void MainWindow::refreshViews()
{
    if (m_connection.state() != client::ConnectionState::Connected) {
        return;
    }
    const client::ClientState &state = m_connection.serverState();
    m_channelTree->rebuild(state, serverDisplayName());

    const quint32 selected =
        m_userList->currentItem() != nullptr ? m_userList->currentItem()->data(kUserIdRole).toUInt() : 0;
    m_userList->clear();
    for (const protocol::UserInfo &user : state.users()) {
        const QString label =
            tr("%1, in %2")
                .arg(ChannelTree::userLabel(user.name, user.id == state.selfId(), static_cast<int>(user.role)),
                     channelPath(user.channelId));
        auto *item = new QListWidgetItem(label, m_userList);
        item->setData(kUserIdRole, user.id);
        if (user.id == selected) {
            m_userList->setCurrentItem(item);
        }
    }
    if (m_userList->currentItem() == nullptr && m_userList->count() > 0) {
        m_userList->setCurrentRow(0);
    }
    updateActions();
}

void MainWindow::updateActions()
{
    const bool connected = m_connection.state() == client::ConnectionState::Connected;
    const bool active = m_connection.state() != client::ConnectionState::Disconnected;
    const Role role = m_connection.serverState().selfRole();
    const auto channelId = m_channelTree->currentChannelId();
    m_disconnectAction->setEnabled(active);
    m_hostAction->setEnabled(m_localServer.state() == client::LocalServer::State::Stopped);
    m_stopHostingAction->setEnabled(m_localServer.state() == client::LocalServer::State::Running);
    m_joinAction->setEnabled(connected && channelId.has_value());
    m_createChannelAction->setEnabled(connected && role != Role::Guest);
    m_deleteChannelAction->setEnabled(connected && role == Role::Admin && channelId &&
                                      *channelId != protocol::kRootChannelId);
    m_privateMessageAction->setEnabled(connected);
}

// ------------------------------------------------------------------- requests

void MainWindow::joinChannel(quint32 channelId)
{
    const client::ClientState &state = m_connection.serverState();
    const auto channel = state.channel(channelId);
    if (m_connection.state() != client::ConnectionState::Connected || !channel ||
        channelId == state.currentChannelId()) {
        return;
    }
    QString password;
    if (channel->hasPassword && state.selfRole() != Role::Admin) {
        bool ok = false;
        password =
            QInputDialog::getText(this, tr("Channel password"), tr("Password for %1:").arg(channelPath(channelId)),
                                  QLineEdit::Password, QString(), &ok);
        if (!ok) {
            return;
        }
    }
    const quint64 id = m_connection.joinChannel(channelId, password);
    m_requestDescriptions.insert(id, tr("Could not join %1").arg(channelPath(channelId)));
}

void MainWindow::createChannel()
{
    const client::ClientState &state = m_connection.serverState();
    if (m_connection.state() != client::ConnectionState::Connected || state.selfRole() == Role::Guest) {
        return;
    }
    // Create inside the selected channel when it can have sub-channels, else at the top.
    quint32 parentId = protocol::kRootChannelId;
    if (const auto selected = m_channelTree->currentChannelId()) {
        if (const auto channel = state.channel(*selected); channel && channel->persistent) {
            parentId = *selected;
        }
    }
    CreateChannelDialog dialog(channelPath(parentId), state.selfRole() == Role::Admin, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    protocol::CreateChannel request;
    request.parentId = parentId;
    request.name = dialog.channelName();
    request.topic = dialog.topic();
    request.password = dialog.password();
    request.maxUsers = dialog.maxUsers();
    request.persistent = dialog.persistent();
    const quint64 id = m_connection.createChannel(request);
    m_requestDescriptions.insert(id, tr("Could not create %1").arg(request.name));
}

void MainWindow::deleteSelectedChannel()
{
    const auto channelId = m_channelTree->currentChannelId();
    if (!channelId || *channelId == protocol::kRootChannelId) {
        return;
    }
    const QString path = channelPath(*channelId);
    if (QMessageBox::question(this, tr("Delete channel"),
                              tr("Delete %1? Anyone inside is moved to the top of the server.").arg(path)) !=
        QMessageBox::Yes) {
        return;
    }
    const quint64 id = m_connection.deleteChannel(*channelId);
    m_requestDescriptions.insert(id, tr("Could not delete %1").arg(path));
}

void MainWindow::sendPrivateMessage(quint32 userId)
{
    const client::ClientState &state = m_connection.serverState();
    if (m_connection.state() != client::ConnectionState::Connected || userId == state.selfId() || !state.user(userId)) {
        return;
    }
    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("Private message"), tr("Message to %1:").arg(userName(userId)),
                                               QLineEdit::Normal, QString(), &ok);
    if (!ok || text.trimmed().isEmpty()) {
        return;
    }
    const quint64 id = m_connection.sendText(protocol::TextTarget::User, userId, text);
    m_requestDescriptions.insert(id, tr("Could not send the message to %1").arg(userName(userId)));
}

void MainWindow::handleMessageInput()
{
    const QString text = m_messageInput->text();
    if (text.trimmed().isEmpty()) {
        return;
    }
    if (m_connection.state() != client::ConnectionState::Connected) {
        // Keep the typed text so nothing is lost.
        setStatusText(tr("Message not sent: you are not connected to a server."));
        return;
    }
    const quint32 channelId = m_connection.serverState().currentChannelId();
    const quint64 id = m_connection.sendText(protocol::TextTarget::Channel, channelId, text);
    if (id == 0) {
        setStatusText(tr("Message not sent: it contains characters that cannot be sent."));
        return;
    }
    m_requestDescriptions.insert(id, tr("Message not sent"));
    m_messageInput->clear();
}

void MainWindow::onRequestFinished(quint64 requestId, const std::optional<protocol::Error> &error)
{
    const QString description = m_requestDescriptions.take(requestId);
    if (!error) {
        return;
    }
    const QString text =
        tr("%1: %2").arg(description.isEmpty() ? tr("Request failed") : description, friendlyError(*error));
    appendHistory(text);
    setStatusText(text);
}

QString MainWindow::friendlyError(const protocol::Error &error)
{
    using protocol::ErrorCode;
    switch (error.code) {
    case ErrorCode::WrongChannelPassword:
        return tr("wrong password.");
    case ErrorCode::ChannelFull:
        return tr("the channel is full.");
    case ErrorCode::NotAuthorized:
        return tr("you are not allowed to do that (%1).").arg(error.detail);
    case ErrorCode::NameInUse:
        return tr("that name is already taken.");
    case ErrorCode::RateLimited:
        return tr("you are sending messages too quickly.");
    case ErrorCode::NotFound:
        return tr("that channel or user no longer exists.");
    case ErrorCode::Timeout:
        return tr("the connection was lost.");
    default:
        return error.detail.isEmpty() ? protocol::toString(error.code) : error.detail + QLatin1Char('.');
    }
}

void MainWindow::onTextReceived(const protocol::TextMessage &message)
{
    const client::ClientState &state = m_connection.serverState();
    const bool fromMe = message.fromUserId == state.selfId();
    const QString sender = userName(message.fromUserId);
    QString line;
    QString spoken;
    switch (message.target) {
    case protocol::TextTarget::Channel:
        line = tr("%1: %2").arg(sender, message.text);
        spoken = line;
        break;
    case protocol::TextTarget::User:
        line = fromMe ? tr("Private to %1: %2").arg(userName(message.targetId), message.text)
                      : tr("Private from %1: %2").arg(sender, message.text);
        spoken = line;
        break;
    case protocol::TextTarget::Server:
        line = tr("Server message from %1: %2").arg(sender, message.text);
        spoken = line;
        break;
    }
    m_history->appendPlainText(QStringLiteral("[%1] %2").arg(timestamp(message.timestampMs), line));
    if (!fromMe) {
        announce(static_cast<int>(Category::Message), spoken);
    }
}

// ------------------------------------------------------------------------ misc

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
    m_connection.disconnectFromServer();
    m_localServer.stop(); // finishes in the background; the destructor waits briefly
    saveLayout();
    if (!m_settings.isReadOnly()) {
        m_settings.save();
    }
    QMainWindow::closeEvent(event);
}

} // namespace opentalk::ui
