// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/storage/ClientSettings.h"

#include "opentalk/logging/Logging.h"

#include <algorithm>

namespace opentalk::storage::client {

SettingsSchema schema()
{
    SettingsSchema s(kSchemaVersion);
    s.add({kRestoreWindowLayout, SettingType::Bool, true, {}});
    s.add({kAnnounceStatusChanges, SettingType::Bool, true, {}});
    s.add({kAnnounceMessages, SettingType::Bool, true, {}});
    s.add({kAnnouncePresence, SettingType::Bool, true, {}});
    s.add({kLogLevel, SettingType::String, QStringLiteral("info"),
           [](const QVariant &v) { return logging::levelFromString(v.toString()).has_value(); }});
    s.add({kLocalServerName, SettingType::String, QStringLiteral("My OpenTalk server"),
           [](const QVariant &v) { return !v.toString().trimmed().isEmpty() && v.toString().size() <= 128; }});
    s.add({kLocalServerPort, SettingType::Int, 10350,
           [](const QVariant &v) { return v.toInt() >= 1 && v.toInt() <= 65535; }});
    s.add({kLocalServerAllowOthers, SettingType::Bool, false, {}});
    s.add({kLocalServerAllowGuests, SettingType::Bool, true, {}});
    s.add({kLocalServerAdminName, SettingType::String, QString(),
           [](const QVariant &v) { return v.toString().size() <= 32; }});
    s.add({kMainWindowGeometry, SettingType::ByteArray, QByteArray(), {}});
    s.add({kMainSplitterState, SettingType::ByteArray, QByteArray(), {}});
    s.add({kAnnounceTalking, SettingType::Bool, false, {}});
    const auto range = [](int low, int high) {
        return [low, high](const QVariant &v) { return v.toInt() >= low && v.toInt() <= high; };
    };
    const auto shortText = [](const QVariant &v) { return v.toString().size() <= 512; };
    s.add({kInputDevice, SettingType::String, QString(), shortText});
    s.add({kOutputDevice, SettingType::String, QString(), shortText});
    s.add({kInputVolume, SettingType::Int, 100, range(0, 400)});
    s.add({kOutputVolume, SettingType::Int, 100, range(0, 400)});
    s.add({kTransmitMode, SettingType::String, kTransmitPushToTalk, [](const QVariant &v) {
               const QString mode = v.toString();
               return mode == kTransmitPushToTalk || mode == kTransmitVoiceActivation || mode == kTransmitContinuous;
           }});
    s.add(
        {kPushToTalkKey, SettingType::String, QString(), [](const QVariant &v) { return v.toString().size() <= 64; }});
    s.add({kVoiceActivationLevel, SettingType::Int, -45, range(-80, 0)});
    s.add({kVoiceBitrate, SettingType::Int, 32, range(8, 128)});
    s.add({kUseUdp, SettingType::Bool, true, {}});
    s.add({kNoiseSuppression, SettingType::Bool, true, {}});
    s.add({kAutoGain, SettingType::Bool, false, {}});
    s.add({kEchoCancellation, SettingType::Bool, false, {}});
    s.add({kHighPass, SettingType::Bool, false, {}});
    s.add({kBassDb, SettingType::Int, 0, range(-12, 12)});
    s.add({kTrebleDb, SettingType::Int, 0, range(-12, 12)});
    s.add({kPitchSemitones, SettingType::Int, 0, range(-12, 12)});
    s.add({kMediaBitrate, SettingType::Int, 64, range(16, 128)});
    s.add({kMediaVolume, SettingType::Int, 100, range(0, 200)});
    s.add({kMediaMonitor, SettingType::Bool, true, {}});
    s.add({kMuteIncomingMedia, SettingType::Bool, false, {}});
    s.add({kMuteIncomingSystemAudio, SettingType::Bool, false, {}});
    s.add({kMediaFolder, SettingType::String, QString(), shortText});
    s.add({kUserVolumes, SettingType::IntMap, QVariantMap(), [](const QVariant &v) {
               const QVariantMap map = v.toMap();
               return map.size() <= 10000 && std::all_of(map.cbegin(), map.cend(), [](const QVariant &x) {
                          return x.toInt() >= 0 && x.toInt() <= 400;
                      });
           }});
    s.add({kSoundsEnabled, SettingType::Bool, true, {}});
    s.add({kSoundsVolume, SettingType::Int, 80, range(0, 100)});
    s.add({kPushToTalkSounds, SettingType::Bool, true, {}});
    s.add({kSoundPack, SettingType::String, QString(), [](const QVariant &v) {
               const QString name = v.toString();
               return name.size() <= 64 && !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\')) &&
                      name != QStringLiteral("..");
           }});
    return s;
}

QStringList notExportedKeys()
{
    return {kMainWindowGeometry, kMainSplitterState, kMediaFolder};
}

QStringList privateKeys()
{
    return {kMainWindowGeometry, kMainSplitterState, kMediaFolder, kUserVolumes, kLocalServerAdminName};
}

} // namespace opentalk::storage::client
