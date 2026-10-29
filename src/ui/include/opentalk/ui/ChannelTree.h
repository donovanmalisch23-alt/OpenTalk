// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QSet>
#include <QTreeWidget>

#include <optional>

namespace opentalk::client {
class ClientState;
}

namespace opentalk::ui {

/// The channel tree: channels in their hierarchy, with the users in each channel listed
/// beneath it. Rebuilt from ClientState on every change, keeping the selected item and
/// keyboard position, so screen reader users do not lose their place.
///
/// Enter (or double-click) on a channel emits channelActivated; on a user, userActivated.
class ChannelTree : public QTreeWidget {
    Q_OBJECT

public:
    enum class ItemKind { None, Channel, User };

    explicit ChannelTree(QWidget *parent = nullptr);

    void showMessage(const QString &text);
    void rebuild(const client::ClientState &state, const QString &serverName);

    ItemKind currentKind() const;
    std::optional<quint32> currentChannelId() const;
    std::optional<quint32> currentUserId() const;
    void selectChannel(quint32 channelId);
    /// What is heard from a user, as bits: 1 << audio::StreamType (talking, playing a
    /// file, sharing computer audio). Changed in place (no rebuild, focus and position
    /// stay). Shown in bold with a description such as "talking" or "playing a file";
    /// the item's name does not change, so a screen reader does not re-read the focused
    /// item every time someone speaks.
    void setUserActivity(quint32 userId, int streams);
    int userActivity(quint32 userId) const { return m_activity.value(userId); }
    bool isUserTalking(quint32 userId) const { return m_activity.value(userId) & 1; }
    /// For example "talking, playing a file".
    static QString activityText(int streams);

    /// The visible text of an item, as a screen reader will read it.
    static QString channelLabel(const QString &name, int userCount, bool hasPassword, bool persistent);
    /// For example "alice (you), moderator, muted, status: back at 5". `role` is empty for
    /// ordinary registered users; `flags` are protocol::UserFlag bits.
    static QString userLabel(const QString &name, bool isSelf, const QString &role, quint32 flags,
                             const QString &status = {});
    /// How a role is shown after a name: "moderator", "guest", a custom role's own name,
    /// or empty for registered users.
    static QString roleLabel(const client::ClientState &state, quint32 roleId);

signals:
    void channelActivated(quint32 channelId);
    void userActivated(quint32 userId);

private:
    QTreeWidgetItem *findItem(ItemKind kind, quint32 id) const;
    void applyActivity(QTreeWidgetItem *item, int streams);

    QHash<quint32, int> m_activity;
};

} // namespace opentalk::ui
