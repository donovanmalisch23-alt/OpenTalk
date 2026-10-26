// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/protocol/Messages.h"

#include "opentalk/protocol/Frame.h"

#include <QCborMap>
#include <QCborStreamReader>
#include <QCborValue>

#include <limits>

namespace opentalk::protocol {
namespace {

// CBOR map keys. Small integers keep messages compact. Keys are scoped per message type.
namespace key {
// Hello
constexpr qint64 kMinVersion = 1;
constexpr qint64 kMaxVersion = 2;
constexpr qint64 kClientName = 3;
constexpr qint64 kClientVersion = 4;
// Welcome
constexpr qint64 kVersion = 1;
constexpr qint64 kServerName = 2;
constexpr qint64 kServerVersion = 3;
constexpr qint64 kMaxPayload = 4;
constexpr qint64 kHeartbeat = 5;
// Ping, Pong
constexpr qint64 kToken = 1;
// Goodbye, Error
constexpr qint64 kCode = 1;
constexpr qint64 kRequestId = 2;
constexpr qint64 kDetail = 3;
} // namespace key

/// Reads typed fields from a decoded CBOR map, remembering the first problem.
class FieldReader {
public:
    explicit FieldReader(const QCborMap &map)
        : m_map(map)
    {
    }

    bool ok() const { return !m_failure.has_value(); }
    QString failure() const { return m_failure.value_or(QString()); }

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

    QString text(qint64 k, const char *name, qsizetype maxLength, bool allowEmpty)
    {
        const QCborValue value = m_map.value(k);
        if (!value.isString()) {
            fail(name, "missing or not a text string");
            return {};
        }
        QString s = value.toString();
        if (s.size() > maxLength) {
            fail(name, "too long");
            return {};
        }
        if (!allowEmpty && s.trimmed().isEmpty()) {
            fail(name, "empty");
            return {};
        }
        for (const QChar c : s) {
            if (c.category() == QChar::Other_Control || c.category() == QChar::Other_Format) {
                fail(name, "contains control or formatting characters");
                return {};
            }
        }
        return s;
    }

private:
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
        if (v < 0 || static_cast<quint64>(v) > std::numeric_limits<Int>::max()) {
            fail(name, "out of range");
            return std::nullopt;
        }
        return static_cast<Int>(v);
    }

    void fail(const char *name, const char *problem)
    {
        if (!m_failure) {
            m_failure = QString::fromLatin1(name) + QLatin1String(": ") + QString::fromLatin1(problem);
        }
    }

    const QCborMap &m_map;
    std::optional<QString> m_failure;
};

QCborMap toCbor(const Hello &m)
{
    return {{key::kMinVersion, m.versions.minimum},
            {key::kMaxVersion, m.versions.maximum},
            {key::kClientName, m.clientName},
            {key::kClientVersion, m.clientVersion}};
}

QCborMap toCbor(const Welcome &m)
{
    return {{key::kVersion, m.version},
            {key::kServerName, m.serverName},
            {key::kServerVersion, m.serverVersion},
            {key::kMaxPayload, static_cast<qint64>(m.maxPayloadSize)},
            {key::kHeartbeat, m.heartbeatIntervalSeconds}};
}

// Tokens are limited to 63 bits so they survive the qint64 round trip in QCborValue.
QCborMap toCbor(const Ping &m)
{
    return {{key::kToken, static_cast<qint64>(m.token & 0x7fffffffffffffffULL)}};
}

QCborMap toCbor(const Pong &m)
{
    return {{key::kToken, static_cast<qint64>(m.token & 0x7fffffffffffffffULL)}};
}

QCborMap toCbor(const Goodbye &m)
{
    return {{key::kCode, static_cast<qint64>(m.reason)}};
}

QCborMap toCbor(const Error &m)
{
    QCborMap map{{key::kCode, static_cast<qint64>(m.code)}, {key::kDetail, m.detail.left(kMaxDetailLength)}};
    if (m.requestId) {
        map.insert(key::kRequestId, static_cast<qint64>(*m.requestId & 0x7fffffffffffffffULL));
    }
    return map;
}

bool isKnownErrorCode(quint16 code)
{
    return code <= static_cast<quint16>(ErrorCode::InternalError);
}

MessageDecodeResult malformed(const QString &detail)
{
    return MessageDecodeFailure{ErrorCode::MalformedMessage, detail};
}

} // namespace

MessageType messageType(const Message &message)
{
    struct Visitor {
        MessageType operator()(const Hello &) const { return MessageType::Hello; }
        MessageType operator()(const Welcome &) const { return MessageType::Welcome; }
        MessageType operator()(const Ping &) const { return MessageType::Ping; }
        MessageType operator()(const Pong &) const { return MessageType::Pong; }
        MessageType operator()(const Goodbye &) const { return MessageType::Goodbye; }
        MessageType operator()(const Error &) const { return MessageType::Error; }
    };
    return std::visit(Visitor{}, message);
}

