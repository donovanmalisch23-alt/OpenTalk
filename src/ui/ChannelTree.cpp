// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/ui/ChannelTree.h"

#include "opentalk/audio/AudioFormat.h"
#include "opentalk/client/ClientState.h"

#include <QTreeWidgetItemIterator>

namespace opentalk::ui {
namespace {

constexpr int kKindRole = Qt::UserRole;
constexpr int kIdRole = Qt::UserRole + 1;

} // namespace

ChannelTree::ChannelTree(QWidget *parent)
    : QTreeWidget(parent)
{
    setHeaderHidden(true);
    setRootIsDecorated(true);
    setUniformRowHeights(true);
    setExpandsOnDoubleClick(false);
    connect(this, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        const auto kind = static_cast<ItemKind>(item->data(0, kKindRole).toInt());
        const quint32 id = item->data(0, kIdRole).toUInt();
        if (kind == ItemKind::Channel) {
            emit channelActivated(id);
        } else if (kind == ItemKind::User) {
            emit userActivated(id);
        }
    });
}

QString ChannelTree::channelLabel(const QString &name, int userCount, bool hasPassword, bool persistent)
{
    QString label = name;
    if (userCount > 0) {
        label += userCount == 1 ? tr(", 1 user") : tr(", %1 users").arg(userCount);
    }
    if (hasPassword) {
        label += tr(", password protected");
    }
    if (!persistent) {
        label += tr(", temporary");
    }
    return label;
}

QString ChannelTree::userLabel(const QString &name, bool isSelf, const QString &role, quint32 flags)
{
    QString label = name;
    if (isSelf) {
        label += tr(" (you)");
    }
    if ((flags & protocol::BotFlag) != 0) {
        label += tr(", bot");
    }
    if (!role.isEmpty()) {
        label += tr(", %1").arg(role);
    }
    const bool voiceMuted = (flags & protocol::VoiceMutedFlag) != 0;
    const bool textMuted = (flags & protocol::TextMutedFlag) != 0;
    if (voiceMuted && textMuted) {
        label += tr(", muted");
    } else if (voiceMuted) {
        label += tr(", voice muted");
    } else if (textMuted) {
        label += tr(", text muted");
    }
    if ((flags & protocol::RestrictedFlag) != 0) {
        label += tr(", restricted");
    }
    return label;
}

QString ChannelTree::roleLabel(const client::ClientState &state, quint32 roleId)
{
    switch (static_cast<protocol::Role>(roleId)) {
    case protocol::Role::User:
        return QString();
    case protocol::Role::Guest:
        return tr("guest");
    case protocol::Role::Admin:
        return tr("administrator");
    case protocol::Role::Owner:
        return tr("owner");
    case protocol::Role::Moderator:
        return tr("moderator");
    case protocol::Role::Restricted:
        return tr("restricted");
    }
    return state.roleName(roleId); // a role the server's owner made
}

void ChannelTree::showMessage(const QString &text)
{
    clear();
    auto *item = new QTreeWidgetItem(this, {text});
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    item->setData(0, kKindRole, static_cast<int>(ItemKind::None));
    setCurrentItem(item);
}

