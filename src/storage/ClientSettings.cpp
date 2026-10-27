// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/storage/ClientSettings.h"

#include "opentalk/logging/Logging.h"

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
    return s;
}

} // namespace opentalk::storage::client
