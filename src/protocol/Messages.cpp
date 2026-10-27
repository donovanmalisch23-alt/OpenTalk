// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/protocol/Messages.h"

#include "opentalk/protocol/Frame.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborStreamReader>
#include <QCborValue>

#include <limits>

namespace opentalk::protocol {
namespace {

// CBOR map keys. Small integers keep messages compact. Keys are scoped per message type
// (and per nested structure); see docs/protocol/proposal.md.
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
constexpr qint64 kGuestsAllowed = 6;
// Ping, Pong
constexpr qint64 kToken = 1;
// Goodbye, Error
constexpr qint64 kCode = 1;
constexpr qint64 kErrorRequestId = 2;
constexpr qint64 kDetail = 3;
// Authenticate
constexpr qint64 kAuthMode = 1;
constexpr qint64 kAuthName = 2;
constexpr qint64 kAuthPassword = 3;
// AuthResult
constexpr qint64 kResultUserId = 1;
constexpr qint64 kResultName = 2;
constexpr qint64 kResultRole = 3;
// ChannelInfo (also the top level of ChannelAdded)
constexpr qint64 kChId = 1;
constexpr qint64 kChParent = 2;
constexpr qint64 kChName = 3;
constexpr qint64 kChTopic = 4;
constexpr qint64 kChPersistent = 5;
constexpr qint64 kChHasPassword = 6;
constexpr qint64 kChMaxUsers = 7;
// UserInfo (also the top level of UserJoined)
constexpr qint64 kUsrId = 1;
constexpr qint64 kUsrName = 2;
constexpr qint64 kUsrRole = 3;
constexpr qint64 kUsrChannel = 4;
// StateSnapshot
constexpr qint64 kSnapChannels = 1;
constexpr qint64 kSnapUsers = 2;
// Requests: key 1 is always the request ID
constexpr qint64 kRequestId = 1;
constexpr qint64 kReqChannel = 2;   // JoinChannel, DeleteChannel
constexpr qint64 kReqPassword = 3;  // JoinChannel
constexpr qint64 kCreateParent = 2; // CreateChannel
constexpr qint64 kCreateName = 3;
constexpr qint64 kCreateTopic = 4;
constexpr qint64 kCreatePersistent = 5;
constexpr qint64 kCreatePassword = 6;
constexpr qint64 kCreateMaxUsers = 7;
constexpr qint64 kDoneResult = 2; // RequestDone
// ChannelRemoved, UserLeft, UserMoved
constexpr qint64 kSubjectId = 1;
constexpr qint64 kMovedChannel = 2;
// SendText
constexpr qint64 kSendTarget = 2;
constexpr qint64 kSendTargetId = 3;
constexpr qint64 kSendText = 4;
// TextMessage
constexpr qint64 kMsgTarget = 1;
constexpr qint64 kMsgFrom = 2;
constexpr qint64 kMsgTargetId = 3;
constexpr qint64 kMsgText = 4;
constexpr qint64 kMsgTime = 5;
} // namespace key

constexpr quint64 k63Bits = 0x7fffffffffffffffULL;

bool isBidiControl(char16_t c)
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

// ------------------------------------------------------------------------ encoding

QCborValue id63(quint64 v)
{
    return static_cast<qint64>(v & k63Bits);
}

QCborMap channelToCbor(const ChannelInfo &c)
{
    return {{key::kChId, static_cast<qint64>(c.id)},
            {key::kChParent, static_cast<qint64>(c.parentId)},
            {key::kChName, c.name},
            {key::kChTopic, c.topic},
            {key::kChPersistent, c.persistent},
            {key::kChHasPassword, c.hasPassword},
            {key::kChMaxUsers, c.maxUsers}};
}

QCborMap userToCbor(const UserInfo &u)
{
    return {{key::kUsrId, static_cast<qint64>(u.id)},
            {key::kUsrName, u.name},
            {key::kUsrRole, static_cast<int>(u.role)},
            {key::kUsrChannel, static_cast<qint64>(u.channelId)}};
}

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
            {key::kHeartbeat, m.heartbeatIntervalSeconds},
            {key::kGuestsAllowed, m.guestsAllowed}};
}

QCborMap toCbor(const Ping &m)
{
    return {{key::kToken, id63(m.token)}};
}

QCborMap toCbor(const Pong &m)
{
    return {{key::kToken, id63(m.token)}};
}

QCborMap toCbor(const Goodbye &m)
{
    return {{key::kCode, static_cast<qint64>(m.reason)}};
}

