// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/storage/SettingsSchema.h"

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

/// Builds the schema for client-settings.json. See docs/reference/client-settings.md.
SettingsSchema schema();

} // namespace opentalk::storage::client
