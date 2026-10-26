// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QDateTime>
#include <QLoggingCategory>
#include <QString>
#include <QtGlobal>

#include <optional>

namespace opentalk::logging {

/// Lowest severity that is written to the log.
enum class Level { Debug, Info, Warning };

/// Parses the persisted setting value ("debug", "info", "warning").
std::optional<Level> levelFromString(const QString &text);
QString levelToString(Level level);

struct Options {
    /// Log file to append to. Empty means log to stderr only.
    QString filePath;
    Level minimumLevel = Level::Info;
    bool alsoWriteToStderr = true;
};

/// Formats one log line. Exposed so the format can be unit tested.
/// Example: "2026-10-26T12:00:00.000Z WARN  opentalk.storage: message"
QString formatLine(QtMsgType type, const char *category, const QString &message, const QDateTime &timestampUtc);

/// Installs the OpenTalk message handler for the whole process.
/// Returns false if the log file could not be opened; stderr logging still works.
bool install(const Options &options);

/// Changes the minimum level at runtime (for example after settings change).
void setMinimumLevel(Level level);

/// Flushes and closes the log file and restores Qt's default handler.
void shutdown();

} // namespace opentalk::logging

// One category per component, so users can filter diagnostics by area.
Q_DECLARE_LOGGING_CATEGORY(lcApp)
Q_DECLARE_LOGGING_CATEGORY(lcUi)
Q_DECLARE_LOGGING_CATEGORY(lcStorage)
Q_DECLARE_LOGGING_CATEGORY(lcSecurity)
Q_DECLARE_LOGGING_CATEGORY(lcClient)
Q_DECLARE_LOGGING_CATEGORY(lcServer)
