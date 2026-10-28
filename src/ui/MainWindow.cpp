// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/ui/MainWindow.h"

#include "opentalk/audio/MediaFile.h"
#include "opentalk/logging/Logging.h"
#include "opentalk/plugins/PluginHost.h"
#include "opentalk/plugins/PluginRegistry.h"
#include "opentalk/storage/AppPaths.h"
#include "opentalk/storage/ClientSettings.h"
#include "opentalk/storage/SettingsStore.h"
#include "opentalk/ui/AdminDialog.h"
#include "opentalk/ui/AnnouncementQueue.h"
#include "opentalk/ui/AudioController.h"
#include "opentalk/ui/AudioDialogs.h"
#include "opentalk/ui/ChannelTree.h"
#include "opentalk/ui/ConnectDialog.h"
#include "opentalk/ui/ModerationDialogs.h"
#include "opentalk/ui/PluginDialogs.h"
#include "opentalk/ui/SettingsDialog.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
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
using moderation::Capability;
using moderation::SanctionType;
using protocol::Role;

QAction *separator(QObject *parent)
{
    auto *action = new QAction(parent);
    action->setSeparator(true);
    return action;
}

/// "you were kicked: spam" -> "You were kicked: spam".
QString sentence(QString text)
{
    if (!text.isEmpty()) {
        text[0] = text[0].toUpper();
    }
    return text;
}

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

