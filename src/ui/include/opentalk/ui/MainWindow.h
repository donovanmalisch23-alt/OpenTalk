// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/audio/AudioBackend.h"
#include "opentalk/audio/Sounds.h"
#include "opentalk/client/Connection.h"
#include "opentalk/client/LocalServer.h"
#include "opentalk/storage/ServerProfiles.h"
#include "opentalk/ui/ConnectDialog.h"

#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QTimer>

#include <functional>
#include <optional>

class QAction;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QSplitter;

namespace opentalk::storage {
class AppPaths;
class SettingsStore;
} // namespace opentalk::storage

namespace opentalk::ui {

class AnnouncementQueue;
class AudioController;
class ChannelTree;

/// The primary window: channel tree (with the users in each channel), online users,
/// conversation history, message input and status bar.
///
/// Keyboard:
///   F6 / Shift+F6     next / previous pane
///   Ctrl+1 .. Ctrl+4  channels, users, history, message input
///   Ctrl+O / Ctrl+D   connect / disconnect
///   Enter             in the channel tree: join the channel, or message the user
///   Ctrl+N            create a channel     Ctrl+P  private message to the selected user
///   F4                settings
///   Ctrl+T            talk on/off (push-to-talk mode)   Ctrl+M  mute microphone
///   Ctrl+Shift+M      mute everything you hear          Ctrl+Shift+V  volume for the selected user
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    enum class Pane { Channels = 0, Users, History, MessageInput };

    /// What the user is asked when a server's certificate is not yet trusted.
    struct TrustQuestion {
        QString serverName;
        QString host;
        QString presentedFingerprint;
        QString previousFingerprint; ///< Non-empty if the certificate changed.
    };
    using TrustPrompt = std::function<bool(const TrustQuestion &)>;
    using ErrorReporter = std::function<void(const QString &title, const QString &text)>;

    /// `audioBackend` is the sound system to use; null means the computer's own
    /// (tests pass a synthetic one).
    MainWindow(storage::SettingsStore &settings, const storage::AppPaths &paths, storage::ServerProfileStore &profiles,
               std::unique_ptr<audio::AudioBackend> audioBackend = nullptr, QWidget *parent = nullptr);
    ~MainWindow() override;

    /// Shows `text` in the status bar and, if enabled in settings, asks the screen reader
    /// to announce it.
    void setStatusText(const QString &text);
    QString statusText() const;

    /// Connects using a profile. If `save` is true the profile (without password) is
    /// stored in the server list, including the fingerprint once the user trusts it.
    void connectToServer(const storage::ServerProfile &profile, const QString &password, bool save);

    QWidget *paneWidget(Pane pane) const;
    ChannelTree *channelTree() const { return m_channelTree; }
    QString historyText() const;
    client::Connection &connection() { return m_connection; }
    client::LocalServer &localServer() { return m_localServer; }
    AudioController &audio() { return *m_audio; }
    QString voiceStatusText() const;

    /// Hosts a server on this computer and joins it (what the "Host a server" dialog does).
    void startLocalServer(const LocalServerDialog::Choice &choice);
    /// Where opentalk-server is; by default next to this program.
    void setLocalServerExecutable(const QString &path) { m_localServerExecutable = path; }
    QAction *settingsAction() const { return m_settingsAction; }
    QList<QAction *> paneActions() const { return m_paneActions; }

    // Hooks that replace dialogs and speech, for automated tests.
    void setTrustPrompt(TrustPrompt prompt) { m_trustPrompt = std::move(prompt); }
    void setErrorReporter(ErrorReporter reporter) { m_errorReporter = std::move(reporter); }
    void setAnnouncementHook(std::function<void(const QString &)> hook) { m_announcementHook = std::move(hook); }

public slots:
    void focusPane(opentalk::ui::MainWindow::Pane pane);
    void focusNextPane();
    void focusPreviousPane();
    void openSettings();
    void openConnectDialog();
    void disconnectFromServer();
    void openLocalServerDialog();
    void stopLocalServer();
    void createChannel();
    void deleteSelectedChannel();
    void joinChannel(quint32 channelId);
    void sendPrivateMessage(quint32 userId);
    void toggleTransmit();
    void toggleMicrophoneMute();
    void toggleDeafen();
    void openUserVolume();
    void openAudioSettings();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildCentralArea();
    void buildMenus();
    void restoreLayout();
    void saveLayout();
    void moveFocusBy(int step);
    void handleMessageInput();
    void applyAnnouncementSettings();
    void announce(int category, const QString &text);
    void appendHistory(const QString &line);
    void reportError(const QString &title, const QString &text);
    void updateActions();
    void scheduleRefresh();
    void refreshViews();
    QString serverDisplayName() const;
    QString userName(quint32 userId) const;
    QString channelPath(quint32 channelId) const;

    void onConnected();
    void onDisconnected(const client::DisconnectInfo &info);
    void onConnectionLost(const client::DisconnectInfo &info, int attempt, int delayMs);
    void onTextReceived(const protocol::TextMessage &message);
    void onRequestFinished(quint64 requestId, const std::optional<protocol::Error> &error);
    void handleUntrustedCertificate(const client::DisconnectInfo &info);
    void launchLocalServer(const LocalServerDialog::Choice &choice);
    void onLocalServerStarted(quint16 port, const QString &fingerprint);
    bool isConnectedToLocalServer() const;
    static QString friendlyError(const protocol::Error &error);
    void buildAudio(std::unique_ptr<audio::AudioBackend> backend);
    void updateVoiceStatus();
    void playSound(audio::EventSound sound);
    std::optional<quint32> selectedUserId() const;
    void openSettingsAt(const QString &category);

    storage::SettingsStore &m_settings;
    const storage::AppPaths &m_paths;
    storage::ServerProfileStore &m_profiles;
    client::Connection m_connection;
    client::LocalServer m_localServer;
    QString m_localServerExecutable;
    std::optional<LocalServerDialog::Choice> m_localChoice;
    std::optional<storage::ServerProfile> m_profile;
    QString m_password;
    bool m_saveProfile = false;
    QHash<quint64, QString> m_requestDescriptions;

    TrustPrompt m_trustPrompt;
    ErrorReporter m_errorReporter;
    std::function<void(const QString &)> m_announcementHook;
    AnnouncementQueue *m_announcements = nullptr;
    QTimer m_refreshTimer;

    QSplitter *m_mainSplitter = nullptr;
    ChannelTree *m_channelTree = nullptr;
    QListWidget *m_userList = nullptr;
    QPlainTextEdit *m_history = nullptr;
    QLineEdit *m_messageInput = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_voiceLabel = nullptr;
    AudioController *m_audio = nullptr;
    QString m_lastDeviceProblem;
    QTimer m_voicePathTimer;
    QAction *m_settingsAction = nullptr;
    QAction *m_connectAction = nullptr;
    QAction *m_disconnectAction = nullptr;
    QAction *m_hostAction = nullptr;
    QAction *m_stopHostingAction = nullptr;
    QAction *m_joinAction = nullptr;
    QAction *m_createChannelAction = nullptr;
    QAction *m_deleteChannelAction = nullptr;
    QAction *m_privateMessageAction = nullptr;
    QAction *m_transmitAction = nullptr;
    QAction *m_muteAction = nullptr;
    QAction *m_deafenAction = nullptr;
    QAction *m_userVolumeAction = nullptr;
    QList<QAction *> m_paneActions;
};

} // namespace opentalk::ui
