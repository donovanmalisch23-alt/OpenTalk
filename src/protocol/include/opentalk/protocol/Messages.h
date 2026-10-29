// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/protocol/AdminMessages.h"
#include "opentalk/protocol/Version.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QString>
#include <QtGlobal>

#include <optional>
#include <variant>

namespace opentalk::protocol {

/// Frame message types. Values are part of the wire format; see
/// docs/protocol/proposal.md for the registry.
enum class MessageType : quint16 {
    // Session (0x0001-0x00FF)
    Hello = 0x0001,
    Welcome = 0x0002,
    Ping = 0x0003,
    Pong = 0x0004,
    Goodbye = 0x0005,
    Error = 0x0006,
    Authenticate = 0x0007,
    AuthResult = 0x0008,
    // Channels (0x0100-0x01FF)
    StateSnapshot = 0x0100,
    ChannelAdded = 0x0101,
    ChannelRemoved = 0x0102,
    ChannelUpdated = 0x0103,
    JoinChannel = 0x0110,
    CreateChannel = 0x0111,
    DeleteChannel = 0x0112,
    EditChannel = 0x0113,
    RequestDone = 0x0120,
    // Users and presence (0x0200-0x02FF)
    UserJoined = 0x0200,
    UserLeft = 0x0201,
    UserMoved = 0x0202,
    UserUpdated = 0x0203,
    SetStatus = 0x0210,
    // Text (0x0300-0x03FF)
    SendText = 0x0300,
    TextMessage = 0x0301,
    // Voice (0x0400-0x04FF)
    AudioSetup = 0x0400,
    AudioData = 0x0401,
    // Moderation and administration (0x0500-0x05FF)
    RolesChanged = 0x0500,
    CapabilitiesChanged = 0x0501,
    SanctionNotice = 0x0502,
    MoveUser = 0x0510,
    ApplySanction = 0x0511,
    RevokeSanction = 0x0512,
    ListRequest = 0x0513,
    ListResult = 0x0514,
    SetUserRole = 0x0515,
    ReportUser = 0x0516,
    ResolveReport = 0x0517,
    SubmitAppeal = 0x0518,
    DecideAppeal = 0x0519,
    ReadAudit = 0x051A,
    AuditPage = 0x051B,
    CreateAccount = 0x0520,
    DeleteAccount = 0x0521,
    SetAccountPassword = 0x0522,
    SaveRole = 0x0523,
    DeleteRole = 0x0524,
    AddRule = 0x0525,
    DeleteRule = 0x0526,
    ServerSettingsRequest = 0x0527,
    ServerSettingsResult = 0x0528,
    GetServerStats = 0x0529,
    ServerStatsResult = 0x052A,
    TransferOwnership = 0x052B,
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
    AuthenticationFailed = 10,
    NotAuthorized = 11,
    NotFound = 12,
    ChannelFull = 13,
    WrongChannelPassword = 14,
    InvalidRequest = 15,
    NameInUse = 16,
    Banned = 17, ///< Detail says until when and why.
    Kicked = 18, ///< In Goodbye: a moderator removed you.
    Muted = 19,  ///< The request needs a voice or text the user is muted for.
};

/// Role IDs. The built-in roles have these values; servers can define more roles with
/// IDs from 100 up, so any quint32 value is valid on the wire. What a role may do is
/// described by RoleInfo (in StateSnapshot and RolesChanged).
enum class Role : quint32 {
    Guest = 0, ///< Not logged in to an account.
    User = 1,  ///< Registered account.
    Admin = 2, ///< Administrator.
    Owner = 3, ///< The server's owner; exactly one account.
    Moderator = 4,
    Restricted = 5, ///< Can join channels only.
};

/// Bits in UserInfo::flags.
enum UserFlag : quint32 {
    VoiceMutedFlag = 0x1, ///< Muted by a moderator (cannot talk).
    TextMutedFlag = 0x2,  ///< Cannot send messages.
    RestrictedFlag = 0x4, ///< Under a restriction sanction.
    BotFlag = 0x8,        ///< The account is a bot (a program, not a person).
};

// Limits, in UTF-16 code units unless stated otherwise.
inline constexpr qsizetype kMaxNameLength = 128;       ///< Client and server software names.
inline constexpr qsizetype kMaxDetailLength = 1024;    ///< Error detail text.
inline constexpr qsizetype kMaxDisplayNameLength = 32; ///< User and guest names.
inline constexpr qsizetype kMaxStatusLength = 100;     ///< A user's status message.
inline constexpr qsizetype kMaxChannelNameLength = 64;
inline constexpr qsizetype kMaxTopicLength = 256;
inline constexpr qsizetype kMaxTextLength = 2000;       ///< One chat message.
inline constexpr qsizetype kMaxSecretLength = 1024;     ///< Passwords.
inline constexpr qsizetype kMaxSnapshotEntries = 10000; ///< Channels or users in one snapshot.
/// Largest payload a server may send. Clients accept up to this; servers accept only
/// the smaller limit they announce in Welcome.
inline constexpr quint32 kMaxServerPayloadSize = 1024 * 1024;
/// The root channel always exists and cannot be deleted.
inline constexpr quint32 kRootChannelId = 1;
/// Largest Opus packet (RFC 6716, section 3.4).
inline constexpr qsizetype kMaxAudioPayloadBytes = 1275;
/// Keys for the encrypted UDP voice channel (XChaCha20-Poly1305).
inline constexpr qsizetype kAudioKeyBytes = 32;

// ----------------------------------------------------------------- session messages

/// First message from the client: the protocol versions it speaks and who it is.
struct Hello {
    VersionRange versions;
    QString clientName;
    QString clientVersion;
    friend bool operator==(const Hello &, const Hello &) = default;
};

/// Server's answer to Hello: the chosen version and the limits the client must respect.
struct Welcome {
    quint16 version = 0;
    QString serverName;
    QString serverVersion;
    quint32 maxPayloadSize = 0;
    quint16 heartbeatIntervalSeconds = 0;
    bool guestsAllowed = false;
    friend bool operator==(const Welcome &, const Welcome &) = default;
};

/// Liveness check. The receiver answers with a Pong carrying the same token (63 bits).
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
/// built from `code` by the receiver. `requestId` is set when it answers a request.
struct Error {
    ErrorCode code = ErrorCode::InternalError;
    std::optional<quint64> requestId;
    QString detail;
    friend bool operator==(const Error &, const Error &) = default;
};

/// Logs in after Welcome: with an account and password, or as a guest with a nickname.
struct Authenticate {
    enum class Mode : quint8 { Account = 0, Guest = 1 };
    Mode mode = Mode::Account;
    QString name;     ///< Username (Account) or nickname (Guest).
    QString password; ///< Account mode only.
    friend bool operator==(const Authenticate &, const Authenticate &) = default;
};

/// Successful login. Followed immediately by a StateSnapshot.
struct AuthResult {
    quint32 userId = 0; ///< This session's user ID (valid until disconnect).
    QString displayName;
    Role role = Role::Guest;
    /// What this user may do server-wide (moderation::Capability values), so the client
    /// can offer only possible actions. The server checks every request regardless.
    QList<quint16> capabilities;
    /// Capabilities held only in some channels (for example a channel moderator's), so
    /// the client can offer those actions too.
    QList<quint16> channelCapabilities;
    friend bool operator==(const AuthResult &, const AuthResult &) = default;
};

// --------------------------------------------------------------- channels and users

struct ChannelInfo {
    quint32 id = 0;
    quint32 parentId = 0; ///< 0 only for the root channel.
    QString name;         ///< Empty only for the root channel.
    QString topic;
    bool persistent = false; ///< Survives restarts; temporary channels vanish when empty.
    bool hasPassword = false;
    quint16 maxUsers = 0; ///< 0 = unlimited.
    friend bool operator==(const ChannelInfo &, const ChannelInfo &) = default;
};

struct UserInfo {
    quint32 id = 0;
    QString name;
    Role role = Role::Guest;
    quint32 channelId = kRootChannelId;
    quint32 flags = 0; ///< UserFlag bits
    QString status;    ///< Optional status message (see SetStatus); empty when none.
    friend bool operator==(const UserInfo &, const UserInfo &) = default;
};

/// Everything a client needs to show the server, sent once after AuthResult.
struct StateSnapshot {
    QList<ChannelInfo> channels;
    QList<UserInfo> users;
    QList<RoleInfo> roles;
    friend bool operator==(const StateSnapshot &, const StateSnapshot &) = default;
};

struct ChannelAdded {
    ChannelInfo channel;
    friend bool operator==(const ChannelAdded &, const ChannelAdded &) = default;
};

/// A channel's name, topic, password or limit changed.
struct ChannelUpdated {
    ChannelInfo channel;
    friend bool operator==(const ChannelUpdated &, const ChannelUpdated &) = default;
};

struct ChannelRemoved {
    quint32 channelId = 0;
    friend bool operator==(const ChannelRemoved &, const ChannelRemoved &) = default;
};

/// Request: move to a channel. Answered by RequestDone or Error (with the request ID).
struct JoinChannel {
    quint64 requestId = 0;
    quint32 channelId = 0;
    QString password; ///< Empty if the channel has none.
    friend bool operator==(const JoinChannel &, const JoinChannel &) = default;
};

/// Request: create a channel. RequestDone carries the new channel's ID.
struct CreateChannel {
    quint64 requestId = 0;
    quint32 parentId = kRootChannelId;
    QString name;
    QString topic;
    bool persistent = false;
    QString password;
    quint16 maxUsers = 0;
    friend bool operator==(const CreateChannel &, const CreateChannel &) = default;
};

/// Request: delete a channel (administrators only).
struct DeleteChannel {
    quint64 requestId = 0;
    quint32 channelId = 0;
    friend bool operator==(const DeleteChannel &, const DeleteChannel &) = default;
};

/// Success answer to a request.
struct RequestDone {
    quint64 requestId = 0;
    std::optional<quint32> resultId; ///< For example the ID of a created channel.
    friend bool operator==(const RequestDone &, const RequestDone &) = default;
};

struct UserJoined {
    UserInfo user;
    friend bool operator==(const UserJoined &, const UserJoined &) = default;
};

struct UserLeft {
    quint32 userId = 0;
    friend bool operator==(const UserLeft &, const UserLeft &) = default;
};

struct UserMoved {
    quint32 userId = 0;
    quint32 channelId = 0;
    friend bool operator==(const UserMoved &, const UserMoved &) = default;
};

// --------------------------------------------------------------------------- text

enum class TextTarget : quint8 {
    Channel = 0, ///< targetId is a channel ID (the sender's current channel).
    User = 1,    ///< targetId is a user ID: a direct message.
    Server = 2,  ///< To everyone on the server (administrators only); targetId is 0.
};

/// Request: send a text message. The server echoes it back as a TextMessage.
struct SendText {
    quint64 requestId = 0;
    TextTarget target = TextTarget::Channel;
    quint32 targetId = 0;
    QString text;
    friend bool operator==(const SendText &, const SendText &) = default;
};

/// A delivered text message, stamped by the server.
struct TextMessage {
    TextTarget target = TextTarget::Channel;
    quint32 fromUserId = 0;
    quint32 targetId = 0;
    QString text;
    qint64 timestampMs = 0; ///< Server time, milliseconds since 1970-01-01 UTC.
    friend bool operator==(const TextMessage &, const TextMessage &) = default;
};

// -------------------------------------------------------------------------- voice

/// Kinds of audio a user can send. Values are part of the wire format.
enum class AudioStream : quint8 {
    Voice = 0,       ///< Microphone.
    Media = 1,       ///< A media file being streamed.
    SystemAudio = 2, ///< Shared computer audio.
};

/// Sent by the server after login: how to reach its UDP voice port and the keys that
/// protect it. Each session gets fresh random keys, one per direction. UDP is optional;
/// voice falls back to AudioData over this connection when UDP is blocked.
struct AudioSetup {
    quint16 udpPort = 0; ///< 0 = the server offers no UDP; use AudioData only.
    quint32 udpSessionId = 0;
    QByteArray clientToServerKey;
    QByteArray serverToClientKey;
    quint32 maxBitrate = 0; ///< Bits per second per stream the server forwards.
    friend bool operator==(const AudioSetup &, const AudioSetup &) = default;
};

/// One Opus packet (20 ms) carried over the TLS connection instead of UDP. From a
/// client, `userId` is 0; from the server it names the speaker.
struct AudioData {
    AudioStream stream = AudioStream::Voice;
    quint16 sequence = 0;
    quint32 userId = 0;
    QByteArray payload;
    friend bool operator==(const AudioData &, const AudioData &) = default;
};

using Message =
    std::variant<Hello, Welcome, Ping, Pong, Goodbye, Error, Authenticate, AuthResult, StateSnapshot, ChannelAdded,
                 ChannelRemoved, ChannelUpdated, JoinChannel, CreateChannel, DeleteChannel, EditChannel, RequestDone,
                 UserJoined, UserLeft, UserMoved, UserUpdated, SendText, TextMessage, AudioSetup, AudioData,
                 RolesChanged, CapabilitiesChanged, SanctionNotice, MoveUser, ApplySanction, RevokeSanction,
                 ListRequest, ListResult, SetUserRole, ReportUser, ResolveReport, SubmitAppeal, DecideAppeal, ReadAudit,
                 AuditPage, CreateAccount, DeleteAccount, SetAccountPassword, SaveRole, DeleteRole, AddRule, DeleteRule,
                 ServerSettingsRequest, ServerSettingsResult, GetServerStats, ServerStatsResult, TransferOwnership,
                 SetStatus>;

MessageType messageType(const Message &message);

/// Messages a client may send, and messages a server may send. A peer that sends a
/// type meant for the other direction is violating the protocol.
bool isClientToServer(MessageType type);
bool isServerToClient(MessageType type);

/// Encodes a message as a complete frame (header + CBOR payload). Returns an empty
/// array if the payload would exceed `maxPayloadSize`.
QByteArray encodeMessage(const Message &message, quint32 maxPayloadSize = kMaxServerPayloadSize);

/// Why a frame could not be turned into a message.
struct MessageDecodeFailure {
    ErrorCode code = ErrorCode::MalformedMessage; ///< UnknownMessage or MalformedMessage.
    QString detail;
};

using MessageDecodeResult = std::variant<Message, MessageDecodeFailure>;

/// Decodes and validates one frame payload. Never trusts its input: the payload must be
/// exactly one CBOR map, required fields must be present with the right types and
/// ranges, and text must be within limits and free of characters that could hide or
/// disguise it (control characters, and bidirectional overrides in chat text). Unknown
/// map keys are ignored so newer peers stay compatible.
MessageDecodeResult decodeMessage(quint16 type, QByteArrayView payload);

/// True if `text` is acceptable as a user or channel name: not blank, within
/// `maxLength`, and without control or invisible formatting characters.
bool isValidName(const QString &text, qsizetype maxLength);
/// True if `text` is acceptable as chat text (newlines and tabs allowed).
bool isValidChatText(const QString &text);

QString toString(ErrorCode code);
QString toString(MessageType type);

} // namespace opentalk::protocol
