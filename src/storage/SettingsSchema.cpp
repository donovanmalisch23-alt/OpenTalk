// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/storage/SettingsSchema.h"

#include <QByteArray>
#include <QJsonObject>
#include <QMetaType>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace opentalk::storage {
namespace {

bool hasType(const QVariant &value, SettingType type)
{
    switch (type) {
    case SettingType::Bool:
        return value.typeId() == QMetaType::Bool;
    case SettingType::Int:
        return value.typeId() == QMetaType::Int;
    case SettingType::String:
        return value.typeId() == QMetaType::QString;
    case SettingType::ByteArray:
        return value.typeId() == QMetaType::QByteArray;
    case SettingType::IntMap: {
        if (value.typeId() != QMetaType::QVariantMap) {
            return false;
        }
        const QVariantMap map = value.toMap();
        return std::all_of(map.cbegin(), map.cend(), [](const QVariant &v) { return v.typeId() == QMetaType::Int; });
    }
    }
    return false;
}

std::optional<int> jsonToInt(const QJsonValue &json)
{
    if (!json.isDouble()) {
        return std::nullopt;
    }
    const double number = json.toDouble();
    if (std::floor(number) != number || number < std::numeric_limits<int>::min() ||
        number > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }
    return static_cast<int>(number);
}

} // namespace

SettingsSchema::SettingsSchema(int version)
    : m_version(version)
{
}

void SettingsSchema::add(SettingDefinition definition)
{
    if (definition.key.isEmpty() || find(definition.key) != nullptr) {
        throw std::invalid_argument("setting key is empty or already defined");
    }
    m_definitions.append(std::move(definition));
    if (!isValid(m_definitions.last().key, m_definitions.last().defaultValue)) {
        m_definitions.removeLast();
        throw std::invalid_argument("setting default value does not satisfy its own definition");
    }
}

const SettingDefinition *SettingsSchema::find(const QString &key) const
{
    for (const SettingDefinition &definition : m_definitions) {
        if (definition.key == key) {
            return &definition;
        }
    }
    return nullptr;
}

bool SettingsSchema::isValid(const QString &key, const QVariant &value) const
{
    const SettingDefinition *definition = find(key);
    if (definition == nullptr || !hasType(value, definition->type)) {
        return false;
    }
    return !definition->extraValidation || definition->extraValidation(value);
}

std::optional<QVariant> SettingsSchema::fromJson(const QString &key, const QJsonValue &json) const
{
    const SettingDefinition *definition = find(key);
    if (definition == nullptr) {
        return std::nullopt;
    }
    QVariant value;
    switch (definition->type) {
    case SettingType::Bool:
        if (!json.isBool()) {
            return std::nullopt;
        }
        value = json.toBool();
        break;
    case SettingType::Int: {
        const std::optional<int> number = jsonToInt(json);
        if (!number) {
            return std::nullopt;
        }
        value = *number;
        break;
    }
    case SettingType::String:
        if (!json.isString()) {
            return std::nullopt;
        }
        value = json.toString();
        break;
    case SettingType::ByteArray: {
        if (!json.isString()) {
            return std::nullopt;
        }
        auto decoded =
            QByteArray::fromBase64Encoding(json.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded) {
            return std::nullopt;
        }
        value = *decoded;
        break;
    }
    case SettingType::IntMap: {
        if (!json.isObject()) {
            return std::nullopt;
        }
        QVariantMap map;
        const QJsonObject object = json.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            const std::optional<int> number = jsonToInt(it.value());
            if (!number) {
                return std::nullopt;
            }
            map.insert(it.key(), *number);
        }
        value = map;
        break;
    }
    }
    if (!isValid(key, value)) {
        return std::nullopt;
    }
    return value;
}

QJsonValue SettingsSchema::toJson(const QString &key, const QVariant &value) const
{
    const SettingDefinition *definition = find(key);
    if (definition == nullptr) {
        return {};
    }
    switch (definition->type) {
    case SettingType::Bool:
        return value.toBool();
    case SettingType::Int:
        return value.toInt();
    case SettingType::String:
        return value.toString();
    case SettingType::ByteArray:
        return QString::fromLatin1(value.toByteArray().toBase64());
    case SettingType::IntMap: {
        QJsonObject object;
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            object.insert(it.key(), it.value().toInt());
        }
        return object;
    }
    }
    return {};
}

} // namespace opentalk::storage