QByteArray encodeMessage(const Message &message)
{
    const QCborMap map = std::visit([](const auto &m) { return toCbor(m); }, message);
    const QByteArray payload = map.toCborValue().toCbor();
    // Session messages are tiny; exceeding the limit would be a programming error.
    const std::optional<QByteArray> frame = encodeFrame(static_cast<quint16>(messageType(message)), payload);
    Q_ASSERT(frame.has_value());
    return frame.value_or(QByteArray());
}

MessageDecodeResult decodeMessage(quint16 type, QByteArrayView payload)
{
    switch (static_cast<MessageType>(type)) {
    case MessageType::Hello:
    case MessageType::Welcome:
    case MessageType::Ping:
    case MessageType::Pong:
    case MessageType::Goodbye:
    case MessageType::Error:
        break;
    default:
        return MessageDecodeFailure{ErrorCode::UnknownMessage,
                                    QStringLiteral("unknown message type 0x%1").arg(type, 4, 16, QLatin1Char('0'))};
    }

    QCborStreamReader reader(reinterpret_cast<const quint8 *>(payload.data()), payload.size());
    const QCborValue root = QCborValue::fromCbor(reader);
    if (reader.lastError() != QCborError::NoError) {
        return malformed(QStringLiteral("invalid CBOR: ") + reader.lastError().toString());
    }
    if (reader.currentOffset() != payload.size()) {
        return malformed(QStringLiteral("trailing bytes after the message"));
    }
    if (!root.isMap()) {
        return malformed(QStringLiteral("payload is not a map"));
    }
    const QCborMap map = root.toMap();
    FieldReader r(map);

    Message message;
    switch (static_cast<MessageType>(type)) {
    case MessageType::Hello: {
        Hello m;
        m.versions.minimum = r.integer<quint16>(key::kMinVersion, "minVersion");
        m.versions.maximum = r.integer<quint16>(key::kMaxVersion, "maxVersion");
        m.clientName = r.text(key::kClientName, "clientName", kMaxNameLength, false);
        m.clientVersion = r.text(key::kClientVersion, "clientVersion", kMaxNameLength, false);
        if (r.ok() && !m.versions.isValid()) {
            return malformed(QStringLiteral("version range is invalid"));
        }
        message = m;
        break;
    }
    case MessageType::Welcome: {
        Welcome m;
        m.version = r.integer<quint16>(key::kVersion, "version");
        m.serverName = r.text(key::kServerName, "serverName", kMaxNameLength, false);
        m.serverVersion = r.text(key::kServerVersion, "serverVersion", kMaxNameLength, false);
        m.maxPayloadSize = r.integer<quint32>(key::kMaxPayload, "maxPayloadSize");
        m.heartbeatIntervalSeconds = r.integer<quint16>(key::kHeartbeat, "heartbeatIntervalSeconds");
        if (r.ok() && (m.version == 0 || m.maxPayloadSize < 1024 || m.heartbeatIntervalSeconds == 0)) {
            return malformed(QStringLiteral("Welcome values out of range"));
        }
        message = m;
        break;
    }
    case MessageType::Ping:
        message = Ping{r.integer<quint64>(key::kToken, "token")};
        break;
    case MessageType::Pong:
        message = Pong{r.integer<quint64>(key::kToken, "token")};
        break;
    case MessageType::Goodbye: {
        const quint16 code = r.integer<quint16>(key::kCode, "code");
        // Unknown reasons from newer peers are reported generically, not rejected.
        message = Goodbye{isKnownErrorCode(code) ? static_cast<ErrorCode>(code) : ErrorCode::InternalError};
        break;
    }
    case MessageType::Error: {
        Error m;
        const quint16 code = r.integer<quint16>(key::kCode, "code");
        m.code = isKnownErrorCode(code) ? static_cast<ErrorCode>(code) : ErrorCode::InternalError;
        m.requestId = r.optionalInteger<quint64>(key::kRequestId, "requestId");
        m.detail = r.text(key::kDetail, "detail", kMaxDetailLength, true);
        message = m;
        break;
    }
    }
    if (!r.ok()) {
        return malformed(r.failure());
    }
    return message;
}

QString toString(ErrorCode code)
{
    switch (code) {
    case ErrorCode::None:
        return QStringLiteral("None");
    case ErrorCode::ProtocolViolation:
        return QStringLiteral("ProtocolViolation");
    case ErrorCode::UnsupportedVersion:
        return QStringLiteral("UnsupportedVersion");
    case ErrorCode::UnknownMessage:
        return QStringLiteral("UnknownMessage");
    case ErrorCode::MalformedMessage:
        return QStringLiteral("MalformedMessage");
    case ErrorCode::RateLimited:
        return QStringLiteral("RateLimited");
    case ErrorCode::ServerFull:
        return QStringLiteral("ServerFull");
    case ErrorCode::Timeout:
        return QStringLiteral("Timeout");
    case ErrorCode::ServerShuttingDown:
        return QStringLiteral("ServerShuttingDown");
    case ErrorCode::InternalError:
        return QStringLiteral("InternalError");
    }
    return QStringLiteral("Unknown");
}

} // namespace opentalk::protocol
