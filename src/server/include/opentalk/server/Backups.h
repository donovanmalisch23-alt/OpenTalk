// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

#include <optional>

namespace opentalk::server {

class ServerDatabase;

/// One automatic backup: "<folder>/opentalk-YYYYMMDD-HHMMSS.db" (UTC).
struct BackupFile {
    QString path;
    QDateTime time; ///< UTC, from the name.
};

/// Automatic backups in `folder`, newest first. Other files are ignored.
QList<BackupFile> listBackups(const QString &folder);
/// Writes a backup named after `now` (UTC) and returns its path.
std::optional<QString> writeBackup(const ServerDatabase &database, const QString &folder, const QDateTime &now,
                                   QString *errorMessage = nullptr);
/// Deletes automatic backups beyond the newest `keep`; returns how many were deleted.
int pruneBackups(const QString &folder, int keep);

} // namespace opentalk::server
