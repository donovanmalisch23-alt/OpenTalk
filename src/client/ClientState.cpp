// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/client/ClientState.h"

#include <algorithm>

namespace opentalk::client {

using namespace protocol;

ClientState::ClientState(QObject *parent)
    : QObject(parent)
{
}

void ClientState::clear()
{
    m_self = {};
    m_channels.clear();
    m_users.clear();
    m_roles.clear();
}

bool ClientState::can(moderation::Capability capability) const
{
    return m_self.capabilities.contains(static_cast<quint16>(capability));
}

bool ClientState::canSomewhere(moderation::Capability capability) const
{
    return can(capability) || m_self.channelCapabilities.contains(static_cast<quint16>(capability));
}

bool ClientState::canModerate() const
{
    using C = moderation::Capability;
    for (C c : {C::UserMove, C::UserWarn, C::UserMute, C::UserKick, C::UserBanTemporary, C::SanctionRevoke,
                C::ReportReview, C::AppealDecide, C::AuditRead, C::RoleAssign, C::RoleManage, C::AccountManage,
                C::ServerConfigure, C::ServerMonitor}) {
        if (canSomewhere(c)) {
            return true;
        }
    }
    return false;
}

QString ClientState::roleName(quint32 roleId) const
{
    for (const RoleInfo &role : m_roles) {
        if (role.id == roleId) {
            return role.name;
        }
    }
    return tr("role %1").arg(roleId);
}

int ClientState::roleRank(quint32 roleId) const
{
    for (const RoleInfo &role : m_roles) {
        if (role.id == roleId) {
            return role.rank;
        }
    }
    return 0;
}

void ClientState::setSelf(const AuthResult &self)
{
    m_self = self;
}

quint32 ClientState::currentChannelId() const
{
    return m_users.value(m_self.userId).channelId;
}

bool ClientState::addChannel(const ChannelInfo &channel)
{
    const bool isRoot = channel.id == kRootChannelId;
    if (m_channels.contains(channel.id) || m_channels.size() >= kMaxEntries ||
        (!isRoot && !m_channels.contains(channel.parentId))) {
        return false;
    }
    m_channels.insert(channel.id, channel);
    return true;
}

bool ClientState::apply(const Message &message)
{
    if (const auto *snapshot = std::get_if<StateSnapshot>(&message)) {
        m_channels.clear();
        m_users.clear();
        for (const ChannelInfo &c : snapshot->channels) {
            addChannel(c);
        }
        if (!m_channels.contains(kRootChannelId)) {
            // A server must always send the root; supply one so the tree stays usable.
            m_channels.insert(kRootChannelId, ChannelInfo{kRootChannelId, 0, QString(), QString(), true, false, 0});
        }
        for (const UserInfo &u : snapshot->users) {
            if (m_users.size() < kMaxEntries && m_channels.contains(u.channelId)) {
                m_users.insert(u.id, u);
            }
        }
        m_roles = snapshot->roles.mid(0, 1000);
        emit reset();
        return true;
    }
    if (const auto *updated = std::get_if<ChannelUpdated>(&message)) {
        const auto it = m_channels.find(updated->channel.id);
        if (it != m_channels.end() && updated->channel.id != kRootChannelId &&
            updated->channel.parentId == it->parentId) {
            *it = updated->channel;
            emit channelUpdated(*it);
        }
        return true;
    }
    if (const auto *updated = std::get_if<UserUpdated>(&message)) {
        const auto it = m_users.find(updated->userId);
        if (it != m_users.end()) {
            const UserInfo before = *it;
            it->role = static_cast<Role>(updated->roleId);
            it->flags = updated->flags;
            it->status = updated->status;
            if (updated->userId == m_self.userId) {
                m_self.role = it->role;
            }
            emit userUpdated(*it, before);
        }
        return true;
    }
    if (const auto *roles = std::get_if<RolesChanged>(&message)) {
        m_roles = roles->roles.mid(0, 1000);
        emit rolesChanged();
        return true;
    }
    if (const auto *caps = std::get_if<CapabilitiesChanged>(&message)) {
        m_self.capabilities = caps->capabilities;
        m_self.channelCapabilities = caps->channelCapabilities;
        emit capabilitiesChanged();
        return true;
    }
    if (const auto *notice = std::get_if<SanctionNotice>(&message)) {
        emit sanctionNotice(notice->sanction);
        return true;
    }
    if (const auto *added = std::get_if<ChannelAdded>(&message)) {
        if (added->channel.id != kRootChannelId && addChannel(added->channel)) {
            emit channelAdded(added->channel);
        }
        return true;
    }
    if (const auto *removed = std::get_if<ChannelRemoved>(&message)) {
        const auto it = m_channels.constFind(removed->channelId);
        const bool hasChildren = std::any_of(m_channels.cbegin(), m_channels.cend(), [removed](const ChannelInfo &c) {
            return c.parentId == removed->channelId;
        });
        const bool occupied = std::any_of(m_users.cbegin(), m_users.cend(),
                                          [removed](const UserInfo &u) { return u.channelId == removed->channelId; });
        if (it != m_channels.cend() && removed->channelId != kRootChannelId && !hasChildren && !occupied) {
            const ChannelInfo old = *it;
            m_channels.remove(removed->channelId);
            emit channelRemoved(old);
        }
        return true;
    }
    if (const auto *joined = std::get_if<UserJoined>(&message)) {
        if (!m_users.contains(joined->user.id) && m_users.size() < kMaxEntries &&
            m_channels.contains(joined->user.channelId)) {
            m_users.insert(joined->user.id, joined->user);
            emit userJoined(joined->user);
        }
        return true;
    }
    if (const auto *left = std::get_if<UserLeft>(&message)) {
        if (left->userId != m_self.userId) {
            const auto it = m_users.constFind(left->userId);
            if (it != m_users.cend()) {
                const UserInfo old = *it;
                m_users.remove(left->userId);
                emit userLeft(old);
            }
        }
        return true;
    }
    if (const auto *moved = std::get_if<UserMoved>(&message)) {
        const auto it = m_users.find(moved->userId);
        if (it != m_users.end() && m_channels.contains(moved->channelId) && it->channelId != moved->channelId) {
            const quint32 from = it->channelId;
            it->channelId = moved->channelId;
            emit userMoved(*it, from);
        }
        return true;
    }
    if (const auto *text = std::get_if<TextMessage>(&message)) {
        emit textReceived(*text);
        return true;
    }
    return false;
}

std::optional<ChannelInfo> ClientState::channel(quint32 id) const
{
    const auto it = m_channels.constFind(id);
    return it != m_channels.cend() ? std::optional<ChannelInfo>(*it) : std::nullopt;
}

std::optional<UserInfo> ClientState::user(quint32 id) const
{
    const auto it = m_users.constFind(id);
    return it != m_users.cend() ? std::optional<UserInfo>(*it) : std::nullopt;
}

QList<ChannelInfo> ClientState::channels() const
{
    return m_channels.values();
}

QList<ChannelInfo> ClientState::childChannels(quint32 parentId) const
{
    QList<ChannelInfo> result;
    for (const ChannelInfo &c : m_channels) {
        if (c.parentId == parentId && c.id != kRootChannelId) {
            result.append(c);
        }
    }
    std::sort(result.begin(), result.end(), [](const ChannelInfo &a, const ChannelInfo &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return result;
}

QList<UserInfo> ClientState::users() const
{
    QList<UserInfo> result = m_users.values();
    std::sort(result.begin(), result.end(), [](const UserInfo &a, const UserInfo &b) {
        const int byName = a.name.compare(b.name, Qt::CaseInsensitive);
        return byName != 0 ? byName < 0 : a.id < b.id;
    });
    return result;
}

QList<UserInfo> ClientState::usersInChannel(quint32 channelId) const
{
    QList<UserInfo> result;
    for (const UserInfo &u : users()) {
        if (u.channelId == channelId) {
            result.append(u);
        }
    }
    return result;
}

QString ClientState::channelPath(quint32 id, const QString &rootName) const
{
    QStringList parts;
    quint32 current = id;
    for (int depth = 0; depth < 64 && current != 0; ++depth) {
        const auto it = m_channels.constFind(current);
        if (it == m_channels.cend()) {
            break;
        }
        parts.prepend(it->id == kRootChannelId ? rootName : it->name);
        current = it->parentId;
    }
    return parts.join(QStringLiteral(" / "));
}

} // namespace opentalk::client
