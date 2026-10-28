// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/audio/AudioEngine.h"
#include "opentalk/client/Connection.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>

#include <memory>

namespace opentalk::storage {
class AppPaths;
class SettingsStore;
} // namespace opentalk::storage

namespace opentalk::ui {

class PushToTalkMonitor;

/// Connects the audio engine to the connection and to the user's settings: microphone
/// to server, server to speakers, push-to-talk, mute, deafen, per-user volume, event
/// sounds and soundpacks. Has no widgets, so it is testable on its own.
class AudioController : public QObject {
    Q_OBJECT

public:
    AudioController(storage::SettingsStore &settings, const storage::AppPaths &paths, client::Connection &connection,
                    std::unique_ptr<audio::AudioBackend> backend, QObject *parent = nullptr);
    ~AudioController() override;

    audio::AudioEngine &engine() { return *m_engine; }
    PushToTalkMonitor &pushToTalk() { return *m_pushToTalk; }

    /// Re-reads every audio and sound setting (also done automatically when they change).
    void applySettings();

    void setTransmitLatched(bool on);
    bool isTransmitLatched() const { return m_engine->isTransmitLatched(); }
    void setMicrophoneMuted(bool muted);
    bool isMicrophoneMuted() const { return m_engine->isMicrophoneMuted(); }
    void setDeafened(bool deafened);
    bool isDeafened() const { return m_engine->isDeafened(); }
    bool isTransmitting() const { return m_engine->isTransmitting(); }
    bool isUserTalking(quint32 userId) const { return m_talking.contains(userId); }

    /// Volume for one user in percent (0-400; 100 = unchanged), remembered per server
    /// and user name.
    int userVolume(quint32 userId) const;
    void setUserVolume(quint32 userId, int percent);

    void playSound(audio::EventSound sound);

    /// Folders holding soundpacks: the user's (in the data folder) and the ones shipped
    /// next to the program.
    QStringList soundPackFolders() const;
    QStringList availableSoundPacks() const;
    /// Loads a soundpack over the built-in sounds; returns the files that were used.
    QStringList loadSoundPack(const QString &name);

signals:
    void transmittingChanged(bool transmitting);
    void talkingChanged(quint32 userId, bool talking);
    void deviceProblem(const QString &message);
    void devicesStarted();

private:
    QString userVolumeKey(quint32 userId) const;
    void applyUserGain(quint32 userId);
    void onVoiceReady();
    void onConnectionStateChanged(client::ConnectionState state);

    storage::SettingsStore &m_settings;
    const storage::AppPaths &m_paths;
    client::Connection &m_connection;
    std::unique_ptr<audio::AudioEngine> m_engine;
    PushToTalkMonitor *m_pushToTalk = nullptr;
    QString m_loadedPack;
    QSet<quint32> m_gainApplied;
    QHash<quint32, int> m_talkingStreams; ///< User -> number of streams currently audible.
    QSet<quint32> m_talking;
};

} // namespace opentalk::ui
