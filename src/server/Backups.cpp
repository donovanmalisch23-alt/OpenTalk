// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/server/Backups.h"

#include "opentalk/server/ServerDatabase.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTimeZone>

#include <algorithm>

namespace opentalk::server {
namespace {

const QString kTimeFormat = QStringLiteral("yyyyMMdd-HHmmss");

} // namespace

QList<BackupFile> listBackups(const QString &folder)
{
    static const QRegularExpression pattern(QStringLiteral("^opentalk-(\\d{8}-\\d{6})\\.db$"));
    QList<BackupFile> backups;
    for (const QString &name : QDir(folder).entryList({QStringLiteral("opentalk-*.db")}, QDir::Files)) {
        const QRegularExpressionMatch match = pattern.match(name);
        if (!match.hasMatch()) {
            continue;
        }
        const QDateTime local = QDateTime::fromString(match.captured(1), kTimeFormat);
        if (!local.isValid()) {
            continue;
        }
        backups.append({QDir(folder).filePath(name), QDateTime(local.date(), local.time(), QTimeZone::utc())});
    }
    std::sort(backups.begin(), backups.end(), [](const BackupFile &a, const BackupFile &b) { return a.time > b.time; });
    return backups;
}

std::optional<QString> writeBackup(const ServerDatabase &database, const QString &folder, const QDateTime &now,
                                   QString *errorMessage)
{
    if (!QDir().mkpath(folder)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("cannot create the backup folder %1").arg(folder);
        }
        return std::nullopt;
    }
    const QString path = QDir(folder).filePath(QStringLiteral("opentalk-%1.db").arg(now.toUTC().toString(kTimeFormat)));
    if (!database.backupTo(path, errorMessage)) {
        return std::nullopt;
    }
    return path;
}

int pruneBackups(const QString &folder, int keep)
{
    int removed = 0;
    const QList<BackupFile> backups = listBackups(folder);
    for (qsizetype i = std::max(0, keep); i < backups.size(); ++i) {
        if (QFile::remove(backups[i].path)) {
            ++removed;
        }
    }
    return removed;
}

} // namespace opentalk::server
