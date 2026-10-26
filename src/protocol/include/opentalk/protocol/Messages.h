// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/protocol/Version.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QtGlobal>

#include <optional>
#include <variant>

namespace opentalk::protocol {

/// Frame message types. See docs/protocol/proposal.md for the registry.
enum class MessageType : quint16 {
    Hello = 0x0001,
    Welcome = 0x0002,
    Ping = 0x0003,
    Pong = 0x0004,
    Goodbye = 0x0005,
    Error = 0x0006,
};

/// Numeric error codes carried by Error and Goodbye messages. Values are part of the
/// wire format and must never be renumbered.
enum class ErrorCode : quint16 {
    None = 0,
    ProtocolViolation = 1,
    UnsupportedVersion = 2,
    UnknownMessage = 3,
    MalformedMessage = 4,
    RateLimited = 5,
    ServerFull = 6,
    Timeout = 7,
    ServerShuttingDown = 8,
    InternalError = 9,
};

/// Upper bounds for strings in session messages, in UTF-16 code units.
inline constexpr qsizetype kMaxNameLength = 128;
inline constexpr qsizetype kMaxDetailLength = 1024;

/// First message from the client: the protocol versions it speaks and who it is.
struct Hello {
    VersionRange versions;
    QString clientName;    ///< For example "OpenTalk".
    QString clientVersion; ///< For example "0.2.0".
    friend bool operator==(const Hello &, const Hello &) = default;
};

/// Server's answer to Hello: the chosen version and the limits the client must respect.
struct Welcome {
    quint16 version = 0;
    QString serverName;
    QString serverVersion;
    quint32 maxPayloadSize = 0;
    quint16 heartbeatIntervalSeconds = 0;
    friend bool operator==(const Welcome &, const Welcome &) = default;
};

/// Liveness check. The receiver answers with a Pong carrying the same token.
struct Ping {
    quint64 token = 0;
    friend bool operator==(const Ping &, const Ping &) = default;
};

struct Pong {
    quint64 token = 0;
    friend bool operator==(const Pong &, const Pong &) = default;
};

/// Orderly end of a session. The sender closes the connection after sending it.
struct Goodbye {
    ErrorCode reason = ErrorCode::None;
    friend bool operator==(const Goodbye &, const Goodbye &) = default;
};

/// Reports a problem. `detail` is short English text for logs; user-facing text is
/// built from `code` by the receiver.
struct Error {
    ErrorCode code = ErrorCode::InternalError;
    std::optional<quint64> requestId;
    QString detail;
    friend bool operator==(const Error &, const Error &) = default;
};

using Message = std::variant<Hello, Welcome, Ping, Pong, Goodbye, Error>;

MessageType messageType(const Message &message);

/// Encodes a message as a complete frame (header + CBOR payload).
QByteArray encodeMessage(const Message &message);

/// Why a frame could not be turned into a message.
struct MessageDecodeFailure {
    ErrorCode code = ErrorCode::MalformedMessage; ///< UnknownMessage or MalformedMessage.
    QString detail;
};

using MessageDecodeResult = std::variant<Message, MessageDecodeFailure>;

/// Decodes and validates one frame payload. Never trusts its input: the payload must be
/// exactly one CBOR map, required fields must be present with the right types and
/// ranges, and strings must be within limits and free of control characters. Unknown
/// map keys are ignored so newer peers stay compatible.
MessageDecodeResult decodeMessage(quint16 type, QByteArrayView payload);

QString toString(ErrorCode code);

} // namespace opentalk::protocol
