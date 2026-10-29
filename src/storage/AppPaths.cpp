// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/storage/AppPaths.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <utility>

namespace opentalk::storage {

QString AppPaths::portableMarkerFileName()
{
    return QStringLiteral("OpenTalk.portable");
}

AppPaths::AppPaths(DataMode mode, QString dataRoot)
    : m_mode(mode)
    , m_dataRoot(std::move(dataRoot))
{
}

AppPaths AppPaths::resolve(const QString &applicationDir, const QString &overrideDir)
{
    if (!overrideDir.isEmpty()) {
        return AppPaths(DataMode::Custom, QDir::cleanPath(QDir(overrideDir).absolutePath()));
    }
    const QDir appDir(applicationDir);
    if (!applicationDir.isEmpty() && QFileInfo::exists(appDir.filePath(portableMarkerFileName()))) {
        return AppPaths(DataMode::Portable, QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("data"))));
    }
    return AppPaths(DataMode::Installed,
                    QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)));
}

QString AppPaths::configDir() const
{
    return m_dataRoot + QStringLiteral("/config");
}

QString AppPaths::logDir() const
{
    return m_dataRoot + QStringLiteral("/logs");
}

QString AppPaths::clientSettingsFile() const
{
    return configDir() + QStringLiteral("/client-settings.json");
}

QString AppPaths::serverProfilesFile() const
{
    return configDir() + QStringLiteral("/server-profiles.json");
}

QString AppPaths::clientLogFile() const
{
    return logDir() + QStringLiteral("/opentalk-client.log");
}

bool AppPaths::ensureDirectories(QString *errorMessage) const
{
    for (const QString &dir : {configDir(), logDir()}) {
        if (!QDir().mkpath(dir)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("Could not create directory %1").arg(QDir::toNativeSeparators(dir));
            }
            return false;
        }
    }
    return true;
}

QString toString(DataMode mode)
{
    switch (mode) {
    case DataMode::Installed:
        return QStringLiteral("installed");
    case DataMode::Portable:
        return QStringLiteral("portable");
    case DataMode::Custom:
        return QStringLiteral("custom");
    }
    return QStringLiteral("unknown");
}

} // namespace opentalk::storage
