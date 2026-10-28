// SPDX-License-Identifier: GPL-3.0-only
// Private helpers shared by the protocol codec files: reading typed, validated fields
// from a decoded CBOR map.
#pragma once

#include "opentalk/protocol/Messages.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>

#include <limits>
#include <optional>

namespace opentalk::protocol::detail {

constexpr quint64 k63Bits = 0x7fffffffffffffffULL;

inline bool isBidiControl(char16_t c)
{
    return (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069);
}

/// Reads typed fields from a decoded CBOR map, remembering the first problem.
class FieldReader {
public:
    explicit FieldReader(const QCborMap &map)
        : m_map(map)
    {
    }

    const QCborMap &map() const { return m_map; }
    bool ok() const { return !m_failure.has_value(); }
    QString failure() const { return m_failure.value_or(QString()); }
    void fail(const QString &message)
    {
        if (!m_failure) {
            m_failure = message;
        }
    }

    template <typename Int> Int integer(qint64 k, const char *name)
    {
        return checkedInteger<Int>(k, name).value_or(Int{});
    }

    template <typename Int> std::optional<Int> optionalInteger(qint64 k, const char *name)
    {
        if (!m_map.contains(k)) {
            return std::nullopt;
        }
        return checkedInteger<Int>(k, name);
    }

    template <typename Enum> Enum enumeration(qint64 k, const char *name, Enum maxValue)
    {
        const auto v = checkedInteger<quint8>(k, name);
        if (v && *v > static_cast<quint8>(maxValue)) {
            fail(name, "unknown value");
        }
        return static_cast<Enum>(v.value_or(0));
    }

    bool boolean(qint64 k, const char *name, bool required)
    {
        const QCborValue value = m_map.value(k);
        if (value.isBool()) {
            return value.toBool();
        }
        if (required || !value.isUndefined()) {
            fail(name, required ? "missing or not a boolean" : "not a boolean");
        }
        return false;
    }

    /// A name: not blank, no control or formatting characters.
    QString name(qint64 k, const char *field, qsizetype maxLength)
    {
        const QString s = rawText(k, field, maxLength, true);
        if (ok() && !isValidName(s, maxLength)) {
            fail(field, "blank, or contains control or formatting characters");
        }
        return s;
    }

    /// Free text that may be empty: no control or formatting characters.
    QString plainText(qint64 k, const char *field, qsizetype maxLength, bool required)
    {
        if (!required && !m_map.contains(k)) {
            return {};
        }
        const QString s = rawText(k, field, maxLength, true);
        for (const QChar c : s) {
            if (c.category() == QChar::Other_Control || c.category() == QChar::Other_Format) {
                fail(field, "contains control or formatting characters");
                break;
            }
        }
        return s;
    }

    QString chatText(qint64 k, const char *field)
    {
        const QString s = rawText(k, field, kMaxTextLength, true);
        if (ok() && !isValidChatText(s)) {
            fail(field, "blank, or contains control or bidirectional override characters");
        }
        return s;
    }

    /// Text with no character restrictions (passwords).
    QString secret(qint64 k, const char *field, bool required)
    {
        if (!required && !m_map.contains(k)) {
            return {};
        }
        const QString s = rawText(k, field, kMaxSecretLength, !required);
        return s;
    }

    QByteArray bytes(qint64 k, const char *field, qsizetype minLength, qsizetype maxLength)
    {
        const QCborValue value = m_map.value(k);
        if (!value.isByteArray()) {
            fail(field, "missing or not a byte string");
            return {};
        }
        QByteArray b = value.toByteArray();
        if (b.size() < minLength || b.size() > maxLength) {
            fail(field, "wrong length");
            return {};
        }
        return b;
    }

    QCborArray array(qint64 k, const char *field, qsizetype maxCount)
    {
        const QCborValue value = m_map.value(k);
        if (!value.isArray()) {
            fail(field, "missing or not an array");
            return {};
        }
        QCborArray a = value.toArray();
        if (a.size() > maxCount) {
            fail(field, "too many entries");
            return {};
        }
        return a;
    }

private:
    QString rawText(qint64 k, const char *field, qsizetype maxLength, bool allowEmpty)
    {
        const QCborValue value = m_map.value(k);
        if (!value.isString()) {
            fail(field, "missing or not a text string");
            return {};
        }
        QString s = value.toString();
        if (s.size() > maxLength) {
            fail(field, "too long");
            return {};
        }
        if (!allowEmpty && s.isEmpty()) {
            fail(field, "empty");
        }
        return s;
    }

    template <typename Int> std::optional<Int> checkedInteger(qint64 k, const char *name)
    {
        const QCborValue value = m_map.value(k);
        if (!value.isInteger()) {
            fail(name, "missing or not an integer");
            return std::nullopt;
        }
        // Unsigned 64-bit values above INT64_MAX arrive as non-integers in QCborValue,
        // so every accepted value fits in qint64 here.
        const qint64 v = value.toInteger();
        if (v < 0 || static_cast<quint64>(v) > static_cast<quint64>(std::numeric_limits<Int>::max())) {
            fail(name, "out of range");
            return std::nullopt;
        }
        return static_cast<Int>(v);
    }

    void fail(const char *name, const char *problem)
    {
        fail(QString::fromLatin1(name) + QLatin1String(": ") + QString::fromLatin1(problem));
    }

    const QCborMap &m_map;
    std::optional<QString> m_failure;
};

inline QCborValue id63(quint64 v)
{
    return static_cast<qint64>(v & k63Bits);
}

} // namespace opentalk::protocol::detail
