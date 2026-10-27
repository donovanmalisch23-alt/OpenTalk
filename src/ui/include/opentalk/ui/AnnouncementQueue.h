// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <functional>

namespace opentalk::ui {

/// Sends short, polite screen reader announcements without overwhelming the user
/// (accessibility criteria A-7 and A-8):
///
/// - Announcements are at least kMinIntervalMs apart; extra ones wait in a short queue.
///   If the queue overflows, the oldest are dropped, because stale news is less useful.
/// - Users joining or leaving within kCoalesceMs are combined ("3 users joined").
/// - Each category can be switched off in Settings.
class AnnouncementQueue : public QObject {
    Q_OBJECT

public:
    enum class Category { Status, Message, Presence };
    using Announcer = std::function<void(const QString &text)>;

    static constexpr int kMinIntervalMs = 500;
    static constexpr int kCoalesceMs = 1500;
    static constexpr int kMaxQueued = 5;

    explicit AnnouncementQueue(Announcer announcer, QObject *parent = nullptr);

    void setEnabled(Category category, bool enabled);
    bool isEnabled(Category category) const;

    void announce(Category category, const QString &text);
    void userJoined(const QString &name);
    void userLeft(const QString &name);

private:
    void enqueue(const QString &text);
    void speakNext();
    void flushPresence();
    static QString describe(const QStringList &names, const QString &verb);

    Announcer m_announcer;
    bool m_enabled[3] = {true, true, true};
    QStringList m_queue;
    QStringList m_joined;
    QStringList m_left;
    QTimer m_speakTimer;
    QTimer m_presenceTimer;
    QElapsedTimer m_sinceLast;
};

} // namespace opentalk::ui
