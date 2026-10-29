// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/ui/AnnouncementQueue.h"

#include <QElapsedTimer>
#include <QTest>

using opentalk::ui::AnnouncementQueue;

class TestAnnouncements : public QObject {
    Q_OBJECT

private:
    QStringList m_spoken;
    QList<qint64> m_times;
    QElapsedTimer m_clock;

    AnnouncementQueue::Announcer recorder()
    {
        return [this](const QString &text) {
            m_spoken.append(text);
            m_times.append(m_clock.elapsed());
        };
    }

private slots:
    void init()
    {
        m_spoken.clear();
        m_times.clear();
        m_clock.start();
    }

    void firstAnnouncementIsImmediateAndOthersAreSpaced()
    {
        AnnouncementQueue queue(recorder());
        queue.announce(AnnouncementQueue::Category::Status, QStringLiteral("one"));
        queue.announce(AnnouncementQueue::Category::Message, QStringLiteral("two"));
        queue.announce(AnnouncementQueue::Category::Message, QStringLiteral("three"));
        QTRY_COMPARE_WITH_TIMEOUT(m_spoken.size(), 3, 3000);
        QCOMPARE(m_spoken, (QStringList{QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three")}));
        QVERIFY(m_times[0] < 200);
        QVERIFY(m_times[1] - m_times[0] >= AnnouncementQueue::kMinIntervalMs - 20);
        QVERIFY(m_times[2] - m_times[1] >= AnnouncementQueue::kMinIntervalMs - 20);
    }

    void overflowDropsTheOldest()
    {
        AnnouncementQueue queue(recorder());
        queue.announce(AnnouncementQueue::Category::Status, QStringLiteral("first"));
        QTRY_COMPARE_WITH_TIMEOUT(m_spoken.size(), 1, 1000);
        for (int i = 1; i <= 8; ++i) {
            queue.announce(AnnouncementQueue::Category::Message, QStringLiteral("m%1").arg(i));
        }
        QTRY_COMPARE_WITH_TIMEOUT(m_spoken.size(), 1 + AnnouncementQueue::kMaxQueued, 5000);
        QCOMPARE(m_spoken.last(), QStringLiteral("m8"));
        QCOMPARE(m_spoken.at(1), QStringLiteral("m4"));
    }

    void presenceIsCoalesced()
    {
        AnnouncementQueue queue(recorder());
        queue.userJoined(QStringLiteral("alice"));
        queue.userJoined(QStringLiteral("bob"));
        queue.userJoined(QStringLiteral("carol"));
        queue.userLeft(QStringLiteral("dave"));
        QTest::qWait(AnnouncementQueue::kCoalesceMs / 2);
        QVERIFY(m_spoken.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(m_spoken.size(), 2, 4000);
        QCOMPARE(m_spoken.at(0), QStringLiteral("3 users joined"));
        QCOMPARE(m_spoken.at(1), QStringLiteral("dave left"));
    }

    void quickRejoinIsNotNews()
    {
        AnnouncementQueue queue(recorder());
        queue.userLeft(QStringLiteral("alice"));
        queue.userJoined(QStringLiteral("alice"));
        queue.userJoined(QStringLiteral("bob"));
        QTRY_COMPARE_WITH_TIMEOUT(m_spoken.size(), 1, 4000);
        QCOMPARE(m_spoken.first(), QStringLiteral("bob joined"));
    }

    void categoriesCanBeDisabled()
    {
        AnnouncementQueue queue(recorder());
        queue.setEnabled(AnnouncementQueue::Category::Message, false);
        queue.setEnabled(AnnouncementQueue::Category::Presence, false);
        queue.announce(AnnouncementQueue::Category::Message, QStringLiteral("hidden"));
        queue.userJoined(QStringLiteral("alice"));
        queue.announce(AnnouncementQueue::Category::Status, QStringLiteral("shown"));
        QTest::qWait(AnnouncementQueue::kCoalesceMs + 300);
        QCOMPARE(m_spoken, QStringList{QStringLiteral("shown")});
    }
};

QTEST_GUILESS_MAIN(TestAnnouncements)
#include "tst_announcements.moc"