/// "3:07", or "1:02:03" for an hour or more.
QString formatDuration(qint64 ms)
{
    const qint64 seconds = std::max<qint64>(0, ms) / 1000;
    if (seconds >= 3600) {
        return QStringLiteral("%1:%2:%3")
            .arg(seconds / 3600)
            .arg(seconds / 60 % 60, 2, 10, QLatin1Char('0'))
            .arg(seconds % 60, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

MainWindow::MainWindow(storage::SettingsStore &settings, const storage::AppPaths &paths,
                       storage::ServerProfileStore &profiles, std::unique_ptr<audio::AudioBackend> audioBackend,
                       QWidget *parent)
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
    m_voiceLabel = new QLabel(this);
    m_voiceLabel->setAccessibleName(tr("Voice status"));
    statusBar()->addPermanentWidget(m_voiceLabel);

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
    connect(&m_settings, &storage::SettingsStore::valueChanged, this, [this](const QString &key) {
        if (key == storage::client::kMuteIncomingMedia || key == storage::client::kMuteIncomingSystemAudio) {
            updateMediaActions();
        }
    });

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
    connect(&state, &client::ClientState::channelUpdated, this, [this](const protocol::ChannelInfo &channel) {
        scheduleRefresh();
        if (channel.id == m_connection.serverState().currentChannelId()) {
            appendHistory(channel.topic.isEmpty()
                              ? tr("Your channel was changed: it is now %1.").arg(channelPath(channel.id))
                              : tr("Your channel was changed: it is now %1, topic: %2.")
                                    .arg(channelPath(channel.id), channel.topic));
        }
    });
    connect(&state, &client::ClientState::userUpdated, this, &MainWindow::onUserUpdated);
    connect(&state, &client::ClientState::rolesChanged, this, &MainWindow::scheduleRefresh);
    connect(&state, &client::ClientState::capabilitiesChanged, this, &MainWindow::updateActions);
    connect(&state, &client::ClientState::sanctionNotice, this, &MainWindow::onSanctionNotice);
    // Which moderation actions apply depends on the focused list and its selection.
    connect(qApp, &QApplication::focusChanged, this, &MainWindow::updateActions);
    connect(&state, &client::ClientState::userJoined, this, [this](const protocol::UserInfo &user) {
        scheduleRefresh();
        appendHistory(tr("%1 joined the server.").arg(user.name));
        m_announcements->userJoined(user.name);
        playSound(audio::EventSound::UserJoinedServer);
    });
    connect(&state, &client::ClientState::userLeft, this, [this](const protocol::UserInfo &user) {
        scheduleRefresh();
        appendHistory(tr("%1 left the server.").arg(user.name));
        m_announcements->userLeft(user.name);
        playSound(audio::EventSound::UserLeftServer);
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
            playSound(audio::EventSound::UserJoinedChannel);
        } else if (from == s.currentChannelId()) {
            appendHistory(tr("%1 left your channel.").arg(user.name));
            announce(static_cast<int>(Category::Presence), tr("%1 left your channel").arg(user.name));
            playSound(audio::EventSound::UserLeftChannel);
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

    buildAudio(std::move(audioBackend));
    buildPlugins();

    m_channelTree->showMessage(tr("Not connected to a server"));
    setStatusText(tr("Not connected"));
    updateActions();
    restoreLayout();
    m_channelTree->setFocus(Qt::OtherFocusReason);
}

MainWindow::~MainWindow()
{
    // Members are destroyed in reverse order, and the connection still emits signals
    // while it shuts down; none of them may reach this half-destroyed window.
    QObject::disconnect(&m_connection, nullptr, this, nullptr);
    QObject::disconnect(&m_connection.serverState(), nullptr, this, nullptr);
    QObject::disconnect(&m_localServer, nullptr, this, nullptr);
    QObject::disconnect(qApp, nullptr, this, nullptr);
    if (m_audio != nullptr) {
        QObject::disconnect(m_audio, nullptr, this, nullptr);
    }
    if (m_pluginHost != nullptr) {
        QObject::disconnect(m_pluginHost, nullptr, this, nullptr);
        delete m_pluginHost; // before the connection it uses
        m_pluginHost = nullptr;
    }
}

void MainWindow::buildPlugins()
{
    m_pluginRegistry = std::make_unique<plugins::PluginRegistry>(m_paths.dataRoot() + QStringLiteral("/plugins"),
                                                                 m_paths.configDir() + QStringLiteral("/plugins.json"));
    m_pluginRegistry->load();
    m_pluginHost = new plugins::PluginHost(
        *m_pluginRegistry, m_connection, m_paths.logDir() + QStringLiteral("/plugins"), plugins::PluginHost::Limits{});
    connect(m_pluginHost, &plugins::PluginHost::commandsChanged, this, &MainWindow::rebuildPluginsMenu);
    connect(m_pluginHost, &plugins::PluginHost::notification, this,
            [this](const QString &plugin, const QString &text, bool speak) {
                appendHistory(tr("%1: %2").arg(plugin, text));
                if (speak) {
                    announce(static_cast<int>(Category::Message), text);
                }
            });
    connect(m_pluginHost, &plugins::PluginHost::problem, this, [this](const QString &, const QString &text) {
        appendHistory(text);
        setStatusText(text);
    });
    rebuildPluginsMenu();
}

void MainWindow::startPlugins()
{
    m_pluginHost->startEnabled();
}

void MainWindow::rebuildPluginsMenu()
{
    if (m_pluginsMenu == nullptr) {
        return;
    }
    m_pluginsMenu->clear();
    QAction *manage = m_pluginsMenu->addAction(tr("&Manage plugins..."), this, &MainWindow::openPluginManager);
    manage->setObjectName(QStringLiteral("managePluginsAction"));
    const QList<plugins::PluginCommand> commands = m_pluginHost->commands();
    if (!commands.isEmpty()) {
        m_pluginsMenu->addSeparator();
    }
    for (const plugins::PluginCommand &command : commands) {
        // "&" in a plugin's title must not become a mnemonic.
        QString title = command.title;
        title.replace(QLatin1Char('&'), QStringLiteral("&&"));
        QString plugin = command.pluginName;
        plugin.replace(QLatin1Char('&'), QStringLiteral("&&"));
        m_pluginsMenu->addAction(tr("%1 (%2)").arg(title, plugin), this,
                                 [this, command] { m_pluginHost->runCommand(command.pluginId, command.id); });
    }
}

void MainWindow::openPluginManager()
{
    PluginManagerDialog dialog(*m_pluginRegistry, *m_pluginHost, this);
    connect(&dialog, &PluginManagerDialog::statusChanged, this, &MainWindow::setStatusText);
    dialog.exec();
}

void MainWindow::buildAudio(std::unique_ptr<audio::AudioBackend> backend)
{
    if (!backend) {
        backend = audio::createSystemAudioBackend();
    }
    m_audio = new AudioController(m_settings, m_paths, m_connection, std::move(backend), this);
    connect(m_audio, &AudioController::transmittingChanged, this, &MainWindow::updateVoiceStatus);
    connect(m_audio, &AudioController::activityChanged, this, &MainWindow::onUserActivityChanged);
    connect(m_audio, &AudioController::mediaFinished, this, [this](const QString &title) {
        const QString text = tr("Finished streaming %1.").arg(title);
        appendHistory(text);
        setStatusText(text);
        updateVoiceStatus();
        updateMediaActions();
    });
    connect(m_audio, &AudioController::systemAudioStopped, this, [this](const QString &reason) {
        const QString text = tr("Sharing computer audio stopped: %1.").arg(reason);
        appendHistory(text);
        setStatusText(text);
        updateVoiceStatus();
        updateMediaActions();
    });
    connect(m_audio, &AudioController::streamingInterrupted, this, [this] {
        appendHistory(tr("Streaming stopped because the connection closed."));
        updateVoiceStatus();
        updateMediaActions();
    });
    connect(m_audio, &AudioController::deviceProblem, this, [this](const QString &message) {
        if (message == m_lastDeviceProblem) {
            return; // the engine keeps retrying; say it once
        }
        m_lastDeviceProblem = message;
        const QString text =
            tr("Audio problem: %1. OpenTalk keeps trying; check the devices in Settings, Audio.").arg(message);
        appendHistory(text);
        setStatusText(text);
    });
    connect(m_audio, &AudioController::devicesStarted, this, [this] {
        if (!m_lastDeviceProblem.isEmpty()) {
            m_lastDeviceProblem.clear();
            appendHistory(tr("Audio devices are working."));
        }
    });
    m_voicePathTimer.setSingleShot(true);
    m_voicePathTimer.setInterval(5000);
    connect(&m_voicePathTimer, &QTimer::timeout, this, [this] {
        if (m_connection.isVoiceReady() && !m_connection.isVoiceUsingUdp() && m_connection.options().allowUdp) {
            appendHistory(tr("Voice is using the main connection because UDP does not get through to this server. "
                             "It works, with a little more delay."));
        }
    });
    connect(&m_connection, &client::Connection::voiceReady, this, [this] { m_voicePathTimer.start(); });
    connect(&m_connection, &client::Connection::voicePathChanged, this, [this](bool usingUdp) {
        if (!usingUdp && m_connection.state() == client::ConnectionState::Connected && !m_voicePathTimer.isActive()) {
            appendHistory(tr("Voice switched to the main connection because UDP stopped getting through."));
        }
    });
    updateVoiceStatus();
}

void MainWindow::onUserActivityChanged(quint32 userId, int streams)
{
    const int started = streams & ~m_channelTree->userActivity(userId);
    m_channelTree->setUserActivity(userId, streams);
    if (started != 0 && m_settings.value(storage::client::kAnnounceTalking).toBool()) {
        announce(static_cast<int>(Category::Presence),
                 tr("%1 %2").arg(userName(userId), ChannelTree::activityText(started)));
    }
}

void MainWindow::streamFile()
{
    using storage::client::kMediaFolder;
    const QString path = m_mediaFileChooser ? m_mediaFileChooser()
                                            : QFileDialog::getOpenFileName(this, tr("Stream a File into the Channel"),
                                                                           m_settings.value(kMediaFolder).toString(),
                                                                           audio::MediaFile::fileFilter());
    if (path.isEmpty()) {
        return;
    }
    m_settings.setValue(kMediaFolder, QFileInfo(path).absolutePath());
    QString error;
    if (!m_audio->startMedia(path, &error)) {
        reportError(tr("Cannot stream the file"),
                    tr("%1 cannot be streamed: %2.").arg(QFileInfo(path).fileName(), error));
        return;
    }
    const audio::MediaStatus status = m_audio->mediaStatus();
    QString text = tr("Streaming %1").arg(status.title);
    if (status.durationMs >= 0) {
        text = tr("Streaming %1, %2 long").arg(status.title, formatDuration(status.durationMs));
    }
    appendHistory(text + QLatin1Char('.'));
    setStatusText(text + QLatin1Char('.'));
    if (!m_connection.serverState().can(Capability::MediaTransmit)) {
        appendHistory(tr("You may stream only in some channels. If nobody hears it, this channel does not allow it."));
    }
    updateVoiceStatus();
    updateMediaActions();
}

void MainWindow::toggleMediaPaused()
{
    const audio::MediaStatus status = m_audio->mediaStatus();
    if (!status.active) {
        return;
    }
    m_audio->setMediaPaused(!status.paused);
    setStatusText(status.paused ? tr("Streaming %1 again.").arg(status.title)
                                : tr("Paused %1 at %2.").arg(status.title, formatDuration(status.positionMs)));
    updateVoiceStatus();
    updateMediaActions();
}

void MainWindow::skipMedia(qint64 deltaMs)
{
    const audio::MediaStatus status = m_audio->mediaStatus();
    if (!status.active) {
        return;
    }
    m_audio->skipMedia(deltaMs);
    qint64 target = std::max<qint64>(0, status.positionMs + deltaMs);
    if (status.durationMs >= 0) {
        target = std::min(target, status.durationMs);
    }
    setStatusText(status.durationMs >= 0 ? tr("%1 of %2").arg(formatDuration(target), formatDuration(status.durationMs))
                                         : formatDuration(target));
}

void MainWindow::stopMedia()
{
    const audio::MediaStatus status = m_audio->mediaStatus();
    if (!status.active) {
        return;
    }
    m_audio->stopMedia();
    const QString text = tr("Stopped streaming %1.").arg(status.title);
    appendHistory(text);
    setStatusText(text);
    updateVoiceStatus();
    updateMediaActions();
}

void MainWindow::sayMediaStatus()
{
    QStringList parts;
    const audio::MediaStatus status = m_audio->mediaStatus();
    if (status.active) {
        QString text = status.paused ? tr("Paused %1").arg(status.title) : tr("Streaming %1").arg(status.title);
        text += status.durationMs >= 0
                    ? tr(", %1 of %2").arg(formatDuration(status.positionMs), formatDuration(status.durationMs))
                    : tr(", %1").arg(formatDuration(status.positionMs));
        parts << text;
    }
    if (m_audio->isSharingSystemAudio()) {
        parts << tr("Sharing computer audio");
    }
    setStatusText(parts.isEmpty() ? tr("Not streaming anything.")
                                  : parts.join(QStringLiteral(". ")) + QLatin1Char('.'));
}

void MainWindow::toggleSystemAudioSharing()
{
    if (m_audio->isSharingSystemAudio()) {
        m_audio->stopSystemAudio();
        appendHistory(tr("Stopped sharing computer audio."));
        setStatusText(tr("Stopped sharing computer audio."));
    } else {
        QString error;
        bool excludesOwnSound = false;
        if (!m_audio->startSystemAudio(&error, &excludesOwnSound)) {
            reportError(tr("Cannot share computer audio"), tr("Computer audio cannot be shared: %1.").arg(error));
        } else if (excludesOwnSound) {
            appendHistory(tr("Sharing computer audio. Everything this computer plays is sent to your channel, except "
                             "OpenTalk itself."));
            setStatusText(tr("Sharing computer audio."));
        } else {
            // The explicit warning the threat model asks for when OpenTalk's own sound
            // cannot be left out.
            const QString warning =
                tr("Sharing computer audio. This version of Windows cannot leave OpenTalk's own sound out, so "
                   "OpenTalk removes it, but some of other people's voices and your screen reader may still be "
                   "heard. To avoid that, send OpenTalk's sound to a different device in Settings, Audio (for "
                   "example headphones), or use Windows 11.");
            appendHistory(warning);
            setStatusText(warning);
        }
    }
    updateVoiceStatus();
    updateMediaActions();
}

void MainWindow::updateMediaActions()
{
    if (m_streamFileAction == nullptr || m_audio == nullptr) {
        return; // still building the window
    }
    const bool connected = m_connection.state() == client::ConnectionState::Connected;
    const bool allowed = connected && m_connection.serverState().canSomewhere(Capability::MediaTransmit);
    const audio::MediaStatus status = m_audio->mediaStatus();
    m_streamFileAction->setEnabled(allowed);
    m_pauseMediaAction->setEnabled(status.active);
    m_pauseMediaAction->setText(status.paused ? tr("&Resume streaming") : tr("&Pause streaming"));
    m_skipBackAction->setEnabled(status.active);
    m_skipForwardAction->setEnabled(status.active);
    m_stopMediaAction->setEnabled(status.active);
    const bool sharing = m_audio->isSharingSystemAudio();
    m_shareSystemAudioAction->setVisible(m_audio->canShareSystemAudio());
    m_shareSystemAudioAction->setEnabled(allowed || sharing);
    m_shareSystemAudioAction->setChecked(sharing);
    m_muteIncomingMediaAction->setChecked(m_settings.value(storage::client::kMuteIncomingMedia).toBool() &&
                                          m_settings.value(storage::client::kMuteIncomingSystemAudio).toBool());
}

void MainWindow::playSound(audio::EventSound sound)
{
    if (m_audio != nullptr) {
        m_audio->playSound(sound);
    }
}

QString MainWindow::voiceStatusText() const
{
    return m_voiceLabel->text();
}

void MainWindow::updateVoiceStatus()
{
    QStringList parts;
    if (m_audio->isDeafened()) {
        parts << tr("Sound muted");
    }
    if (m_audio->isMicrophoneMuted()) {
        parts << tr("Microphone muted");
    } else if (m_audio->isTransmitting()) {
        parts << tr("Talking");
    } else if (m_audio->isTransmitLatched()) {
        parts << tr("Talk on");
    }
    const audio::MediaStatus media = m_audio->mediaStatus();
    if (media.active) {
        parts << (media.paused ? tr("Paused %1").arg(media.title) : tr("Streaming %1").arg(media.title));
    }
    if (m_audio->isSharingSystemAudio()) {
        parts << tr("Sharing computer audio");
    }
    m_voiceLabel->setText(parts.join(QStringLiteral(", ")));
    if (m_transmitAction != nullptr) {
        m_transmitAction->setChecked(m_audio->isTransmitLatched());
        m_muteAction->setChecked(m_audio->isMicrophoneMuted());
        m_deafenAction->setChecked(m_audio->isDeafened());
    }
}

void MainWindow::toggleTransmit()
{
    const bool on = !m_audio->isTransmitLatched();
    m_audio->setTransmitLatched(on);
    updateVoiceStatus();
    announce(static_cast<int>(Category::Status), on ? tr("Talking on") : tr("Talking off"));
}

void MainWindow::toggleMicrophoneMute()
{
    const bool muted = !m_audio->isMicrophoneMuted();
    m_audio->setMicrophoneMuted(muted);
    updateVoiceStatus();
    announce(static_cast<int>(Category::Status), muted ? tr("Microphone muted") : tr("Microphone on"));
}

void MainWindow::toggleDeafen()
{
    const bool deafened = !m_audio->isDeafened();
    m_audio->setDeafened(deafened);
    updateVoiceStatus();
    announce(static_cast<int>(Category::Status), deafened ? tr("Sound muted") : tr("Sound on"));
}

std::optional<quint32> MainWindow::selectedUserId() const
{
    if (m_userList->hasFocus() && m_userList->currentItem() != nullptr) {
        return m_userList->currentItem()->data(kUserIdRole).toUInt();
    }
    return m_channelTree->currentUserId();
}

void MainWindow::openUserVolume()
{
    const std::optional<quint32> userId = selectedUserId();
    if (!userId || *userId == m_connection.serverState().selfId() || !m_connection.serverState().user(*userId)) {
        return;
    }
    const int before = m_audio->userVolume(*userId);
    UserVolumeDialog dialog(userName(*userId), before, this);
    connect(&dialog, &UserVolumeDialog::percentChanged, this,
            [this, id = *userId](int percent) { m_audio->setUserVolume(id, percent); });
    if (dialog.exec() != QDialog::Accepted) {
        m_audio->setUserVolume(*userId, before);
        return;
    }
    m_audio->setUserVolume(*userId, dialog.percent());
    if (!m_settings.isReadOnly()) {
        m_settings.save();
    }
    setStatusText(tr("Volume for %1 is %2%.").arg(userName(*userId)).arg(dialog.percent()));
}

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
    connect(m_userList, &QListWidget::currentItemChanged, this, &MainWindow::updateActions);
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
    serverMenu->addSeparator();
    m_broadcastAction = serverMenu->addAction(tr("Send a message to &everyone..."), this, &MainWindow::sendBroadcast);

    QMenu *channelMenu = menuBar()->addMenu(tr("Cha&nnel"));
    m_joinAction = channelMenu->addAction(tr("&Join selected channel"), this, [this] {
        if (const auto id = m_channelTree->currentChannelId()) {
            joinChannel(*id);
        }
    });
    m_createChannelAction = channelMenu->addAction(tr("&Create channel..."), this, &MainWindow::createChannel);
    m_createChannelAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
    m_editChannelAction = channelMenu->addAction(tr("&Edit channel..."), this, &MainWindow::editSelectedChannel);
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
    m_userVolumeAction = channelMenu->addAction(tr("&Volume for selected user..."), this, &MainWindow::openUserVolume);
    m_userVolumeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));

    // Mnemonics avoid the pane labels (Alt+C, S, M, U) and the other menus.
    QMenu *moderationMenu = menuBar()->addMenu(tr("M&oderation"));
    const auto moderate = [this](SanctionType type) { return [this, type] { moderateSelectedUser(type); }; };
    m_warnAction = moderationMenu->addAction(tr("&Warn selected user..."), this, moderate(SanctionType::Warning));
    m_muteUserAction = moderationMenu->addAction(tr("M&ute selected user..."), this, moderate(SanctionType::Mute));
    m_kickAction = moderationMenu->addAction(tr("&Kick selected user..."), this, moderate(SanctionType::Kick));
    m_banAction = moderationMenu->addAction(tr("&Ban selected user..."), this, moderate(SanctionType::Ban));
    m_moveUserAction = moderationMenu->addAction(tr("Mo&ve selected user..."), this, &MainWindow::moveSelectedUser);
    moderationMenu->addSeparator();
    m_reportAction = moderationMenu->addAction(tr("&Report selected user..."), this, &MainWindow::reportSelectedUser);
    m_mySanctionsAction =
        moderationMenu->addAction(tr("My &sanctions and appeals..."), this, &MainWindow::openMySanctions);
    moderationMenu->addSeparator();
    m_adminAction = moderationMenu->addAction(tr("&Administration..."), this, &MainWindow::openAdministration);
    m_adminAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));
    const QList<std::pair<QAction *, const char *>> named = {
        {m_editChannelAction, "editChannelAction"},
        {m_broadcastAction, "broadcastAction"},
        {m_warnAction, "warnAction"},
        {m_muteUserAction, "muteUserAction"},
        {m_kickAction, "kickAction"},
        {m_banAction, "banAction"},
        {m_moveUserAction, "moveUserAction"},
        {m_reportAction, "reportAction"},
        {m_mySanctionsAction, "mySanctionsAction"},
        {m_adminAction, "adminAction"},
        {m_deleteChannelAction, "deleteChannelAction"},
        {m_createChannelAction, "createChannelAction"},
    };
    for (const auto &[action, name] : named) {
        action->setObjectName(QLatin1String(name));
    }

    // The same actions are the channel tree's context menu (Shift+F10 or the Menu key).
    const QList<QAction *> userActions = {m_warnAction, m_muteUserAction, m_kickAction,
                                          m_banAction,  m_moveUserAction, m_reportAction};
    m_channelTree->addActions({m_joinAction, m_createChannelAction, m_editChannelAction, m_deleteChannelAction,
                               separator(this), m_privateMessageAction, m_userVolumeAction, separator(this)});
    m_channelTree->addActions(userActions);
    m_userList->setContextMenuPolicy(Qt::ActionsContextMenu);
    m_userList->addActions({m_privateMessageAction, m_userVolumeAction, separator(this)});
    m_userList->addActions(userActions);

    QMenu *audioMenu = menuBar()->addMenu(tr("&Audio"));
    connect(audioMenu, &QMenu::aboutToShow, this, &MainWindow::updateMediaActions);
    m_transmitAction =
        audioMenu->addAction(tr("&Talk (stay on until pressed again)"), this, &MainWindow::toggleTransmit);
    m_transmitAction->setCheckable(true);
    m_transmitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    m_muteAction = audioMenu->addAction(tr("&Mute microphone"), this, &MainWindow::toggleMicrophoneMute);
    m_muteAction->setCheckable(true);
    m_muteAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    m_deafenAction = audioMenu->addAction(tr("Mute &everything I hear"), this, &MainWindow::toggleDeafen);
    m_deafenAction->setCheckable(true);
    m_deafenAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    audioMenu->addSeparator();
    m_streamFileAction = audioMenu->addAction(tr("Stream a &file into the channel..."), this, &MainWindow::streamFile);
    m_streamFileAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    m_pauseMediaAction = audioMenu->addAction(tr("&Pause streaming"), this, &MainWindow::toggleMediaPaused);
    m_pauseMediaAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K));
    m_skipBackAction = audioMenu->addAction(tr("&Back 10 seconds"), this, [this] { skipMedia(-10'000); });
    m_skipBackAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_J));
    m_skipForwardAction = audioMenu->addAction(tr("For&ward 10 seconds"), this, [this] { skipMedia(10'000); });
    m_skipForwardAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L));
    m_stopMediaAction = audioMenu->addAction(tr("St&op streaming"), this, &MainWindow::stopMedia);
    m_stopMediaAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X));
    m_mediaStatusAction = audioMenu->addAction(tr("What is stream&ing?"), this, &MainWindow::sayMediaStatus);
    m_mediaStatusAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
    m_shareSystemAudioAction =
        audioMenu->addAction(tr("Share &computer audio"), this, &MainWindow::toggleSystemAudioSharing);
    m_shareSystemAudioAction->setCheckable(true);
    m_shareSystemAudioAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    m_muteIncomingMediaAction = audioMenu->addAction(tr("M&ute files and computer audio from others"), this, [this] {
        const bool mute = m_muteIncomingMediaAction->isChecked();
        m_settings.setValue(storage::client::kMuteIncomingMedia, mute);
        m_settings.setValue(storage::client::kMuteIncomingSystemAudio, mute);
        setStatusText(mute ? tr("Files and computer audio from others muted.")
                           : tr("Files and computer audio from others on."));
    });
    m_muteIncomingMediaAction->setCheckable(true);
    for (const auto &[action, name] :
         QList<std::pair<QAction *, const char *>>{{m_streamFileAction, "streamFileAction"},
                                                   {m_pauseMediaAction, "pauseMediaAction"},
                                                   {m_skipBackAction, "skipBackAction"},
                                                   {m_skipForwardAction, "skipForwardAction"},
                                                   {m_stopMediaAction, "stopMediaAction"},
                                                   {m_mediaStatusAction, "mediaStatusAction"},
                                                   {m_shareSystemAudioAction, "shareSystemAudioAction"},
                                                   {m_muteIncomingMediaAction, "muteIncomingMediaAction"}}) {
        action->setObjectName(QLatin1String(name));
    }
    audioMenu->addSeparator();
    audioMenu->addAction(tr("Audio &settings..."), this, &MainWindow::openAudioSettings);

    m_pluginsMenu = menuBar()->addMenu(tr("&Plugins"));

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
    playSound(audio::EventSound::Error);
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
    if (userId == 0) {
        return tr("the server");
    }
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
    options.allowUdp = m_settings.value(storage::client::kUseUdp).toBool();
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
    // The person hosting owns the server. If it already has an owner (hosted before under
    // another name), the new account becomes an administrator instead.
    const auto finish = [this, choice](const QString &error) {
        // An existing account is fine: you are logging in to it again.
        if (!error.isEmpty() && !error.contains(QStringLiteral("already exists"))) {
            setStatusText(tr("Your server could not start."));
            reportError(tr("Could not host a server"),
                        tr("Could not create the administrator account: %1.").arg(error));
            return;
        }
        launchLocalServer(choice);
    };
    m_localServer.createAccount(m_localServerExecutable, dataDir, choice.adminName, choice.adminPassword,
                                QStringLiteral("owner"), [this, choice, dataDir, finish](const QString &error) {
                                    if (error.contains(QStringLiteral("already has an owner"))) {
                                        m_localServer.createAccount(m_localServerExecutable, dataDir, choice.adminName,
                                                                    choice.adminPassword, QStringLiteral("admin"),
                                                                    finish);
                                        return;
                                    }
                                    finish(error);
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
    playSound(audio::EventSound::Connected);
    m_channelTree->setFocus(Qt::OtherFocusReason);
}

void MainWindow::onDisconnected(const client::DisconnectInfo &info)
{
    setWindowTitle(QStringLiteral("OpenTalk"));
    playSound(audio::EventSound::Disconnected);
    m_channelTree->showMessage(tr("Not connected to a server"));
    m_userList->clear();
    m_requestDescriptions.clear();
    m_requestSuccesses.clear();
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
        if (info.protocolCode == protocol::ErrorCode::Kicked || info.protocolCode == protocol::ErrorCode::Banned) {
            setStatusText(tr("Not connected"));
            reportError(info.protocolCode == protocol::ErrorCode::Kicked ? tr("Kicked") : tr("Banned"),
                        tr("Disconnected from %1. %2.").arg(serverDisplayName(), sentence(info.detail)));
            return;
        }
        [[fallthrough]];
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
    if (attempt == 1) {
        playSound(audio::EventSound::Disconnected);
    }
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
                .arg(ChannelTree::userLabel(user.name, user.id == state.selfId(),
                                            ChannelTree::roleLabel(state, static_cast<quint32>(user.role)), user.flags),
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
    if (m_adminAction == nullptr) {
        return; // still building the window
    }
    const bool connected = m_connection.state() == client::ConnectionState::Connected;
    const bool active = m_connection.state() != client::ConnectionState::Disconnected;
    const client::ClientState &state = m_connection.serverState();
    const auto channelId = m_channelTree->currentChannelId();
    const bool realChannel = channelId && *channelId != protocol::kRootChannelId;
    const auto userId = selectedUserId();
    const bool otherUser = connected && userId && *userId != state.selfId() && state.user(*userId);
    // Moderation actions appear only for people who may use them (here or in some channel);
    // the server still decides each case.
    const auto offer = [connected](QAction *action, bool allowed, bool applicable) {
        action->setVisible(connected && allowed);
        action->setEnabled(connected && allowed && applicable);
    };
    m_disconnectAction->setEnabled(active);
    m_hostAction->setEnabled(m_localServer.state() == client::LocalServer::State::Stopped);
    m_stopHostingAction->setEnabled(m_localServer.state() == client::LocalServer::State::Running);
    m_joinAction->setEnabled(connected && channelId.has_value());
    m_createChannelAction->setEnabled(connected && (state.canSomewhere(Capability::ChannelCreateTemporary) ||
                                                    state.canSomewhere(Capability::ChannelCreatePersistent)));
    m_editChannelAction->setEnabled(connected && realChannel && state.canSomewhere(Capability::ChannelEdit));
    m_deleteChannelAction->setEnabled(connected && realChannel && state.canSomewhere(Capability::ChannelDelete));
    m_privateMessageAction->setEnabled(connected);
    m_userVolumeAction->setEnabled(connected);
    offer(m_broadcastAction, state.can(Capability::BroadcastSend), true);
    offer(m_warnAction, state.canSomewhere(Capability::UserWarn), otherUser);
    offer(m_muteUserAction, state.canSomewhere(Capability::UserMute), otherUser);
    offer(m_kickAction, state.canSomewhere(Capability::UserKick), otherUser);
    offer(m_banAction,
          state.canSomewhere(Capability::UserBanTemporary) || state.canSomewhere(Capability::UserBanPermanent),
          otherUser);
    offer(m_moveUserAction, state.canSomewhere(Capability::UserMove), otherUser);
    offer(m_adminAction, state.canModerate(), true);
    m_reportAction->setEnabled(otherUser);
    m_mySanctionsAction->setEnabled(connected);
    updateMediaActions();
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
    if (channel->hasPassword && !state.can(Capability::ChannelBypassRestrictions)) {
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
    if (m_connection.state() != client::ConnectionState::Connected || !m_createChannelAction->isEnabled()) {
        return;
    }
    // Create inside the selected channel when it can have sub-channels, else at the top.
    quint32 parentId = protocol::kRootChannelId;
    if (const auto selected = m_channelTree->currentChannelId()) {
        if (const auto channel = state.channel(*selected); channel && channel->persistent) {
            parentId = *selected;
        }
    }
    CreateChannelDialog dialog(channelPath(parentId), state.canSomewhere(Capability::ChannelCreatePersistent), this);
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

void MainWindow::track(quint64 id, const QString &failure, const QString &success)
{
    if (id == 0) {
        setStatusText(tr("%1: you are not connected.").arg(failure));
        return;
    }
    m_requestDescriptions.insert(id, failure);
    if (!success.isEmpty()) {
        m_requestSuccesses.insert(id, success);
    }
}

void MainWindow::onRequestFinished(quint64 requestId, const std::optional<protocol::Error> &error)
{
    const QString description = m_requestDescriptions.take(requestId);
    const QString success = m_requestSuccesses.take(requestId);
    if (!error) {
        if (!success.isEmpty()) {
            appendHistory(success);
            setStatusText(success);
        }
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
    case ErrorCode::Muted:
        return error.detail.isEmpty() ? tr("you are muted.") : error.detail + QLatin1Char('.');
    case ErrorCode::Banned:
        return error.detail.isEmpty() ? tr("you are banned there.") : error.detail + QLatin1Char('.');
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
        // From user 0: the server itself, for example its welcome message.
        line = message.fromUserId == 0 ? tr("Server: %1").arg(message.text)
                                       : tr("Message to everyone from %1: %2").arg(sender, message.text);
        spoken = line;
        break;
    }
    m_history->appendPlainText(QStringLiteral("[%1] %2").arg(timestamp(message.timestampMs), line));
    if (!fromMe) {
        announce(static_cast<int>(Category::Message), spoken);
        playSound(message.target == protocol::TextTarget::Channel ? audio::EventSound::ChannelMessage
                  : message.target == protocol::TextTarget::User  ? audio::EventSound::PrivateMessage
                                                                  : audio::EventSound::ServerMessage);
    }
}

// ------------------------------------------------------------------ moderation

void MainWindow::moderateSelectedUser(SanctionType type)
{
    const client::ClientState &state = m_connection.serverState();
    const auto userId = selectedUserId();
    const auto user = userId ? state.user(*userId) : std::nullopt;
    if (m_connection.state() != client::ConnectionState::Connected || !user || user->id == state.selfId()) {
        return;
    }
    SanctionDialog::Options options;
    options.targetName = user->name;
    options.type = type;
    options.channelId = user->channelId;
    options.channelName = channelPath(user->channelId);
    options.canBanAddress = state.can(Capability::UserBanAddress);
    options.canBanPermanently = state.can(Capability::UserBanPermanent);
    SanctionDialog dialog(options, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    protocol::ApplySanction request = dialog.request();
    request.userId = user->id;
    QString done;
    switch (static_cast<SanctionType>(request.type)) {
    case SanctionType::Warning:
        done = tr("You warned %1.");
        break;
    case SanctionType::VoiceMute:
        done = tr("You muted the voice of %1.");
        break;
    case SanctionType::TextMute:
        done = tr("You muted the messages of %1.");
        break;
    case SanctionType::Mute:
        done = tr("You muted %1.");
        break;
    case SanctionType::Kick:
        done = tr("You kicked %1.");
        break;
    case SanctionType::Ban:
        done = tr("You banned %1.");
        break;
    case SanctionType::Restrict:
        done = tr("You restricted %1.");
        break;
    }
    track(m_connection.sendRequest(request),
          tr("Could not give %1 a %2").arg(user->name, sanctionTypeName(request.type).toLower()), done.arg(user->name));
}

void MainWindow::moveSelectedUser()
{
    const client::ClientState &state = m_connection.serverState();
    const auto userId = selectedUserId();
    const auto user = userId ? state.user(*userId) : std::nullopt;
    if (m_connection.state() != client::ConnectionState::Connected || !user) {
        return;
    }
    QList<QPair<quint32, QString>> channels;
    for (const protocol::ChannelInfo &channel : state.channels()) {
        channels.append({channel.id, channelPath(channel.id)});
    }
    ChannelPickerDialog dialog(tr("Move %1").arg(user->name), tr("&Move %1 to:").arg(user->name), channels,
                               user->channelId, this);
    if (dialog.exec() != QDialog::Accepted || dialog.channelId() == 0 || dialog.channelId() == user->channelId) {
        return;
    }
    const quint32 target = dialog.channelId();
    track(m_connection.sendRequest(protocol::MoveUser{0, user->id, target}), tr("Could not move %1").arg(user->name),
          tr("You moved %1 to %2.").arg(user->name, channelPath(target)));
}

void MainWindow::reportSelectedUser()
{
    const client::ClientState &state = m_connection.serverState();
    const auto userId = selectedUserId();
    const auto user = userId ? state.user(*userId) : std::nullopt;
    if (m_connection.state() != client::ConnectionState::Connected || !user || user->id == state.selfId()) {
        return;
    }
    ReportDialog dialog(user->name, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    track(m_connection.sendRequest(protocol::ReportUser{0, user->id, dialog.reason()}),
          tr("Could not report %1").arg(user->name),
          tr("Your report about %1 was sent to the moderators.").arg(user->name));
}

void MainWindow::editSelectedChannel()
{
    const auto channelId = m_channelTree->currentChannelId();
    const auto channel = channelId ? m_connection.serverState().channel(*channelId) : std::nullopt;
    if (m_connection.state() != client::ConnectionState::Connected || !channel ||
        channel->id == protocol::kRootChannelId) {
        return;
    }
    EditChannelDialog dialog(*channel, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const protocol::EditChannel request = dialog.request();
    track(m_connection.sendRequest(request), tr("Could not change %1").arg(channelPath(channel->id)),
          tr("Saved the changes to %1.").arg(request.name));
}

void MainWindow::openMySanctions()
{
    if (m_connection.state() != client::ConnectionState::Connected) {
        return;
    }
    MySanctionsDialog dialog(m_connection, this);
    dialog.exec();
}

void MainWindow::openAdministration()
{
    if (m_connection.state() != client::ConnectionState::Connected || !m_connection.serverState().canModerate()) {
        return;
    }
    AdminDialog dialog(m_connection, this);
    connect(&dialog, &AdminDialog::statusChanged, this, &MainWindow::setStatusText);
    dialog.exec();
}

void MainWindow::sendBroadcast()
{
    if (m_connection.state() != client::ConnectionState::Connected ||
        !m_connection.serverState().can(Capability::BroadcastSend)) {
        return;
    }
    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("Message to everyone"),
                                               tr("Message to everyone on %1:").arg(serverDisplayName()),
                                               QLineEdit::Normal, QString(), &ok);
    if (!ok || text.trimmed().isEmpty()) {
        return;
    }
    const quint64 id = m_connection.sendText(protocol::TextTarget::Server, 0, text);
    if (id == 0) {
        setStatusText(tr("Message not sent: it contains characters that cannot be sent."));
        return;
    }
    track(id, tr("Message to everyone not sent"));
}

void MainWindow::onSanctionNotice(const protocol::SanctionInfo &sanction)
{
    const auto type = static_cast<SanctionType>(sanction.type);
    const QString kind = sanctionTypeName(sanction.type).toLower();
    QString text;
    switch (static_cast<moderation::SanctionState>(sanction.state)) {
    case moderation::SanctionState::Active:
        text =
            tr("%1, from %2.")
                .arg(describeSanction(sanction, sanction.channelId != 0 ? channelPath(sanction.channelId) : QString()),
                     sanction.actorName.isEmpty() ? tr("the server") : sanction.actorName);
        if (!sanction.reason.isEmpty()) {
            text += tr(" Reason: %1.").arg(sanction.reason);
        }
        if (type != SanctionType::Kick) {
            text += tr(" To appeal, use Moderation, My sanctions and appeals.");
        }
        break;
    case moderation::SanctionState::Expired:
        text = tr("Your %1 has ended.").arg(kind);
        break;
    case moderation::SanctionState::Revoked:
        text = sanction.resolvedBy.isEmpty() ? tr("Your %1 was lifted.").arg(kind)
                                             : tr("Your %1 was lifted by %2.").arg(kind, sanction.resolvedBy);
        break;
    case moderation::SanctionState::Overturned:
        text = tr("Your %1 was overturned on appeal.").arg(kind);
        break;
    }
    appendHistory(text);
    setStatusText(text);
    playSound(audio::EventSound::ServerMessage);
}

void MainWindow::onUserUpdated(const protocol::UserInfo &user, const protocol::UserInfo &before)
{
    scheduleRefresh();
    const client::ClientState &state = m_connection.serverState();
    if (user.id == state.selfId()) {
        if (user.role != before.role) {
            const QString role = ChannelTree::roleLabel(state, static_cast<quint32>(user.role));
            const QString text =
                role.isEmpty() ? tr("You are now a registered user.") : tr("You are now %1.").arg(role);
            appendHistory(text);
            setStatusText(text);
        }
        return;
    }
    if (user.channelId != state.currentChannelId()) {
        return;
    }
    const bool mutedBefore = (before.flags & protocol::VoiceMutedFlag) != 0;
    const bool mutedNow = (user.flags & protocol::VoiceMutedFlag) != 0;
    if (mutedBefore != mutedNow) {
        appendHistory(mutedNow ? tr("%1 was muted.").arg(user.name) : tr("%1 can talk again.").arg(user.name));
    }
}

// ------------------------------------------------------------------------ misc

void MainWindow::openSettings()
{
    openSettingsAt(QString());
}

void MainWindow::openAudioSettings()
{
    openSettingsAt(tr("Audio"));
}

void MainWindow::openSettingsAt(const QString &category)
{
    SettingsDialog dialog(m_settings, m_paths, m_audio, this);
    if (!category.isEmpty()) {
        dialog.selectCategory(category);
    }
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
    m_pluginHost->stopAll();
    m_connection.disconnectFromServer();
    m_audio->engine().stop();
    m_localServer.stop(); // finishes in the background; the destructor waits briefly
    saveLayout();
    if (!m_settings.isReadOnly()) {
        m_settings.save();
    }
    QMainWindow::closeEvent(event);
}

} // namespace opentalk::ui
