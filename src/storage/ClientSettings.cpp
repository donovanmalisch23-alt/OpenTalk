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
    s.add({kMainWindowGeometry, SettingType::ByteArray, QByteArray(), {}});
    s.add({kMainSplitterState, SettingType::ByteArray, QByteArray(), {}});
    return s;
}

} // namespace opentalk::storage::client