QCborMap toCbor(const Error &m)
{
    QCborMap map{{key::kCode, static_cast<qint64>(m.code)}, {key::kDetail, m.detail.left(kMaxDetailLength)}};
    if (m.requestId) {
        map.insert(key::kErrorRequestId, id63(*m.requestId));
    }
    return map;
}

QCborMap toCbor(const Authenticate &m)
{
    QCborMap map{{key::kAuthMode, static_cast<int>(m.mode)}, {key::kAuthName, m.name}};
    if (m.mode == Authenticate::Mode::Account) {
        map.insert(key::kAuthPassword, m.password);
    }
    return map;
}

QCborMap toCbor(const AuthResult &m)
{
    return {{key::kResultUserId, static_cast<qint64>(m.userId)},
            {key::kResultName, m.displayName},
            {key::kResultRole, static_cast<int>(m.role)}};
}

QCborMap toCbor(const StateSnapshot &m)
{
    QCborArray channels;
    for (const ChannelInfo &c : m.channels) {
        channels.append(channelToCbor(c));
    }
    QCborArray users;
    for (const UserInfo &u : m.users) {
        users.append(userToCbor(u));
    }
    return {{key::kSnapChannels, channels}, {key::kSnapUsers, users}};
}

QCborMap toCbor(const ChannelAdded &m)
{
    return channelToCbor(m.channel);
}

QCborMap toCbor(const ChannelRemoved &m)
{
    return {{key::kSubjectId, static_cast<qint64>(m.channelId)}};
}

QCborMap toCbor(const JoinChannel &m)
{
    QCborMap map{{key::kRequestId, id63(m.requestId)}, {key::kReqChannel, static_cast<qint64>(m.channelId)}};
    if (!m.password.isEmpty()) {
        map.insert(key::kReqPassword, m.password);
    }
    return map;
}

QCborMap toCbor(const CreateChannel &m)
{
    QCborMap map{{key::kRequestId, id63(m.requestId)},
                 {key::kCreateParent, static_cast<qint64>(m.parentId)},
                 {key::kCreateName, m.name},
                 {key::kCreateTopic, m.topic},
                 {key::kCreatePersistent, m.persistent},
                 {key::kCreateMaxUsers, m.maxUsers}};
    if (!m.password.isEmpty()) {
        map.insert(key::kCreatePassword, m.password);
    }
    return map;
}

QCborMap toCbor(const DeleteChannel &m)
{
    return {{key::kRequestId, id63(m.requestId)}, {key::kReqChannel, static_cast<qint64>(m.channelId)}};
}

QCborMap toCbor(const RequestDone &m)
{
    QCborMap map{{key::kRequestId, id63(m.requestId)}};
    if (m.resultId) {
        map.insert(key::kDoneResult, static_cast<qint64>(*m.resultId));
    }
    return map;
}

QCborMap toCbor(const UserJoined &m)
{
    return userToCbor(m.user);
}

QCborMap toCbor(const UserLeft &m)
{
    return {{key::kSubjectId, static_cast<qint64>(m.userId)}};
}

QCborMap toCbor(const UserMoved &m)
{
    return {{key::kSubjectId, static_cast<qint64>(m.userId)}, {key::kMovedChannel, static_cast<qint64>(m.channelId)}};
}

QCborMap toCbor(const SendText &m)
{
    return {{key::kRequestId, id63(m.requestId)},
            {key::kSendTarget, static_cast<int>(m.target)},
            {key::kSendTargetId, static_cast<qint64>(m.targetId)},
            {key::kSendText, m.text}};
}

QCborMap toCbor(const TextMessage &m)
{
    return {{key::kMsgTarget, static_cast<int>(m.target)},
            {key::kMsgFrom, static_cast<qint64>(m.fromUserId)},
            {key::kMsgTargetId, static_cast<qint64>(m.targetId)},
            {key::kMsgText, m.text},
            {key::kMsgTime, m.timestampMs}};
}

// ------------------------------------------------------------------------ decoding

bool isKnownErrorCode(quint16 code)
{
    return code <= static_cast<quint16>(ErrorCode::NameInUse);
}

ErrorCode errorCodeFrom(quint16 code)
{
    // Unknown codes from newer peers are reported generically, not rejected.
    return isKnownErrorCode(code) ? static_cast<ErrorCode>(code) : ErrorCode::InternalError;
}

