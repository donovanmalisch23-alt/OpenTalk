// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <optional>

namespace opentalk::moderation {

/// One permission. Values are stored in databases and sent over the network: never
/// renumber them. Names (for example "user.kick") are used in configuration and logs.
enum class Capability : quint16 {
    ChannelJoin = 1,
    ChannelCreateTemporary = 2,
    ChannelCreatePersistent = 3,
    ChannelEdit = 4,
    ChannelDelete = 5,
    ChannelBypassRestrictions = 6, ///< Join despite channel passwords and user limits.
    TextSend = 10,                 ///< Messages to the channel you are in.
    TextPrivate = 11,
    BroadcastSend = 12, ///< Messages to the whole server.
    VoiceTransmit = 20,
    MediaTransmit = 21, ///< Streaming media files and computer audio.
    UserMove = 30,
    UserWarn = 31,
    UserMute = 32,
    UserKick = 33,
    UserBanTemporary = 34,
    UserBanPermanent = 35,
    UserBanAddress = 36, ///< Bans that also block an IP address.
    SanctionRevoke = 37,
    ReportReview = 40,
    AppealDecide = 41,
    AuditRead = 42,
    RoleAssign = 50,
    RoleManage = 51,    ///< Create, change and delete roles and permission rules.
    AccountManage = 52, ///< Create and delete accounts, reset passwords.
    ServerConfigure = 53,
    ServerMonitor = 54,
    OwnerTransfer = 100, ///< Owner only; can never be granted.
};

QString capabilityName(Capability capability);
std::optional<Capability> capabilityFromName(const QString &name);
std::optional<Capability> capabilityFromValue(quint16 value);
QList<Capability> allCapabilities();
/// Capabilities only the owner has, which no role or rule can grant.
bool isOwnerOnly(Capability capability);
/// Short English description for administration screens.
QString capabilityDescription(Capability capability);

using RoleId = quint32;

/// Built-in roles. Their IDs match the roles of protocol version 1 (0-2) so existing
/// accounts keep their meaning.
namespace roles {
inline constexpr RoleId kGuest = 0;
inline constexpr RoleId kUser = 1;
inline constexpr RoleId kAdministrator = 2;
inline constexpr RoleId kOwner = 3;
inline constexpr RoleId kModerator = 4;
inline constexpr RoleId kRestricted = 5;
/// Roles created by administrators get IDs from here on.
inline constexpr RoleId kFirstCustom = 100;
inline constexpr int kOwnerRank = 100;
inline constexpr int kAdministratorRank = 90;
} // namespace roles

struct RoleDefinition {
    RoleId id = 0;
    QString name;
    /// Higher ranks may act on lower ranks only (strictly lower).
    int rank = 0;
    QSet<Capability> grants;
    bool builtIn = false;
    friend bool operator==(const RoleDefinition &, const RoleDefinition &) = default;
};

/// The default roles of a new server.
QList<RoleDefinition> builtInRoles();

/// An allow or deny for one capability at one place (the whole server or a channel and
/// everything below it), for one account or everyone holding one role.
struct PermissionRule {
    enum class Subject : quint8 { Account = 0, Role = 1 };
    quint64 id = 0;
    quint32 channelId = 0; ///< 0 = the whole server.
    Subject subject = Subject::Role;
    qint64 subjectId = 0; ///< Account ID or role ID.
    Capability capability = Capability::ChannelJoin;
    bool allow = true;
    friend bool operator==(const PermissionRule &, const PermissionRule &) = default;
};

/// Who is asking.
struct Actor {
    qint64 accountId = 0; ///< 0 for guests.
    RoleId role = roles::kGuest;
};

struct Decision {
    /// What decided: the owner rule, a rule for the account, a rule for the role, the
    /// role's own grants, or nothing (default deny).
    enum class Source { Owner, OwnerOnly, AccountRule, RoleRule, RoleGrant, Default };
    bool allowed = false;
    QString reason; ///< Why, in English, for administrators ("denied by a rule on channel 1004 for role Guest").
    Source source = Source::Default;
};

/// Decides whether an actor may use a capability somewhere. Pure logic; see
/// docs/security/moderation-model.md for the rules:
///
/// 1. The owner may do everything.
/// 2. Owner-only capabilities are denied to everyone else.
/// 3. From the channel up through its parents to the server level, the first level with
///    a rule that applies decides. Within a level, rules for the account come before
///    rules for its role; among rules of the same kind, deny beats allow.
/// 4. Otherwise the role's grants decide. 5. Otherwise deny.
class PermissionResolver {
public:
    void setRoles(const QList<RoleDefinition> &roles);
    void setRules(const QList<PermissionRule> &rules);
    const QList<RoleDefinition> &roles() const { return m_roles; }
    const QList<PermissionRule> &rules() const { return m_rules; }
    std::optional<RoleDefinition> role(RoleId id) const;

    /// `channelChain` lists the channel and its ancestors, nearest first (the root
    /// channel last). Empty means a server-wide question.
    Decision check(const Actor &actor, Capability capability, const QList<quint32> &channelChain = {}) const;
    bool allows(const Actor &actor, Capability capability, const QList<quint32> &channelChain = {}) const
    {
        return check(actor, capability, channelChain).allowed;
    }
    /// Everything the actor may do server-wide (for the client to enable its menus).
    QList<Capability> serverCapabilities(const Actor &actor) const;
    /// Capabilities the actor lacks server-wide but has in at least one channel through
    /// a channel rule (a channel moderator, for example). A hint for menus only: the
    /// answer for a particular channel still comes from check().
    QList<Capability> channelOnlyCapabilities(const Actor &actor) const;

    int rankOf(RoleId role) const;
    /// True if `actor` ranks strictly above `target` (the owner outranks everyone else).
    bool outranks(RoleId actor, RoleId target) const;

private:
    QList<RoleDefinition> m_roles;
    QList<PermissionRule> m_rules;
    QHash<RoleId, int> m_roleIndex;
};

} // namespace opentalk::moderation
