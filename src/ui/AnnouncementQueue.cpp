// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/ui/AnnouncementQueue.h"

namespace opentalk::ui {

AnnouncementQueue::AnnouncementQueue(Announcer announcer, QObject *parent)
    : QObject(parent)
    , m_announcer(std::move(announcer))
{
    m_speakTimer.setSingleShot(true);
    // The default coarse timer may fire up to 5% early, which would break the minimum spacing.
    m_speakTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_speakTimer, &QTimer::timeout, this, &AnnouncementQueue::speakNext);
    m_presenceTimer.setSingleShot(true);
    m_presenceTimer.setInterval(kCoalesceMs);
    connect(&m_presenceTimer, &QTimer::timeout, this, &AnnouncementQueue::flushPresence);
}

void AnnouncementQueue::setEnabled(Category category, bool enabled)
{
    m_enabled[static_cast<int>(category)] = enabled;
    if (category == Category::Presence && !enabled) {
        m_joined.clear();
        m_left.clear();
        m_presenceTimer.stop();
    }
}

bool AnnouncementQueue::isEnabled(Category category) const
{
    return m_enabled[static_cast<int>(category)];
}

void AnnouncementQueue::announce(Category category, const QString &text)
{
    if (isEnabled(category) && !text.trimmed().isEmpty()) {
        enqueue(text);
    }
}

void AnnouncementQueue::userJoined(const QString &name)
{
    if (!isEnabled(Category::Presence)) {
        return;
    }
    // Someone who leaves and rejoins within the window is not news.
    if (!m_left.removeOne(name)) {
        m_joined.append(name);
    }
    if (!m_presenceTimer.isActive()) {
        m_presenceTimer.start();
    }
}

void AnnouncementQueue::userLeft(const QString &name)
{
    if (!isEnabled(Category::Presence)) {
        return;
    }
    if (!m_joined.removeOne(name)) {
        m_left.append(name);
    }
    if (!m_presenceTimer.isActive()) {
        m_presenceTimer.start();
    }
}

QString AnnouncementQueue::describe(const QStringList &names, const QString &verb)
{
    if (names.size() == 1) {
        return tr("%1 %2").arg(names.first(), verb);
    }
    if (names.size() == 2) {
        return tr("%1 and %2 %3").arg(names.at(0), names.at(1), verb);
    }
    return tr("%1 users %2").arg(names.size()).arg(verb);
}

void AnnouncementQueue::flushPresence()
{
    if (!m_joined.isEmpty()) {
        enqueue(describe(m_joined, tr("joined")));
    }
    if (!m_left.isEmpty()) {
        enqueue(describe(m_left, tr("left")));
    }
    m_joined.clear();
    m_left.clear();
}

void AnnouncementQueue::enqueue(const QString &text)
{
    m_queue.append(text);
    while (m_queue.size() > kMaxQueued) {
        m_queue.removeFirst();
    }
    if (!m_speakTimer.isActive()) {
        const qint64 waited = m_sinceLast.isValid() ? m_sinceLast.elapsed() : kMinIntervalMs;
        m_speakTimer.start(static_cast<int>(std::max<qint64>(0, kMinIntervalMs - waited)));
    }
}

void AnnouncementQueue::speakNext()
{
    if (m_queue.isEmpty()) {
        return;
    }
    const QString text = m_queue.takeFirst();
    m_sinceLast.start();
    if (m_announcer) {
        m_announcer(text);
    }
    if (!m_queue.isEmpty()) {
        m_speakTimer.start(kMinIntervalMs);
    }
}

} // namespace opentalk::ui
