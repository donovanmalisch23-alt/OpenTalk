// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/storage/SettingsStore.h"

#include "opentalk/logging/Logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

#include <utility>

namespace opentalk::storage {
namespace {

const QString kFormatKey = QStringLiteral("format");
const QString kFormatName = QStringLiteral("opentalk-settings");
const QString kVersionKey = QStringLiteral("schemaVersion");
const QString kValuesKey = QStringLiteral("values");

// Settings files are small; refuse anything absurd rather than reading it into memory.
constexpr qint64 kMaxSettingsFileBytes = 4 * 1024 * 1024;

QString timestampSuffix()
{
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"));
}

/// Copies or renames `path` to a fresh backup name. Returns the backup path, or empty on failure.
QString makeBackup(const QString &path, const QString &label, bool moveInsteadOfCopy)
{
    const QString base = path + QLatin1Char('.') + label + QLatin1Char('-') + timestampSuffix();
    QString candidate = base;
    for (int i = 1; QFileInfo::exists(candidate); ++i) {
        candidate = base + QLatin1Char('-') + QString::number(i);
    }
    const bool ok = moveInsteadOfCopy ? QFile::rename(path, candidate) : QFile::copy(path, candidate);
    return ok ? candidate : QString();
}

} // namespace

SettingsStore::SettingsStore(SettingsSchema schema, QString filePath, QObject *parent)
    : QObject(parent)
    , m_schema(std::move(schema))
    , m_filePath(std::move(filePath))
{
}

void SettingsStore::addMigration(int fromVersion, Migration migration)
{
    m_migrations.insert(fromVersion, std::move(migration));
}

LoadReport SettingsStore::fail(LoadReport report, const QString &reason)
{
    m_readOnly = true;
    report.status = LoadReport::Status::Failed;
    report.warnings.append(reason);
    qCWarning(lcStorage) << "Settings file" << m_filePath << "is read-only for this session:" << reason;
    return report;
}

LoadReport SettingsStore::load()
{
    LoadReport report;
    m_values.clear();
    m_rawValues = {};
    m_readOnly = false;

    QFile file(m_filePath);
    if (!file.exists()) {
        return report;
    }
    if (file.size() > kMaxSettingsFileBytes) {
        return fail(report, QStringLiteral("file is larger than %1 bytes").arg(kMaxSettingsFileBytes));
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(report, QStringLiteral("cannot open file: %1").arg(file.errorString()));
    }
    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    const QJsonObject root = document.object();
    const bool wellFormed = parseError.error == QJsonParseError::NoError && document.isObject() &&
                            root.value(kFormatKey).toString() == kFormatName && root.value(kVersionKey).isDouble() &&
                            root.value(kValuesKey).isObject();
    if (!wellFormed) {
        // Move the damaged file aside so the user (or support) can still inspect it.
        const QString backup = makeBackup(m_filePath, QStringLiteral("corrupt"), true);
        if (backup.isEmpty()) {
            return fail(report, QStringLiteral("file is not a valid settings file and could not be moved aside"));
        }
        report.status = LoadReport::Status::RecoveredFromCorruption;
        report.backupPath = backup;
        report.warnings.append(
            QStringLiteral("Settings file was damaged; defaults are in use. The old file was saved as %1")
                .arg(QDir::toNativeSeparators(backup)));
        qCWarning(lcStorage) << "Settings file was damaged and moved to" << backup;
        return report;
    }

    const int fileVersion = root.value(kVersionKey).toInt();
    QJsonObject rawValues = root.value(kValuesKey).toObject();

    if (fileVersion > m_schema.version()) {
        m_readOnly = true;
        report.status = LoadReport::Status::NewerVersionReadOnly;
        report.warnings.append(
            QStringLiteral("Settings were written by a newer OpenTalk (format %1, this version understands %2). "
                           "Defaults are in use and the file will not be changed.")
                .arg(fileVersion)
                .arg(m_schema.version()));
        return report;
    }

    if (fileVersion < m_schema.version()) {
        const QString backup = makeBackup(m_filePath, QStringLiteral("v%1-backup").arg(fileVersion), false);
        if (backup.isEmpty()) {
            return fail(report, QStringLiteral("could not back up the file before upgrading it"));
        }
        report.backupPath = backup;
        for (int version = fileVersion; version < m_schema.version(); ++version) {
            const auto migration = m_migrations.constFind(version);
            if (migration == m_migrations.constEnd()) {
                return fail(report, QStringLiteral("no upgrade path from settings format %1").arg(version));
            }
            QString error;
            if (!(*migration)(rawValues, error)) {
                return fail(report, QStringLiteral("upgrade from format %1 failed: %2").arg(version).arg(error));
            }
        }
        report.status = LoadReport::Status::Migrated;
    } else {
        report.status = LoadReport::Status::Loaded;
    }

    m_rawValues = rawValues;
    for (auto it = rawValues.constBegin(); it != rawValues.constEnd(); ++it) {
        if (m_schema.find(it.key()) == nullptr) {
            continue; // Unknown keys are preserved in m_rawValues and written back.
        }
        const std::optional<QVariant> value = m_schema.fromJson(it.key(), it.value());
        if (value) {
            m_values.insert(it.key(), *value);
        } else {
            report.warnings.append(
                QStringLiteral("Ignored invalid value for \"%1\"; using the default.").arg(it.key()));
        }
    }
    for (const QString &warning : std::as_const(report.warnings)) {
        qCWarning(lcStorage).noquote() << warning;
    }
    return report;
}

bool SettingsStore::save(QString *errorMessage)
{
    auto setError = [errorMessage](const QString &message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        qCWarning(lcStorage).noquote() << message;
        return false;
    };
    if (m_readOnly) {
        return setError(QStringLiteral("Settings file %1 is read-only for this session; not saving.").arg(m_filePath));
    }

    QJsonObject values = m_rawValues;
    for (auto it = m_values.constBegin(); it != m_values.constEnd(); ++it) {
        values.insert(it.key(), m_schema.toJson(it.key(), it.value()));
    }
    QJsonObject root;
    root.insert(kFormatKey, kFormatName);
    root.insert(kVersionKey, m_schema.version());
    root.insert(kValuesKey, values);

    if (!QDir().mkpath(QFileInfo(m_filePath).absolutePath())) {
        return setError(QStringLiteral("Cannot create the folder for %1").arg(m_filePath));
    }
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return setError(QStringLiteral("Cannot write %1: %2").arg(m_filePath, file.errorString()));
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        return setError(QStringLiteral("Cannot save %1: %2").arg(m_filePath, file.errorString()));
    }
    m_rawValues = values;
    return true;
}

QVariant SettingsStore::value(const QString &key) const
{
    const auto it = m_values.constFind(key);
    if (it != m_values.constEnd()) {
        return *it;
    }
    const SettingDefinition *definition = m_schema.find(key);
    Q_ASSERT_X(definition != nullptr, "SettingsStore::value", "unknown settings key");
    return definition != nullptr ? definition->defaultValue : QVariant();
}

bool SettingsStore::setValue(const QString &key, const QVariant &value)
{
    if (!m_schema.isValid(key, value)) {
        qCWarning(lcStorage) << "Rejected invalid value for setting" << key;
        return false;
    }
    const bool changed = this->value(key) != value;
    m_values.insert(key, value);
    if (changed) {
        emit valueChanged(key, value);
    }
    return true;
}

} // namespace opentalk::storage