void ChannelTree::rebuild(const client::ClientState &state, const QString &serverName)
{
    const ItemKind previousKind = currentKind();
    const quint32 previousId = currentItem() != nullptr ? currentItem()->data(0, kIdRole).toUInt() : 0;
    const bool hadFocus = hasFocus();

    setUpdatesEnabled(false);
    clear();
    QHash<quint32, QTreeWidgetItem *> channelItems;
    for (const protocol::ChannelInfo &channel : state.channels()) {
        // channels() is in ID order, so a parent is always created before its children.
        const bool isRoot = channel.id == protocol::kRootChannelId;
        QTreeWidgetItem *parentItem = isRoot ? nullptr : channelItems.value(channel.parentId);
        if (!isRoot && parentItem == nullptr) {
            continue;
        }
        const QList<protocol::UserInfo> members = state.usersInChannel(channel.id);
        const QString label = channelLabel(isRoot ? serverName : channel.name, static_cast<int>(members.size()),
                                           channel.hasPassword, channel.persistent);
        auto *item =
            parentItem != nullptr ? new QTreeWidgetItem(parentItem, {label}) : new QTreeWidgetItem(this, {label});
        item->setData(0, kKindRole, static_cast<int>(ItemKind::Channel));
        item->setData(0, kIdRole, channel.id);
        if (!channel.topic.isEmpty()) {
            item->setToolTip(0, channel.topic);
            item->setStatusTip(0, channel.topic);
        }
        channelItems.insert(channel.id, item);
        for (const protocol::UserInfo &user : members) {
            auto *userItem =
                new QTreeWidgetItem(item, {userLabel(user.name, user.id == state.selfId(),
                                                     roleLabel(state, static_cast<quint32>(user.role)), user.flags)});
            userItem->setData(0, kKindRole, static_cast<int>(ItemKind::User));
            userItem->setData(0, kIdRole, user.id);
            if (m_activity.contains(user.id)) {
                applyActivity(userItem, m_activity.value(user.id));
            }
        }
    }
    // Users come before sub-channels inside each channel; sort sub-channels by name.
    for (QTreeWidgetItem *item : std::as_const(channelItems)) {
        QList<QTreeWidgetItem *> users;
        QList<QTreeWidgetItem *> channels;
        for (QTreeWidgetItem *child : item->takeChildren()) {
            (static_cast<ItemKind>(child->data(0, kKindRole).toInt()) == ItemKind::User ? users : channels)
                .append(child);
        }
        std::sort(channels.begin(), channels.end(), [](QTreeWidgetItem *a, QTreeWidgetItem *b) {
            return a->text(0).compare(b->text(0), Qt::CaseInsensitive) < 0;
        });
        item->addChildren(users);
        item->addChildren(channels);
    }
    expandAll();
    setUpdatesEnabled(true);

    QTreeWidgetItem *restore = findItem(previousKind, previousId);
    if (restore == nullptr) {
        restore = findItem(ItemKind::Channel, state.currentChannelId());
    }
    if (restore == nullptr) {
        restore = topLevelItem(0);
    }
    if (restore != nullptr) {
        setCurrentItem(restore);
    }
    if (hadFocus) {
        setFocus(Qt::OtherFocusReason);
    }
}

QTreeWidgetItem *ChannelTree::findItem(ItemKind kind, quint32 id) const
{
    if (kind == ItemKind::None) {
        return nullptr;
    }
    for (QTreeWidgetItemIterator it(const_cast<ChannelTree *>(this)); *it != nullptr; ++it) {
        if (static_cast<ItemKind>((*it)->data(0, kKindRole).toInt()) == kind &&
            (*it)->data(0, kIdRole).toUInt() == id) {
            return *it;
        }
    }
    return nullptr;
}

ChannelTree::ItemKind ChannelTree::currentKind() const
{
    return currentItem() != nullptr ? static_cast<ItemKind>(currentItem()->data(0, kKindRole).toInt()) : ItemKind::None;
}

std::optional<quint32> ChannelTree::currentChannelId() const
{
    if (currentKind() == ItemKind::Channel) {
        return currentItem()->data(0, kIdRole).toUInt();
    }
    // A selected user means "that user's channel".
    if (currentKind() == ItemKind::User && currentItem()->parent() != nullptr) {
        return currentItem()->parent()->data(0, kIdRole).toUInt();
    }
    return std::nullopt;
}

std::optional<quint32> ChannelTree::currentUserId() const
{
    return currentKind() == ItemKind::User ? std::optional<quint32>(currentItem()->data(0, kIdRole).toUInt())
                                           : std::nullopt;
}

void ChannelTree::setUserActivity(quint32 userId, int streams)
{
    if (streams != 0) {
        m_activity.insert(userId, streams);
    } else {
        m_activity.remove(userId);
    }
    if (QTreeWidgetItem *item = findItem(ItemKind::User, userId)) {
        applyActivity(item, streams);
    }
}

QString ChannelTree::activityText(int streams)
{
    QStringList parts;
    if (streams & (1 << static_cast<int>(audio::StreamType::Voice))) {
        parts << tr("talking");
    }
    // Never described as talking, so listeners know it is not the person's microphone.
    if (streams & (1 << static_cast<int>(audio::StreamType::Media))) {
        parts << tr("playing a file");
    }
    if (streams & (1 << static_cast<int>(audio::StreamType::SystemAudio))) {
        parts << tr("sharing computer audio");
    }
    return parts.join(QStringLiteral(", "));
}

void ChannelTree::applyActivity(QTreeWidgetItem *item, int streams)
{
    QFont font = item->font(0);
    font.setBold(streams != 0);
    item->setFont(0, font);
    const QString text = activityText(streams);
    item->setData(0, Qt::AccessibleDescriptionRole, text);
    item->setToolTip(0, text.isEmpty() ? QString() : text.left(1).toUpper() + text.mid(1));
}

void ChannelTree::selectChannel(quint32 channelId)
{
    if (QTreeWidgetItem *item = findItem(ItemKind::Channel, channelId)) {
        setCurrentItem(item);
    }
}

} // namespace opentalk::ui
