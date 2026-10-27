// SPDX-License-Identifier: GPL-3.0-only
#pragma once

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

    /// The visible text of an item, as a screen reader will read it.
    static QString channelLabel(const QString &name, int userCount, bool hasPassword, bool persistent);
    static QString userLabel(const QString &name, bool isSelf, int role);

signals:
    void channelActivated(quint32 channelId);
    void userActivated(quint32 userId);

private:
    QTreeWidgetItem *findItem(ItemKind kind, quint32 id) const;
};

} // namespace opentalk::ui
