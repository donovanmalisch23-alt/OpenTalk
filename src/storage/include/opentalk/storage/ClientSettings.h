// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/storage/SettingsSchema.h"

#include <QStringList>

#include <QString>

namespace opentalk::storage::client {

/// Current version of the client settings file format.
inline constexpr int kSchemaVersion = 1;

// Every key here is used by the application. Add a key only together with the code that reads it.
inline const QString kRestoreWindowLayout = QStringLiteral("general/restoreWindowLayout");
inline const QString kAnnounceStatusChanges = QStringLiteral("accessibility/announceStatusChanges");
inline const QString kAnnounceMessages = QStringLiteral("accessibility/announceMessages");
inline const QString kAnnouncePresence = QStringLiteral("accessibility/announcePresence");
inline const QString kLogLevel = QStringLiteral("diagnostics/logLevel");
inline const QString kLocalServerName = QStringLiteral("localServer/name");
inline const QString kLocalServerPort = QStringLiteral("localServer/port");
inline const QString kLocalServerAllowOthers = QStringLiteral("localServer/allowOtherComputers");
inline const QString kLocalServerAllowGuests = QStringLiteral("localServer/allowGuests");
inline const QString kLocalServerAdminName = QStringLiteral("localServer/adminName");
inline const QString kMainWindowGeometry = QStringLiteral("ui/mainWindowGeometry");
inline const QString kMainSplitterState = QStringLiteral("ui/mainSplitterState");
inline const QString kAnnounceTalking = QStringLiteral("accessibility/announceTalking");
inline const QString kInputDevice = QStringLiteral("audio/inputDevice");
inline const QString kOutputDevice = QStringLiteral("audio/outputDevice");
inline const QString kInputVolume = QStringLiteral("audio/inputVolume");
inline const QString kOutputVolume = QStringLiteral("audio/outputVolume");
inline const QString kTransmitMode = QStringLiteral("audio/transmitMode");
inline const QString kPushToTalkKey = QStringLiteral("audio/pushToTalkKey");
inline const QString kVoiceActivationLevel = QStringLiteral("audio/voiceActivationLevel");
inline const QString kVoiceBitrate = QStringLiteral("audio/bitrateKbps");
inline const QString kUseUdp = QStringLiteral("audio/useUdp");
inline const QString kNoiseSuppression = QStringLiteral("audio/noiseSuppression");
inline const QString kAutoGain = QStringLiteral("audio/autoGain");
inline const QString kEchoCancellation = QStringLiteral("audio/echoCancellation");
inline const QString kHighPass = QStringLiteral("audio/effects/highPass");
inline const QString kBassDb = QStringLiteral("audio/effects/bassDb");
inline const QString kTrebleDb = QStringLiteral("audio/effects/trebleDb");
inline const QString kPitchSemitones = QStringLiteral("audio/effects/pitchSemitones");
inline const QString kMediaBitrate = QStringLiteral("audio/media/bitrateKbps");
inline const QString kMediaVolume = QStringLiteral("audio/media/volume");
inline const QString kMediaMonitor = QStringLiteral("audio/media/monitor");
inline const QString kMuteIncomingMedia = QStringLiteral("audio/media/muteIncoming");
inline const QString kMuteIncomingSystemAudio = QStringLiteral("audio/media/muteIncomingComputerAudio");
inline const QString kMediaFolder = QStringLiteral("media/lastFolder");
inline const QString kUserVolumes = QStringLiteral("audio/userVolumes");
inline const QString kSoundsEnabled = QStringLiteral("sounds/enabled");
inline const QString kSoundsVolume = QStringLiteral("sounds/volume");
inline const QString kPushToTalkSounds = QStringLiteral("sounds/pushToTalk");
inline const QString kSoundPack = QStringLiteral("sounds/pack");

// Values of kTransmitMode.
inline const QString kTransmitPushToTalk = QStringLiteral("pushToTalk");
inline const QString kTransmitVoiceActivation = QStringLiteral("voiceActivation");
inline const QString kTransmitContinuous = QStringLiteral("continuous");

/// Builds the schema for client-settings.json. See docs/reference/client-settings.md.
SettingsSchema schema();

/// Settings that belong to one computer (window positions, the last folder used), so
/// "Export settings" leaves them out.
QStringList notExportedKeys();

/// Settings a support file leaves out because they are private or meaningless to others:
/// other people's names (per-user volumes), folders, the local server's admin name and
/// window positions.
QStringList privateKeys();

} // namespace opentalk::storage::client
