// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/storage/SettingsSchema.h"

#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <functional>

namespace opentalk::storage {

/// What happened when a settings file was loaded.
struct LoadReport {
    enum class Status {
        CreatedDefaults,         ///< No file existed; defaults are in use.
        Loaded,                  ///< File loaded at the current schema version.
        Migrated,                ///< File was older; a backup was made and it was upgraded in memory.
        RecoveredFromCorruption, ///< File was unreadable; it was moved aside and defaults are in use.
        NewerVersionReadOnly,    ///< File came from a newer OpenTalk; it is left untouched.
        Failed,                  ///< Something went wrong; the store is read-only to protect the file.
    };

    Status status = Status::CreatedDefaults;
    /// Human-readable notes about values that were ignored or problems found.
    QStringList warnings;
    /// Path of a backup copy made during recovery or migration, if any.
    QString backupPath;
};

/// What happened when settings were imported from a file.
struct ImportReport {
    bool ok = false; ///< False when the file was not used at all; `error` says why.
    QString error;
    int changed = 0;     ///< How many settings now have a different value.
    QStringList skipped; ///< Human-readable notes about values that were not imported.
};

/// Loads, validates and atomically saves one JSON settings file described by a schema.
///
/// Guarantees:
/// - Values are always valid for the schema; invalid values on disk fall back to defaults.
/// - Unknown or invalid entries found on disk are kept and written back unchanged, so a
///   downgrade or a manual edit never silently loses data.
/// - Saving uses QSaveFile, so a crash mid-write cannot leave a truncated file.
/// - A file from a newer version, or one that could not be backed up, is never overwritten.
///
/// Thread ownership: a store belongs to the thread that created it (the GUI thread for the
/// client). It is not internally synchronised.
class SettingsStore : public QObject {
    Q_OBJECT

public:
    /// Upgrades the raw "values" object from version N to N + 1. Return false and set
    /// `error` if the data cannot be migrated.
    using Migration = std::function<bool(QJsonObject &values, QString &error)>;

    SettingsStore(SettingsSchema schema, QString filePath, QObject *parent = nullptr);

    /// Registers the migration that upgrades files at `fromVersion` to `fromVersion + 1`.
    void addMigration(int fromVersion, Migration migration);

    LoadReport load();
    bool save(QString *errorMessage = nullptr);

    /// Writes every setting this version knows, except `excludedKeys`, to `path` in the
    /// settings-file format, for moving settings to another computer or copy.
    bool exportTo(const QString &path, const QStringList &excludedKeys, QString *errorMessage) const;
    /// Applies the values in a file written by exportTo() (or a settings file), through
    /// setValue(), so the usual change notifications happen. Unknown, invalid and excluded
    /// values are skipped and listed in the report. Older formats are upgraded with the
    /// registered migrations; files from a newer OpenTalk are refused. Does not save.
    ImportReport importFrom(const QString &path, const QStringList &excludedKeys);

    QVariant value(const QString &key) const;
    /// Returns false (and changes nothing) if the key is unknown or the value is invalid.
    bool setValue(const QString &key, const QVariant &value);

    bool isReadOnly() const { return m_readOnly; }
    const SettingsSchema &schema() const { return m_schema; }
    QString filePath() const { return m_filePath; }

signals:
    void valueChanged(const QString &key, const QVariant &value);

private:
    LoadReport fail(LoadReport report, const QString &reason);

    SettingsSchema m_schema;
    QString m_filePath;
    QMap<int, Migration> m_migrations;
    QHash<QString, QVariant> m_values;
    /// Everything read from disk, including entries this version does not understand.
    QJsonObject m_rawValues;
    bool m_readOnly = false;
};

} // namespace opentalk::storage