ChannelInfo readChannel(FieldReader &r)
{
    ChannelInfo c;
    c.id = r.integer<quint32>(key::kChId, "channel.id");
    c.parentId = r.integer<quint32>(key::kChParent, "channel.parentId");
    // The root channel has an empty name; every other channel needs a real one.
    if (c.id == kRootChannelId) {
        c.name = r.plainText(key::kChName, "channel.name", kMaxChannelNameLength, true);
    } else {
        c.name = r.name(key::kChName, "channel.name", kMaxChannelNameLength);
    }
    c.topic = r.plainText(key::kChTopic, "channel.topic", kMaxTopicLength, true);
    c.persistent = r.boolean(key::kChPersistent, "channel.persistent", true);
    c.hasPassword = r.boolean(key::kChHasPassword, "channel.hasPassword", true);
    c.maxUsers = r.integer<quint16>(key::kChMaxUsers, "channel.maxUsers");
    if (r.ok() && (c.id == 0 || (c.id == kRootChannelId) != (c.parentId == 0) || c.id == c.parentId)) {
        r.fail(QStringLiteral("channel: invalid id or parent"));
    }
    return c;
}

UserInfo readUser(FieldReader &r)
{
    UserInfo u;
    u.id = r.integer<quint32>(key::kUsrId, "user.id");
    u.name = r.name(key::kUsrName, "user.name", kMaxDisplayNameLength);
    u.role = r.enumeration<Role>(key::kUsrRole, "user.role", Role::Admin);
    u.channelId = r.integer<quint32>(key::kUsrChannel, "user.channelId");
    if (r.ok() && (u.id == 0 || u.channelId == 0)) {
        r.fail(QStringLiteral("user: invalid id"));
    }
    return u;
}

MessageDecodeResult malformed(const QString &detail)
{
    return MessageDecodeFailure{ErrorCode::MalformedMessage, detail};
}

bool isKnownType(quint16 type)
{
    switch (static_cast<MessageType>(type)) {
    case MessageType::Hello:
    case MessageType::Welcome:
    case MessageType::Ping:
    case MessageType::Pong:
    case MessageType::Goodbye:
    case MessageType::Error:
    case MessageType::Authenticate:
    case MessageType::AuthResult:
    case MessageType::StateSnapshot:
    case MessageType::ChannelAdded:
    case MessageType::ChannelRemoved:
    case MessageType::JoinChannel:
    case MessageType::CreateChannel:
    case MessageType::DeleteChannel:
    case MessageType::RequestDone:
    case MessageType::UserJoined:
    case MessageType::UserLeft:
    case MessageType::UserMoved:
    case MessageType::SendText:
    case MessageType::TextMessage:
        return true;
    }
    return false;
}

} // namespace

bool isValidName(const QString &text, qsizetype maxLength)
{
    if (text.size() > maxLength || text.trimmed().isEmpty() || text.trimmed() != text) {
        return false;
    }
    for (const QChar c : text) {
        if (c.category() == QChar::Other_Control || c.category() == QChar::Other_Format) {
            return false;
        }
    }
    return true;
}

bool isValidChatText(const QString &text)
{
    if (text.size() > kMaxTextLength || text.trimmed().isEmpty()) {
        return false;
    }
    for (const QChar c : text) {
        if (c == QLatin1Char('\n') || c == QLatin1Char('\t')) {
            continue;
        }
        // Other formatting characters such as the zero-width joiner stay allowed,
        // because emoji sequences need them.
        if (c.category() == QChar::Other_Control || isBidiControl(c.unicode())) {
            return false;
        }
    }
    return true;
}

MessageType messageType(const Message &message)
{
    struct Visitor {
        MessageType operator()(const Hello &) const { return MessageType::Hello; }
        MessageType operator()(const Welcome &) const { return MessageType::Welcome; }
        MessageType operator()(const Ping &) const { return MessageType::Ping; }
        MessageType operator()(const Pong &) const { return MessageType::Pong; }
        MessageType operator()(const Goodbye &) const { return MessageType::Goodbye; }
        MessageType operator()(const Error &) const { return MessageType::Error; }
        MessageType operator()(const Authenticate &) const { return MessageType::Authenticate; }
        MessageType operator()(const AuthResult &) const { return MessageType::AuthResult; }
        MessageType operator()(const StateSnapshot &) const { return MessageType::StateSnapshot; }
        MessageType operator()(const ChannelAdded &) const { return MessageType::ChannelAdded; }
        MessageType operator()(const ChannelRemoved &) const { return MessageType::ChannelRemoved; }
        MessageType operator()(const JoinChannel &) const { return MessageType::JoinChannel; }
        MessageType operator()(const CreateChannel &) const { return MessageType::CreateChannel; }
        MessageType operator()(const DeleteChannel &) const { return MessageType::DeleteChannel; }
        MessageType operator()(const RequestDone &) const { return MessageType::RequestDone; }
        MessageType operator()(const UserJoined &) const { return MessageType::UserJoined; }
        MessageType operator()(const UserLeft &) const { return MessageType::UserLeft; }
        MessageType operator()(const UserMoved &) const { return MessageType::UserMoved; }
        MessageType operator()(const SendText &) const { return MessageType::SendText; }
        MessageType operator()(const TextMessage &) const { return MessageType::TextMessage; }
    };
    return std::visit(Visitor{}, message);
}

