// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/moderation/Permissions.h"

#include <algorithm>
#include <utility>

namespace opentalk::moderation {
namespace {

struct CapabilityInfo {
    Capability capability;
    const char *name;
    const char *description;
};

// The one table of capabilities: order is the order shown to administrators.
const CapabilityInfo kCapabilities[] = {
    {Capability::ChannelJoin, "channel.join", "Join channels"},
    {Capability::ChannelCreateTemporary, "channel.create.temporary", "Create temporary channels"},
    {Capability::ChannelCreatePersistent, "channel.create.persistent", "Create permanent channels"},
    {Capability::ChannelEdit, "channel.edit", "Change channel names, topics, passwords and limits"},
    {Capability::ChannelDelete, "channel.delete", "Delete channels"},
    {Capability::ChannelBypassRestrictions, "channel.bypass", "Enter channels without their password or user limit"},
    {Capability::TextSend, "text.send", "Send messages to the channel"},
    {Capability::TextPrivate, "text.private", "Send private messages"},
    {Capability::BroadcastSend, "broadcast.send", "Send messages to everyone on the server"},
    {Capability::VoiceTransmit, "voice.transmit", "Talk"},
    {Capability::MediaTransmit, "media.transmit", "Stream media files and computer audio"},
    {Capability::UserMove, "user.move", "Move people to other channels"},
    {Capability::UserWarn, "user.warn", "Warn people"},
    {Capability::UserMute, "user.mute", "Mute people"},
    {Capability::UserKick, "user.kick", "Kick people"},
    {Capability::UserBanTemporary, "user.ban.temporary", "Ban people for a limited time"},
    {Capability::UserBanPermanent, "user.ban.permanent", "Ban people permanently"},
    {Capability::UserBanAddress, "user.ban.address", "Ban IP addresses"},
    {Capability::SanctionRevoke, "sanction.revoke", "Lift sanctions early"},
    {Capability::ReportReview, "report.review", "Review reports"},
    {Capability::AppealDecide, "appeal.decide", "Decide appeals"},
    {Capability::AuditRead, "audit.read", "Read the audit log"},
    {Capability::RoleAssign, "role.assign", "Give people roles"},
    {Capability::RoleManage, "role.manage", "Create and change roles and channel permissions"},
    {Capability::AccountManage, "account.manage", "Create and delete accounts and reset passwords"},
    {Capability::ServerConfigure, "server.configure", "Change server settings"},
    {Capability::ServerMonitor, "server.monitor", "See server statistics"},
    {Capability::OwnerTransfer, "owner.transfer", "Transfer ownership (owner only)"},
};

const CapabilityInfo *infoFor(Capability capability)
{
    for (const CapabilityInfo &info : kCapabilities) {
        if (info.capability == capability) {
            return &info;
        }
    }
    return nullptr;
}

QSet<Capability> setOf(std::initializer_list<Capability> list)
{
    return QSet<Capability>(list.begin(), list.end());
}

} // namespace

QString capabilityName(Capability capability)
{
    const CapabilityInfo *info = infoFor(capability);
    return info != nullptr ? QString::fromLatin1(info->name) : QStringLiteral("unknown");
}

QString capabilityDescription(Capability capability)
{
    const CapabilityInfo *info = infoFor(capability);
    return info != nullptr ? QString::fromLatin1(info->description) : QString();
}

std::optional<Capability> capabilityFromName(const QString &name)
{
    for (const CapabilityInfo &info : kCapabilities) {
        if (name == QLatin1String(info.name)) {
            return info.capability;
        }
    }
    return std::nullopt;
}

std::optional<Capability> capabilityFromValue(quint16 value)
{
    for (const CapabilityInfo &info : kCapabilities) {
        if (static_cast<quint16>(info.capability) == value) {
            return info.capability;
        }
    }
    return std::nullopt;
}

QList<Capability> allCapabilities()
{
    QList<Capability> result;
    for (const CapabilityInfo &info : kCapabilities) {
        result.append(info.capability);
    }
    return result;
}

bool isOwnerOnly(Capability capability)
{
    return capability == Capability::OwnerTransfer;
}

QList<RoleDefinition> builtInRoles()
{
    using C = Capability;
    QSet<Capability> everything;
    for (Capability c : allCapabilities()) {
        if (!isOwnerOnly(c)) {
            everything.insert(c);
        }
    }
    const QSet<Capability> participant =
        setOf({C::ChannelJoin, C::TextSend, C::TextPrivate, C::VoiceTransmit, C::MediaTransmit});
    QSet<Capability> user = participant;
    user.insert(C::ChannelCreateTemporary);
    QSet<Capability> moderator = user;
    moderator.unite(setOf({C::UserMove, C::UserWarn, C::UserMute, C::UserKick, C::UserBanTemporary, C::SanctionRevoke,
                           C::ReportReview, C::AppealDecide, C::AuditRead, C::ServerMonitor}));
    return {
        {roles::kOwner, QStringLiteral("Owner"), roles::kOwnerRank, everything, true},
        {roles::kAdministrator, QStringLiteral("Administrator"), roles::kAdministratorRank, everything, true},
        {roles::kModerator, QStringLiteral("Moderator"), 50, moderator, true},
        {roles::kUser, QStringLiteral("Registered user"), 10, user, true},
        {roles::kGuest, QStringLiteral("Guest"), 5,
         setOf({C::ChannelJoin, C::TextSend, C::TextPrivate, C::VoiceTransmit}), true},
        {roles::kRestricted, QStringLiteral("Restricted"), 1, setOf({C::ChannelJoin}), true},
    };
}

void PermissionResolver::setRoles(const QList<RoleDefinition> &roles)
{
    m_roles = roles;
    m_roleIndex.clear();
    for (int i = 0; i < m_roles.size(); ++i) {
        m_roleIndex.insert(m_roles[i].id, i);
    }
}

void PermissionResolver::setRules(const QList<PermissionRule> &rules)
{
    m_rules = rules;
}

std::optional<RoleDefinition> PermissionResolver::role(RoleId id) const
{
    const auto it = m_roleIndex.constFind(id);
    return it != m_roleIndex.cend() ? std::optional<RoleDefinition>(m_roles[*it]) : std::nullopt;
}

int PermissionResolver::rankOf(RoleId role) const
{
    if (role == roles::kOwner) {
        return roles::kOwnerRank;
    }
    const auto definition = this->role(role);
    return definition ? definition->rank : 0;
}

bool PermissionResolver::outranks(RoleId actor, RoleId target) const
{
    if (target == roles::kOwner) {
        return false;
    }
    return actor == roles::kOwner || rankOf(actor) > rankOf(target);
}

Decision PermissionResolver::check(const Actor &actor, Capability capability, const QList<quint32> &channelChain) const
{
    if (actor.role == roles::kOwner) {
        return {true, QStringLiteral("the owner may do everything"), Decision::Source::Owner};
    }
    if (isOwnerOnly(capability)) {
        return {false, QStringLiteral("only the owner may do this"), Decision::Source::OwnerOnly};
    }
    const auto roleName = [this](RoleId id) {
        const auto definition = role(id);
        return definition ? definition->name : QStringLiteral("role %1").arg(id);
    };
    // Most specific level first: the channel, its parents, then the whole server.
    QList<quint32> levels = channelChain;
    levels.append(0);
    // At each level, rules for the account are more specific than rules for its role.
    const auto decide = [&](quint32 level, PermissionRule::Subject subject) -> std::optional<Decision> {
        bool matched = false;
        bool denied = false;
        for (const PermissionRule &rule : m_rules) {
            if (rule.channelId != level || rule.capability != capability || rule.subject != subject) {
                continue;
            }
            const bool applies = subject == PermissionRule::Subject::Account
                                     ? (actor.accountId != 0 && rule.subjectId == actor.accountId)
                                     : rule.subjectId == static_cast<qint64>(actor.role);
            if (applies) {
                matched = true;
                denied = denied || !rule.allow;
            }
        }
        if (!matched) {
            return std::nullopt;
        }
        const QString who = subject == PermissionRule::Subject::Account
                                ? QStringLiteral("this account")
                                : QStringLiteral("role %1").arg(roleName(actor.role));
        const QString where = level == 0 ? QStringLiteral("the server") : QStringLiteral("channel %1").arg(level);
        return Decision{!denied,
                        QStringLiteral("%1 by a rule on %2 for %3")
                            .arg(denied ? QStringLiteral("denied") : QStringLiteral("allowed"), where, who),
                        subject == PermissionRule::Subject::Account ? Decision::Source::AccountRule
                                                                    : Decision::Source::RoleRule};
    };
    for (quint32 level : std::as_const(levels)) {
        if (auto decision = decide(level, PermissionRule::Subject::Account)) {
            return *decision;
        }
        if (auto decision = decide(level, PermissionRule::Subject::Role)) {
            return *decision;
        }
    }
    const auto definition = role(actor.role);
    if (definition && definition->grants.contains(capability)) {
        return {true, QStringLiteral("granted by role %1").arg(definition->name), Decision::Source::RoleGrant};
    }
    return {false, QStringLiteral("not granted to role %1").arg(roleName(actor.role)), Decision::Source::Default};
}

QList<Capability> PermissionResolver::serverCapabilities(const Actor &actor) const
{
    QList<Capability> result;
    for (Capability c : allCapabilities()) {
        if (allows(actor, c)) {
            result.append(c);
        }
    }
    return result;
}

QList<Capability> PermissionResolver::channelOnlyCapabilities(const Actor &actor) const
{
    QList<Capability> result;
    if (actor.role == roles::kOwner) {
        return result; // the owner already has everything server-wide
    }
    for (const PermissionRule &rule : m_rules) {
        if (rule.channelId == 0 || !rule.allow || result.contains(rule.capability)) {
            continue;
        }
        const bool applies = rule.subject == PermissionRule::Subject::Account
                                 ? (actor.accountId != 0 && rule.subjectId == actor.accountId)
                                 : rule.subjectId == static_cast<qint64>(actor.role);
        // The rule's own channel is the most specific level, so checking it alone gives
        // the answer there (an account rule on the same channel may still deny).
        if (applies && !allows(actor, rule.capability) && allows(actor, rule.capability, {rule.channelId})) {
            result.append(rule.capability);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

} // namespace opentalk::moderation
