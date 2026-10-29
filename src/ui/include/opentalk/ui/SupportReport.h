// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QDateTime>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace opentalk::storage {
class SettingsStore;
}

namespace opentalk::ui {

/// Everything a support file says, gathered by MainWindow. Kept apart from the window so
/// the wording and the privacy rules can be tested on their own.
struct SupportReportInput {
    QString version;
    QString qtVersion;
    QString tlsLibrary;
    QString operatingSystem;
    QString dataMode;
    QString dataFolder;
    QString connection; ///< State and voice path only: never the server's address.
    QString voiceStatus;
    QStringList inputDevices;
    QStringList outputDevices;
    QStringList plugins;
    QList<QPair<QString, QString>> settings; ///< Key and value as shown.
    QString log;                             ///< The end of the app log.
    /// The user's home folder. Wherever it appears (in folder names and the log) it is
    /// replaced by a placeholder, because it usually contains the person's name.
    QString homeFolder;
};

/// The plain-text support file: written to be read line by line with a screen reader.
QString buildSupportReport(const SupportReportInput &input, const QDateTime &createdUtc);

/// The settings as "key", "value" pairs for a support file; `privateKeys` show as left out.
QList<QPair<QString, QString>> describeSettings(const storage::SettingsStore &settings, const QStringList &privateKeys);

/// The last `maxLines` lines of a text file (reading at most its last 256 KB), or an empty
/// string if it cannot be read.
QString readLogTail(const QString &path, int maxLines);

} // namespace opentalk::ui
