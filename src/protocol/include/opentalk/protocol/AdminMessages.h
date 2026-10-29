// SPDX-License-Identifier: GPL-3.0-only
// Moderation and administration messages (protocol 0x0500-0x05FF). Included by
// Messages.h; see docs/protocol/moderation.md for the rules behind them.
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>

#include <optional>

namespace opentalk::protocol {

inline constexpr qsizetype kMaxReasonLength = 500;
inline constexpr qsizetype kMaxRoleNameLength = 32;
inline constexpr qsizetype kMaxListEntries = 10000;
inline constexpr qsizetype kMaxAuditPage = 500;

/// A role as clients see it. Capabilities are the numeric values of
/// moderation::Capability.
struct RoleInfo {
    quint32 id = 0;
    QString name;
    qint32 rank = 0;
    QList<quint16> capabilities;
    bool builtIn = false;
    friend bool operator==(const RoleInfo &, const RoleInfo &) = default;
};

struct SanctionInfo {
    quint64 id = 0;
    quint8 type = 0;       ///< moderation::SanctionType
    quint32 channelId = 0; ///< 0 = server-wide
    QString targetName;
    QString targetAccount; ///< Empty for guests.
    QString targetAddress; ///< Only sent to people who may ban addresses.
    QString actorName;
    QString reason;
    qint64 createdMs = 0;
    qint64 expiresMs = 0; ///< 0 = no end
    quint8 state = 0;     ///< moderation::SanctionState
    QString resolvedBy;
    QString resolution;
    friend bool operator==(const SanctionInfo &, const SanctionInfo &) = default;
};

struct ReportInfo {
    quint64 id = 0;
    QString reporterName;
    QString targetName;
    QString targetAccount;
    QString reason;
    qint64 createdMs = 0;
    quint8 state = 0; ///< moderation::Report::State
    QString handledBy;
    QString resolution;
    friend bool operator==(const ReportInfo &, const ReportInfo &) = default;
};

struct AppealInfo {
    quint64 id = 0;
    quint64 sanctionId = 0;
    QString appellantName;
    QString text;
    qint64 createdMs = 0;
    quint8 state = 0; ///< moderation::Appeal::State
    QString decidedBy;
    QString decisionReason;
    friend bool operator==(const AppealInfo &, const AppealInfo &) = default;
};

struct AuditEntryInfo {
    quint64 sequence = 0;
    qint64 timeMs = 0;
    QString actorName;
    QString action;
    QString target;
    quint32 channelId = 0;
    QString reason;
    QString details;
    QByteArray hash;
    friend bool operator==(const AuditEntryInfo &, const AuditEntryInfo &) = default;
};

struct AccountInfo {
    QString name;
    quint32 roleId = 0;
    bool disabled = false;
    bool bot = false;
    friend bool operator==(const AccountInfo &, const AccountInfo &) = default;
};

struct RuleInfo {
    quint64 id = 0;
    quint32 channelId = 0; ///< 0 = whole server
    bool forAccount = false;
    QString account;    ///< When forAccount.
    quint32 roleId = 0; ///< When not forAccount.
    quint16 capability = 0;
    bool allow = true;
    friend bool operator==(const RuleInfo &, const RuleInfo &) = default;
};

struct ServerSettingsInfo {
    QString name;
    QString welcomeMessage; ///< Shown to everyone after login; empty for none.
    bool allowGuests = false;
    quint16 maxUsers = 200;
    quint32 maxVoiceBitrate = 128000;
    quint16 maxTemporaryBanDays = 30;
    QString contact; ///< How banned people can reach the server's team.
    friend bool operator==(const ServerSettingsInfo &, const ServerSettingsInfo &) = default;
};

struct ServerStatsInfo {
    qint64 uptimeSeconds = 0;
    quint32 onlineUsers = 0;
    quint32 connections = 0;
    quint32 channels = 0;
    quint64 voicePacketsIn = 0;
    quint64 voicePacketsOut = 0;
    quint64 voicePacketsRejected = 0;
    quint32 activeSanctions = 0;
    quint32 openReports = 0;
    quint32 openAppeals = 0;
    quint64 auditEntries = 0;
    bool auditIntact = true;
    // Since 1.0 (optional on the wire; 0 from older servers).
    quint64 bytesIn = 0;          ///< Data received since start: connections and voice over UDP.
    quint64 bytesOut = 0;         ///< Data sent since start.
    quint64 failedLogins = 0;     ///< Wrong or refused logins since start.
    quint64 connectionErrors = 0; ///< Connections ended by protocol errors, time-outs or flooding.
    friend bool operator==(const ServerStatsInfo &, const ServerStatsInfo &) = default;
};

// ------------------------------------------------------------------ server to client

/// A user's role, flags (for example a mute) or status message changed. Always carries
/// the complete current values.
struct UserUpdated {
    quint32 userId = 0;
    quint32 roleId = 0;
    quint32 flags = 0; ///< UserFlag bits
    QString status;    ///< The user's status message; empty when none.
    friend bool operator==(const UserUpdated &, const UserUpdated &) = default;
};

/// Request: set your own status message, shown to everyone next to your name (for
/// example "back at 5"). Empty clears it. Answered by RequestDone or Error.
struct SetStatus {
    quint64 requestId = 0;
    QString status; ///< Up to kMaxStatusLength characters, no control characters.
    friend bool operator==(const SetStatus &, const SetStatus &) = default;
};

/// The server's roles changed; replaces the client's list.
struct RolesChanged {
    QList<RoleInfo> roles;
    friend bool operator==(const RolesChanged &, const RolesChanged &) = default;
};

/// What this user may now do server-wide, and only in some channels (after a role or
/// rule change). See AuthResult.
struct CapabilitiesChanged {
    QList<quint16> capabilities;
    QList<quint16> channelCapabilities;
    friend bool operator==(const CapabilitiesChanged &, const CapabilitiesChanged &) = default;
};

/// Tells a user about a sanction on them, or that one was lifted.
struct SanctionNotice {
    SanctionInfo sanction;
    friend bool operator==(const SanctionNotice &, const SanctionNotice &) = default;
};

// ------------------------------------------------------------------------- requests
// Every request starts with a request ID and is answered by RequestDone, Error, or one
// of the list messages below carrying the same ID.

struct EditChannel {
    quint64 requestId = 0;
    quint32 channelId = 0;
    QString name;
    QString topic;
    quint16 maxUsers = 0;
    bool changePassword = false;
    QString password; ///< When changePassword: the new one, or empty to remove it.
    friend bool operator==(const EditChannel &, const EditChannel &) = default;
};

struct MoveUser {
    quint64 requestId = 0;
    quint32 userId = 0;
    quint32 channelId = 0;
    friend bool operator==(const MoveUser &, const MoveUser &) = default;
};

/// Warn, mute, kick, ban or restrict someone online (userId) or an account (account).
struct ApplySanction {
    quint64 requestId = 0;
    quint32 userId = 0; ///< Online target; 0 when `account` is used.
    QString account;
    quint8 type = 0;             ///< moderation::SanctionType
    quint32 channelId = 0;       ///< 0 = server-wide
    quint32 durationSeconds = 0; ///< 0 = until lifted
    QString reason;
    bool banAddress = false;
    friend bool operator==(const ApplySanction &, const ApplySanction &) = default;
};

struct RevokeSanction {
    quint64 requestId = 0;
    quint64 sanctionId = 0;
    QString reason;
    friend bool operator==(const RevokeSanction &, const RevokeSanction &) = default;
};

/// Kinds of list a client can ask for.
enum class ListKind : quint8 {
    Sanctions = 0, ///< Sanctions (active unless includeClosed); your own if you are not a moderator.
    Reports = 1,
    Appeals = 2, ///< All for those who decide appeals; your own otherwise.
    Accounts = 3,
    Rules = 4,
};

struct ListRequest {
    quint64 requestId = 0;
    ListKind kind = ListKind::Sanctions;
    bool includeClosed = false;
    friend bool operator==(const ListRequest &, const ListRequest &) = default;
};

/// The answer to ListRequest; only the list of the requested kind is filled.
struct ListResult {
    quint64 requestId = 0;
    ListKind kind = ListKind::Sanctions;
    QList<SanctionInfo> sanctions;
    QList<ReportInfo> reports;
    QList<AppealInfo> appeals;
    QList<AccountInfo> accounts;
    QList<RuleInfo> rules;
    friend bool operator==(const ListResult &, const ListResult &) = default;
};

struct SetUserRole {
    quint64 requestId = 0;
    QString account;
    quint32 roleId = 0;
    friend bool operator==(const SetUserRole &, const SetUserRole &) = default;
};

struct ReportUser {
    quint64 requestId = 0;
    quint32 userId = 0;
    QString reason;
    friend bool operator==(const ReportUser &, const ReportUser &) = default;
};

struct ResolveReport {
    quint64 requestId = 0;
    quint64 reportId = 0;
    quint8 state = 1; ///< ActionTaken or Dismissed
    QString resolution;
    friend bool operator==(const ResolveReport &, const ResolveReport &) = default;
};

struct SubmitAppeal {
    quint64 requestId = 0;
    quint64 sanctionId = 0;
    QString text;
    friend bool operator==(const SubmitAppeal &, const SubmitAppeal &) = default;
};

struct DecideAppeal {
    quint64 requestId = 0;
    quint64 appealId = 0;
    quint8 decision = 1; ///< Upheld, Reduced or Overturned
    QString reason;
    quint32 newDurationSeconds = 0; ///< For Reduced: the new length counted from the original start.
    friend bool operator==(const DecideAppeal &, const DecideAppeal &) = default;
};

/// Reads the audit log backwards: entries before `beforeSequence` (0 = newest).
struct ReadAudit {
    quint64 requestId = 0;
    quint64 beforeSequence = 0;
    quint16 limit = 100;
    friend bool operator==(const ReadAudit &, const ReadAudit &) = default;
};

struct AuditPage {
    quint64 requestId = 0;
    QList<AuditEntryInfo> entries; ///< Newest first.
    bool chainIntact = true;       ///< Result of the server's full check.
    quint64 firstBadSequence = 0;
    friend bool operator==(const AuditPage &, const AuditPage &) = default;
};

struct CreateAccount {
    quint64 requestId = 0;
    QString name;
    QString password;
    quint32 roleId = 1;
    bool bot = false; ///< The account is for a program (shown as "bot" to everyone).
    friend bool operator==(const CreateAccount &, const CreateAccount &) = default;
};

struct DeleteAccount {
    quint64 requestId = 0;
    QString name;
    friend bool operator==(const DeleteAccount &, const DeleteAccount &) = default;
};

struct SetAccountPassword {
    quint64 requestId = 0;
    QString name;
    QString password;
    friend bool operator==(const SetAccountPassword &, const SetAccountPassword &) = default;
};

/// Creates (id 0) or changes a role. RequestDone carries the role ID.
struct SaveRole {
    quint64 requestId = 0;
    RoleInfo role;
    friend bool operator==(const SaveRole &, const SaveRole &) = default;
};

struct DeleteRole {
    quint64 requestId = 0;
    quint32 roleId = 0;
    friend bool operator==(const DeleteRole &, const DeleteRole &) = default;
};

/// Adds a permission rule; RequestDone carries its ID.
struct AddRule {
    quint64 requestId = 0;
    RuleInfo rule;
    friend bool operator==(const AddRule &, const AddRule &) = default;
};

struct DeleteRule {
    quint64 requestId = 0;
    quint64 ruleId = 0;
    friend bool operator==(const DeleteRule &, const DeleteRule &) = default;
};

/// Asks for the server settings (`settings` empty) or changes them.
struct ServerSettingsRequest {
    quint64 requestId = 0;
    std::optional<ServerSettingsInfo> settings;
    friend bool operator==(const ServerSettingsRequest &, const ServerSettingsRequest &) = default;
};

struct ServerSettingsResult {
    quint64 requestId = 0;
    ServerSettingsInfo settings;
    friend bool operator==(const ServerSettingsResult &, const ServerSettingsResult &) = default;
};

struct GetServerStats {
    quint64 requestId = 0;
    friend bool operator==(const GetServerStats &, const GetServerStats &) = default;
};

struct ServerStatsResult {
    quint64 requestId = 0;
    ServerStatsInfo stats;
    friend bool operator==(const ServerStatsResult &, const ServerStatsResult &) = default;
};

struct TransferOwnership {
    quint64 requestId = 0;
    QString account;
    friend bool operator==(const TransferOwnership &, const TransferOwnership &) = default;
};

} // namespace opentalk::protocol
