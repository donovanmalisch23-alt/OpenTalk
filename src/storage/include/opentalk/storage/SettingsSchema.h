// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QJsonValue>
#include <QList>
#include <QString>
#include <QVariant>

#include <functional>
#include <optional>

namespace opentalk::storage {

/// Value types a setting may have. Each maps to one JSON representation.
enum class SettingType {
    Bool,      ///< JSON true/false
    Int,       ///< JSON integer number
    String,    ///< JSON string
    ByteArray, ///< JSON string holding base64 (used for opaque window state)
    IntMap,    ///< JSON object of integers, as a QVariantMap of int (for example per-user volumes)
};

/// Describes one persisted setting: its key, type, default and validation rule.
struct SettingDefinition {
    QString key;
    SettingType type = SettingType::String;
    QVariant defaultValue;
    /// Optional extra rule applied after the type check (for example an allowed range).
    std::function<bool(const QVariant &)> extraValidation;
};

/// The full set of settings one settings file may contain, plus its format version.
class SettingsSchema {
public:
    explicit SettingsSchema(int version);

    int version() const { return m_version; }

    /// Adds a definition. Keys must be unique and the default must itself be valid.
    void add(SettingDefinition definition);

    const SettingDefinition *find(const QString &key) const;
    const QList<SettingDefinition> &definitions() const { return m_definitions; }

    /// True when `value` has the right type for `key` and passes its extra validation.
    bool isValid(const QString &key, const QVariant &value) const;

    /// Converts a JSON value from disk into a typed value, or nullopt if it does not fit.
    std::optional<QVariant> fromJson(const QString &key, const QJsonValue &json) const;
    QJsonValue toJson(const QString &key, const QVariant &value) const;

private:
    int m_version;
    QList<SettingDefinition> m_definitions;
};

} // namespace opentalk::storage
