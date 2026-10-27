// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>

namespace opentalk::storage {

/// How the data directory was chosen.
enum class DataMode {
    /// Per-user location from the operating system (for example %LOCALAPPDATA% on Windows).
    Installed,
    /// A marker file next to the executable keeps all data beside the application.
    Portable,
    /// The user passed --data-dir on the command line.
    Custom,
};

/// The documented on-disk layout for client data. See docs/architecture/data-layout.md.
class AppPaths {
public:
    /// File that switches OpenTalk into portable mode when placed next to the executable.
    static QString portableMarkerFileName();

    /// Decides where data lives. `overrideDir` wins, then the portable marker in
    /// `applicationDir`, then the standard per-user location.
    static AppPaths resolve(const QString &applicationDir, const QString &overrideDir = {});

    DataMode mode() const { return m_mode; }
    QString dataRoot() const { return m_dataRoot; }
    QString configDir() const;
    QString logDir() const;
    QString clientSettingsFile() const;
    QString serverProfilesFile() const;
    QString clientLogFile() const;

    /// Creates the directories that must exist before the application writes to them.
    bool ensureDirectories(QString *errorMessage = nullptr) const;

private:
    AppPaths(DataMode mode, QString dataRoot);

    DataMode m_mode;
    QString m_dataRoot;
};

QString toString(DataMode mode);

} // namespace opentalk::storage
