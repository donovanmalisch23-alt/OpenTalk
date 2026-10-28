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
    /// Whether this user's microphone is heard right now.
    bool isUserTalking(quint32 userId) const { return userActivity(userId) & streamBit(audio::StreamType::Voice); }
    /// What is heard from this user right now: a combination of streamBit() values.
    int userActivity(quint32 userId) const { return m_activity.value(userId); }
    static constexpr int streamBit(audio::StreamType type) { return 1 << static_cast<int>(type); }

    // Files and computer audio played into the channel.
    bool startMedia(const QString &path, QString *errorMessage) { return m_engine->startMedia(path, errorMessage); }
    void stopMedia() { m_engine->stopMedia(); }
    void setMediaPaused(bool paused) { m_engine->setMediaPaused(paused); }
    void skipMedia(qint64 deltaMs) { m_engine->skipMedia(deltaMs); }
    audio::MediaStatus mediaStatus() const { return m_engine->mediaStatus(); }
    bool canShareSystemAudio() const { return m_engine->canShareSystemAudio(); }
    bool startSystemAudio(QString *errorMessage, bool *excludesOwnSound)
    {
        return m_engine->startSystemAudio(errorMessage, excludesOwnSound);
    }
    void stopSystemAudio() { m_engine->stopSystemAudio(); }
    bool isSharingSystemAudio() const { return m_engine->isSharingSystemAudio(); }

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
    /// A user's microphone started or stopped being heard.
    void talkingChanged(quint32 userId, bool talking);
    /// What is heard from a user changed (voice, a file, computer audio); see userActivity().
    void activityChanged(quint32 userId, int streams);
    /// A file being streamed reached its end.
    void mediaFinished(const QString &title);
    /// Streaming a file or computer audio stopped because the connection closed.
    void streamingInterrupted();
    /// Sharing computer audio stopped by itself, for example because the device went away.
    void systemAudioStopped(const QString &reason);
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
    QHash<quint32, int> m_activity; ///< User -> streamBit() values of the streams being heard.
};

} // namespace opentalk::ui