bool isClientToServer(MessageType type)
{
    switch (type) {
    case MessageType::Hello:
    case MessageType::Ping:
    case MessageType::Pong:
    case MessageType::Goodbye:
    case MessageType::Error:
    case MessageType::Authenticate:
    case MessageType::JoinChannel:
    case MessageType::CreateChannel:
    case MessageType::DeleteChannel:
    case MessageType::SendText:
        return true;
    default:
        return false;
    }
}

bool isServerToClient(MessageType type)
{
    switch (type) {
    case MessageType::Welcome:
    case MessageType::Ping:
    case MessageType::Pong:
    case MessageType::Goodbye:
    case MessageType::Error:
    case MessageType::AuthResult:
    case MessageType::StateSnapshot:
    case MessageType::ChannelAdded:
    case MessageType::ChannelRemoved:
    case MessageType::RequestDone:
    case MessageType::UserJoined:
    case MessageType::UserLeft:
    case MessageType::UserMoved:
    case MessageType::TextMessage:
        return true;
    default:
        return false;
    }
}

QByteArray encodeMessage(const Message &message, quint32 maxPayloadSize)
{
    const QCborMap map = std::visit([](const auto &m) { return toCbor(m); }, message);
    const QByteArray payload = map.toCborValue().toCbor();
    return encodeFrame(static_cast<quint16>(messageType(message)), payload, maxPayloadSize).value_or(QByteArray());
}

