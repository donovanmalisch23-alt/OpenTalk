// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/ui/AudioController.h"

#include "opentalk/logging/Logging.h"
#include "opentalk/storage/AppPaths.h"
#include "opentalk/storage/ClientSettings.h"
#include "opentalk/storage/SettingsStore.h"
#include "opentalk/ui/PushToTalkMonitor.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <utility>

namespace opentalk::ui {

using audio::EventSound;
namespace keys = storage::client;

AudioController::AudioController(storage::SettingsStore &settings, const storage::AppPaths &paths,
                                 client::Connection &connection, std::unique_ptr<audio::AudioBackend> backend,
                                 QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_paths(paths)
    , m_connection(connection)
    , m_engine(std::make_unique<audio::AudioEngine>(std::move(backend)))
    , m_pushToTalk(new PushToTalkMonitor(this))
{
    connect(m_pushToTalk, &PushToTalkMonitor::pressedChanged, m_engine.get(),
            &audio::AudioEngine::setPushToTalkPressed);
    connect(m_engine.get(), &audio::AudioEngine::packetReady, this,
            [this](audio::StreamType type, quint16 sequence, const QByteArray &opus) {
                m_connection.sendAudio(static_cast<protocol::AudioStream>(type), sequence, opus);
            });
    connect(&m_connection, &client::Connection::audioReceived, this, [this](const protocol::AudioData &audio) {
        if (!m_gainApplied.contains(audio.userId)) {
            applyUserGain(audio.userId);
        }
        m_engine->receivePacket(audio.userId, static_cast<audio::StreamType>(audio.stream), audio.sequence,
                                audio.payload);
    });
    connect(&m_connection, &client::Connection::voiceReady, this, &AudioController::onVoiceReady);
    connect(&m_connection, &client::Connection::stateChanged, this, &AudioController::onConnectionStateChanged);
    connect(m_engine.get(), &audio::AudioEngine::transmittingChanged, this, &AudioController::transmittingChanged);
    connect(m_engine.get(), &audio::AudioEngine::deviceProblem, this, &AudioController::deviceProblem);
    connect(m_engine.get(), &audio::AudioEngine::devicesStarted, this, &AudioController::devicesStarted);
    connect(m_engine.get(), &audio::AudioEngine::talkingChanged, this,
            [this](quint32 userId, audio::StreamType type, bool talking) {
                const int before = m_activity.value(userId);
                const int after = talking ? (before | streamBit(type)) : (before & ~streamBit(type));
                if (after == before) {
                    return;
                }
                if (after == 0) {
                    m_activity.remove(userId);
                } else {
                    m_activity.insert(userId, after);
                }
                emit activityChanged(userId, after);
                if (type == audio::StreamType::Voice) {
                    emit talkingChanged(userId, talking);
                }
            });
    connect(m_engine.get(), &audio::AudioEngine::mediaFinished, this, &AudioController::mediaFinished);
    connect(m_engine.get(), &audio::AudioEngine::systemAudioStopped, this, &AudioController::systemAudioStopped);
    connect(&m_settings, &storage::SettingsStore::valueChanged, this, [this](const QString &key) {
        if (key.startsWith(QLatin1String("audio/")) || key.startsWith(QLatin1String("sounds/"))) {
            if (key != keys::kUserVolumes) {
                applySettings();
            }
        }
    });
    applySettings();
    m_engine->start();
}

AudioController::~AudioController()
{
    m_engine->stop();
}

void AudioController::applySettings()
{
    audio::AudioSettings s;
    s.inputDeviceId = m_settings.value(keys::kInputDevice).toString();
    s.outputDeviceId = m_settings.value(keys::kOutputDevice).toString();
    s.inputGain = static_cast<float>(m_settings.value(keys::kInputVolume).toInt()) / 100.0f;
    s.outputGain = static_cast<float>(m_settings.value(keys::kOutputVolume).toInt()) / 100.0f;
    const QString mode = m_settings.value(keys::kTransmitMode).toString();
    s.mode = mode == keys::kTransmitVoiceActivation ? audio::TransmitMode::VoiceActivation
             : mode == keys::kTransmitContinuous    ? audio::TransmitMode::Continuous
                                                    : audio::TransmitMode::PushToTalk;
    s.voiceActivationThresholdDb = static_cast<float>(m_settings.value(keys::kVoiceActivationLevel).toInt());
    int bitrate = m_settings.value(keys::kVoiceBitrate).toInt() * 1000;
    if (const quint32 limit = m_connection.voiceMaxBitrate(); limit > 0) {
        bitrate = std::min(bitrate, static_cast<int>(limit));
    }
    s.bitrate = bitrate;
    s.soundsEnabled = m_settings.value(keys::kSoundsEnabled).toBool();
    s.soundGain = static_cast<float>(m_settings.value(keys::kSoundsVolume).toInt()) / 100.0f;
    s.pushToTalkSounds = m_settings.value(keys::kPushToTalkSounds).toBool();
    s.noiseSuppression = m_settings.value(keys::kNoiseSuppression).toBool();
    s.autoGain = m_settings.value(keys::kAutoGain).toBool();
    s.echoCancellation = m_settings.value(keys::kEchoCancellation).toBool();
    s.highPass = m_settings.value(keys::kHighPass).toBool();
    s.bassDb = m_settings.value(keys::kBassDb).toInt();
    s.trebleDb = m_settings.value(keys::kTrebleDb).toInt();
    s.pitchSemitones = m_settings.value(keys::kPitchSemitones).toInt();
    int mediaBitrate = m_settings.value(keys::kMediaBitrate).toInt() * 1000;
    if (const quint32 limit = m_connection.voiceMaxBitrate(); limit > 0) {
        mediaBitrate = std::min(mediaBitrate, static_cast<int>(limit));
    }
    s.mediaBitrate = mediaBitrate;
    s.mediaGain = static_cast<float>(m_settings.value(keys::kMediaVolume).toInt()) / 100.0f;
    s.mediaMonitor = m_settings.value(keys::kMediaMonitor).toBool();
    m_engine->setSettings(s);
    m_engine->setIncomingStreamMuted(audio::StreamType::Media, m_settings.value(keys::kMuteIncomingMedia).toBool());
    m_engine->setIncomingStreamMuted(audio::StreamType::SystemAudio,
                                     m_settings.value(keys::kMuteIncomingSystemAudio).toBool());

    const QKeySequence key =
        QKeySequence::fromString(m_settings.value(keys::kPushToTalkKey).toString(), QKeySequence::PortableText);
    if (!m_pushToTalk->setKey(key)) {
        qCWarning(lcUi) << "Push-to-talk key cannot be used:" << key.toString(QKeySequence::PortableText);
    }

    const QString pack = m_settings.value(keys::kSoundPack).toString();
    if (pack != m_loadedPack || m_loadedPack.isNull()) {
        loadSoundPack(pack);
    }
}

void AudioController::onVoiceReady()
{
    m_gainApplied.clear();
    applySettings(); // the server's bit rate limit is known now
    m_engine->setTransmitAllowed(true);
}

void AudioController::onConnectionStateChanged(client::ConnectionState state)
{
    if (state != client::ConnectionState::Connected) {
        m_engine->setTransmitAllowed(false);
        m_engine->resetStreams();
        m_gainApplied.clear();
        const QHash<quint32, int> activity = std::exchange(m_activity, {});
        for (auto it = activity.cbegin(); it != activity.cend(); ++it) {
            emit activityChanged(it.key(), 0);
            if (it.value() & streamBit(audio::StreamType::Voice)) {
                emit talkingChanged(it.key(), false);
            }
        }
        if (m_engine->mediaStatus().active || m_engine->isSharingSystemAudio()) {
            m_engine->stopMedia();
            m_engine->stopSystemAudio();
            emit streamingInterrupted();
        }
    }
}

void AudioController::setTransmitLatched(bool on)
{
    m_engine->setTransmitLatched(on);
}

void AudioController::setMicrophoneMuted(bool muted)
{
    m_engine->setMicrophoneMuted(muted);
}

void AudioController::setDeafened(bool deafened)
{
    m_engine->setDeafened(deafened);
}

QString AudioController::userVolumeKey(quint32 userId) const
{
    const auto user = m_connection.serverState().user(userId);
    if (!user) {
        return {};
    }
    const client::ConnectOptions &options = m_connection.options();
    return QStringLiteral("%1:%2/%3").arg(options.host.toLower()).arg(options.port).arg(user->name);
}

int AudioController::userVolume(quint32 userId) const
{
    const QString key = userVolumeKey(userId);
    return key.isEmpty() ? 100 : m_settings.value(keys::kUserVolumes).toMap().value(key, 100).toInt();
}

void AudioController::setUserVolume(quint32 userId, int percent)
{
    const QString key = userVolumeKey(userId);
    if (key.isEmpty()) {
        return;
    }
    percent = std::clamp(percent, 0, 400);
    QVariantMap volumes = m_settings.value(keys::kUserVolumes).toMap();
    if (percent == 100) {
        volumes.remove(key);
    } else {
        volumes.insert(key, percent);
    }
    m_settings.setValue(keys::kUserVolumes, volumes);
    applyUserGain(userId);
}

void AudioController::applyUserGain(quint32 userId)
{
    m_gainApplied.insert(userId);
    m_engine->setUserGain(userId, static_cast<float>(userVolume(userId)) / 100.0f);
}

void AudioController::playSound(EventSound sound)
{
    m_engine->playSound(sound);
}

QStringList AudioController::soundPackFolders() const
{
    return {m_paths.dataRoot() + QStringLiteral("/soundpacks"),
            QCoreApplication::applicationDirPath() + QStringLiteral("/soundpacks")};
}

QStringList AudioController::availableSoundPacks() const
{
    QStringList names;
    for (const QString &folder : soundPackFolders()) {
        for (const QString &name : QDir(folder).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (!names.contains(name)) {
                names.append(name);
            }
        }
    }
    return names;
}

QStringList AudioController::loadSoundPack(const QString &name)
{
    m_loadedPack = name.isNull() ? QStringLiteral("") : name;
    audio::SoundSet sounds = audio::builtInSounds();
    QStringList used;
    if (!name.isEmpty()) {
        QString folder;
        for (const QString &candidate : soundPackFolders()) {
            if (QFileInfo(candidate + QLatin1Char('/') + name).isDir()) {
                folder = candidate + QLatin1Char('/') + name;
                break;
            }
        }
        if (folder.isEmpty()) {
            qCWarning(lcUi).noquote() << "Soundpack not found:" << name << "- using the built-in sounds";
        }
        for (int i = 0; !folder.isEmpty() && i < static_cast<int>(EventSound::Count); ++i) {
            for (const QString &extension : {QStringLiteral(".wav"), QStringLiteral(".flac"), QStringLiteral(".mp3")}) {
                const QString path =
                    folder + QLatin1Char('/') + audio::soundFileName(static_cast<EventSound>(i)) + extension;
                if (!QFileInfo::exists(path)) {
                    continue;
                }
                QVector<float> samples;
                QString error;
                if (audio::decodeAudioFile(path, &samples, &error, 10)) {
                    sounds[static_cast<std::size_t>(i)] = samples;
                    used.append(path);
                } else {
                    qCWarning(lcUi).noquote() << "Cannot use sound" << path << ":" << error;
                }
                break;
            }
        }
    }
    m_engine->setSoundSet(sounds);
    return used;
}

} // namespace opentalk::ui
