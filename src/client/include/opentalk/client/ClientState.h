// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/moderation/Permissions.h"
#include "opentalk/protocol/Messages.h"

#include <QHash>
#include <QList>
#include <QMap>
#include <QObject>

#include <optional>

namespace opentalk::client {

/// The client's picture of the server: channels, online users and who "I" am, kept up to
/// date from server messages. It assumes nothing about the server's honesty: events
/// that refer to unknown channels or users, or that would break the tree, are ignored
/// rather than trusted, and the total size is capped.
///
/// No I/O; owned by one thread.
class ClientState : public QObject {
    Q_OBJECT

public:
    /// Upper bound on channels or users kept, whatever the server sends.
    static constexpr qsizetype kMaxEntries = 20000;

    explicit ClientState(QObject *parent = nullptr);

    void clear();
    void setSelf(const protocol::AuthResult &self);

    /// Applies a server message. Returns true if it was a state or text message.
    bool apply(const protocol::Message &message);

    quint32 selfId() const { return m_self.userId; }
    QString selfName() const { return m_self.displayName; }
    protocol::Role selfRole() const { return m_self.role; }
    quint32 currentChannelId() const;
    /// What this user may do server-wide, as the server last said. Use it to offer only
    /// possible actions; the server decides in the end (channel rules can differ).
    bool can(moderation::Capability capability) const;
    /// True if the user may do this server-wide or in at least one channel (a channel
    /// moderator, for example). Use it to offer actions that the server checks per channel.
    bool canSomewhere(moderation::Capability capability) const;
    /// True if the user may do any moderation or administration at all.
    bool canModerate() const;
    QList<protocol::RoleInfo> roles() const { return m_roles; }
    /// The role's name, for example "Moderator" (or "role 105" if unknown).
    QString roleName(quint32 roleId) const;
    int roleRank(quint32 roleId) const;

    std::optional<protocol::ChannelInfo> channel(quint32 id) const;
    std::optional<protocol::UserInfo> user(quint32 id) const;
    /// Channels in ID order (parents always come before their children).
    QList<protocol::ChannelInfo> channels() const;
    QList<protocol::ChannelInfo> childChannels(quint32 parentId) const;
    /// Users sorted by name, case-insensitively.
    QList<protocol::UserInfo> users() const;
    QList<protocol::UserInfo> usersInChannel(quint32 channelId) const;
    /// For example "Lobby / Quiet corner". The root channel is shown as `rootName`.
    QString channelPath(quint32 id, const QString &rootName) const;

signals:
    /// The whole state was replaced (after login).
    void reset();
    void channelAdded(const opentalk::protocol::ChannelInfo &channel);
    void channelRemoved(const opentalk::protocol::ChannelInfo &channel);
    void userJoined(const opentalk::protocol::UserInfo &user);
    void userLeft(const opentalk::protocol::UserInfo &user);
    void userMoved(const opentalk::protocol::UserInfo &user, quint32 fromChannelId);
    void textReceived(const opentalk::protocol::TextMessage &message);
    void channelUpdated(const opentalk::protocol::ChannelInfo &channel);
    /// A user's role or flags (mutes, restriction) changed.
    void userUpdated(const opentalk::protocol::UserInfo &user, const opentalk::protocol::UserInfo &before);
    void rolesChanged();
    void capabilitiesChanged();
    /// A sanction on this user was applied, changed or lifted.
    void sanctionNotice(const opentalk::protocol::SanctionInfo &sanction);

private:
    bool addChannel(const protocol::ChannelInfo &channel);

    protocol::AuthResult m_self;
    QList<protocol::RoleInfo> m_roles;
    QMap<quint32, protocol::ChannelInfo> m_channels;
    QHash<quint32, protocol::UserInfo> m_users;
};

} // namespace opentalk::client

Q_DECLARE_METATYPE(opentalk::protocol::ChannelInfo)
Q_DECLARE_METATYPE(opentalk::protocol::UserInfo)
Q_DECLARE_METATYPE(opentalk::protocol::TextMessage)