MessageDecodeResult decodeMessage(quint16 type, QByteArrayView payload)
{
    if (!isKnownType(type)) {
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
        m.clientName = r.name(key::kClientName, "clientName", kMaxNameLength);
        m.clientVersion = r.name(key::kClientVersion, "clientVersion", kMaxNameLength);
        if (r.ok() && !m.versions.isValid()) {
            return malformed(QStringLiteral("version range is invalid"));
        }
        message = m;
        break;
    }
    case MessageType::Welcome: {
        Welcome m;
        m.version = r.integer<quint16>(key::kVersion, "version");
        m.serverName = r.name(key::kServerName, "serverName", kMaxNameLength);
        m.serverVersion = r.name(key::kServerVersion, "serverVersion", kMaxNameLength);
        m.maxPayloadSize = r.integer<quint32>(key::kMaxPayload, "maxPayloadSize");
        m.heartbeatIntervalSeconds = r.integer<quint16>(key::kHeartbeat, "heartbeatIntervalSeconds");
        m.guestsAllowed = r.boolean(key::kGuestsAllowed, "guestsAllowed", false);
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
    case MessageType::Goodbye:
        message = Goodbye{errorCodeFrom(r.integer<quint16>(key::kCode, "code"))};
        break;
    case MessageType::Error: {
        Error m;
        m.code = errorCodeFrom(r.integer<quint16>(key::kCode, "code"));
        m.requestId = r.optionalInteger<quint64>(key::kErrorRequestId, "requestId");
        m.detail = r.plainText(key::kDetail, "detail", kMaxDetailLength, true);
        message = m;
        break;
    }
    case MessageType::Authenticate: {
        Authenticate m;
        m.mode = r.enumeration<Authenticate::Mode>(key::kAuthMode, "mode", Authenticate::Mode::Guest);
        m.name = r.name(key::kAuthName, "name", kMaxDisplayNameLength);
        if (m.mode == Authenticate::Mode::Account) {
            m.password = r.secret(key::kAuthPassword, "password", true);
        }
        message = m;
        break;
    }
    case MessageType::AuthResult: {
        AuthResult m;
        m.userId = r.integer<quint32>(key::kResultUserId, "userId");
        m.displayName = r.name(key::kResultName, "displayName", kMaxDisplayNameLength);
        m.role = r.enumeration<Role>(key::kResultRole, "role", Role::Admin);
        if (r.ok() && m.userId == 0) {
            return malformed(QStringLiteral("userId must not be 0"));
        }
        message = m;
        break;
    }
    case MessageType::StateSnapshot: {
        StateSnapshot m;
        const QCborArray channels = r.array(key::kSnapChannels, "channels", kMaxSnapshotEntries);
        const QCborArray users = r.array(key::kSnapUsers, "users", kMaxSnapshotEntries);
        for (qsizetype i = 0; r.ok() && i < channels.size(); ++i) {
            const QCborMap entry = channels.at(i).toMap();
            FieldReader sub(entry);
            m.channels.append(readChannel(sub));
            if (!channels.at(i).isMap() || !sub.ok()) {
                return malformed(sub.ok() ? QStringLiteral("channel entry is not a map") : sub.failure());
            }
        }
        for (qsizetype i = 0; r.ok() && i < users.size(); ++i) {
            const QCborMap entry = users.at(i).toMap();
            FieldReader sub(entry);
            m.users.append(readUser(sub));
            if (!users.at(i).isMap() || !sub.ok()) {
                return malformed(sub.ok() ? QStringLiteral("user entry is not a map") : sub.failure());
            }
        }
        message = m;
        break;
    }
    case MessageType::ChannelAdded:
        message = ChannelAdded{readChannel(r)};
        break;
    case MessageType::ChannelRemoved:
        message = ChannelRemoved{r.integer<quint32>(key::kSubjectId, "channelId")};
        break;
    case MessageType::JoinChannel: {
        JoinChannel m;
        m.requestId = r.integer<quint64>(key::kRequestId, "requestId");
        m.channelId = r.integer<quint32>(key::kReqChannel, "channelId");
        m.password = r.secret(key::kReqPassword, "password", false);
        message = m;
        break;
    }
    case MessageType::CreateChannel: {
        CreateChannel m;
        m.requestId = r.integer<quint64>(key::kRequestId, "requestId");
        m.parentId = r.integer<quint32>(key::kCreateParent, "parentId");
        m.name = r.name(key::kCreateName, "name", kMaxChannelNameLength);
        m.topic = r.plainText(key::kCreateTopic, "topic", kMaxTopicLength, false);
        m.persistent = r.boolean(key::kCreatePersistent, "persistent", false);
        m.password = r.secret(key::kCreatePassword, "password", false);
        m.maxUsers = r.optionalInteger<quint16>(key::kCreateMaxUsers, "maxUsers").value_or(0);
        message = m;
        break;
    }
    case MessageType::DeleteChannel:
        message = DeleteChannel{r.integer<quint64>(key::kRequestId, "requestId"),
                                r.integer<quint32>(key::kReqChannel, "channelId")};
        break;
    case MessageType::RequestDone:
        message = RequestDone{r.integer<quint64>(key::kRequestId, "requestId"),
                              r.optionalInteger<quint32>(key::kDoneResult, "resultId")};
        break;
    case MessageType::UserJoined:
        message = UserJoined{readUser(r)};
        break;
    case MessageType::UserLeft:
        message = UserLeft{r.integer<quint32>(key::kSubjectId, "userId")};
        break;
    case MessageType::UserMoved:
        message = UserMoved{r.integer<quint32>(key::kSubjectId, "userId"),
                            r.integer<quint32>(key::kMovedChannel, "channelId")};
        break;
    case MessageType::SendText: {
        SendText m;
        m.requestId = r.integer<quint64>(key::kRequestId, "requestId");
        m.target = r.enumeration<TextTarget>(key::kSendTarget, "target", TextTarget::Server);
        m.targetId = r.integer<quint32>(key::kSendTargetId, "targetId");
        m.text = r.chatText(key::kSendText, "text");
        message = m;
        break;
    }
    case MessageType::TextMessage: {
        TextMessage m;
        m.target = r.enumeration<TextTarget>(key::kMsgTarget, "target", TextTarget::Server);
        m.fromUserId = r.integer<quint32>(key::kMsgFrom, "fromUserId");
        m.targetId = r.integer<quint32>(key::kMsgTargetId, "targetId");
        m.text = r.chatText(key::kMsgText, "text");
        m.timestampMs = r.integer<qint64>(key::kMsgTime, "timestamp");
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
    case ErrorCode::AuthenticationFailed:
        return QStringLiteral("AuthenticationFailed");
    case ErrorCode::NotAuthorized:
        return QStringLiteral("NotAuthorized");
    case ErrorCode::NotFound:
        return QStringLiteral("NotFound");
    case ErrorCode::ChannelFull:
        return QStringLiteral("ChannelFull");
    case ErrorCode::WrongChannelPassword:
        return QStringLiteral("WrongChannelPassword");
    case ErrorCode::InvalidRequest:
        return QStringLiteral("InvalidRequest");
    case ErrorCode::NameInUse:
        return QStringLiteral("NameInUse");
    }
    return QStringLiteral("Unknown");
}

QString toString(MessageType type)
{
    return QStringLiteral("0x%1").arg(static_cast<quint16>(type), 4, 16, QLatin1Char('0'));
}

} // namespace opentalk::protocol
