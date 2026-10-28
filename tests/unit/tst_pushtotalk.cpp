// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/ui/AudioDialogs.h"
#include "opentalk/ui/PushToTalkMonitor.h"

#include <QAbstractSpinBox>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

using namespace opentalk::ui;

class TestPushToTalk : public QObject {
    Q_OBJECT

private:
    QLineEdit *m_target = nullptr;

private slots:
    void init()
    {
        m_target = new QLineEdit;
        m_target->show();
        m_target->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(m_target));
    }

    void cleanup() { delete m_target; }

    void singleKey()
    {
        PushToTalkMonitor ptt;
        ptt.setMode(PushToTalkMonitor::Mode::ApplicationOnly);
        QVERIFY(ptt.setKey(QKeySequence(Qt::Key_F12)));
        QSignalSpy changed(&ptt, &PushToTalkMonitor::pressedChanged);
        QTest::keyPress(m_target, Qt::Key_F12);
        QVERIFY(ptt.isPressed());
        QTest::keyRelease(m_target, Qt::Key_F12);
        QVERIFY(!ptt.isPressed());
        QCOMPARE(changed.size(), 2);
        QTest::keyClick(m_target, Qt::Key_F11);
        QCOMPARE(changed.size(), 2);
    }

    void combinationNeedsItsModifiers()
    {
        PushToTalkMonitor ptt;
        ptt.setMode(PushToTalkMonitor::Mode::ApplicationOnly);
        QVERIFY(ptt.setKey(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Space)));
        QTest::keyPress(m_target, Qt::Key_Space);
        QVERIFY(!ptt.isPressed());
        QTest::keyRelease(m_target, Qt::Key_Space);
        QTest::keyPress(m_target, Qt::Key_Control, Qt::ControlModifier);
        QTest::keyPress(m_target, Qt::Key_Shift, Qt::ControlModifier | Qt::ShiftModifier);
        QTest::keyPress(m_target, Qt::Key_Space, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(ptt.isPressed());
        // Letting go of a modifier ends talking even if Space is still down.
        QTest::keyRelease(m_target, Qt::Key_Shift, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(!ptt.isPressed());
        QTest::keyRelease(m_target, Qt::Key_Space, Qt::ControlModifier);
        QTest::keyRelease(m_target, Qt::Key_Control, Qt::ControlModifier);
    }

    void modifierOnlyCombination()
    {
        PushToTalkMonitor ptt;
        ptt.setMode(PushToTalkMonitor::Mode::ApplicationOnly);
        QVERIFY(ptt.setKey(QKeySequence::fromString(QStringLiteral("Ctrl+Meta"), QKeySequence::PortableText)));
        QTest::keyPress(m_target, Qt::Key_Control, Qt::ControlModifier);
        QVERIFY(!ptt.isPressed());
        QTest::keyPress(m_target, Qt::Key_Meta, Qt::ControlModifier | Qt::MetaModifier);
        QVERIFY(ptt.isPressed());
        QTest::keyRelease(m_target, Qt::Key_Control, Qt::ControlModifier | Qt::MetaModifier);
        QVERIFY(!ptt.isPressed());
        QTest::keyRelease(m_target, Qt::Key_Meta, Qt::MetaModifier);
    }

    void autoRepeatIsIgnoredAndKeyCanBeCleared()
    {
        PushToTalkMonitor ptt;
        ptt.setMode(PushToTalkMonitor::Mode::ApplicationOnly);
        QVERIFY(ptt.setKey(QKeySequence(Qt::Key_F12)));
        QSignalSpy changed(&ptt, &PushToTalkMonitor::pressedChanged);
        QTest::keyPress(m_target, Qt::Key_F12);
        QTest::keyEvent(QTest::Press, m_target, Qt::Key_F12, Qt::NoModifier); // still held
        QCOMPARE(changed.size(), 1);
        QVERIFY(ptt.setKey(QKeySequence())); // switching off releases
        QVERIFY(!ptt.isPressed());
        QTest::keyRelease(m_target, Qt::Key_F12);
        QTest::keyPress(m_target, Qt::Key_F12);
        QVERIFY(!ptt.isPressed());
    }

    void unusableKeys()
    {
        QVERIFY(!PushToTalkMonitor::isUsable(QKeySequence(Qt::Key_Escape)));
        QVERIFY(!PushToTalkMonitor::isUsable(QKeySequence(Qt::Key_F1, Qt::Key_F2)));
        QVERIFY(PushToTalkMonitor::isUsable(QKeySequence(Qt::Key_F12)));
        PushToTalkMonitor ptt;
        QVERIFY(!ptt.setKey(QKeySequence(Qt::Key_Escape)));
        QVERIFY(ptt.key().isEmpty());
        QCOMPARE(PushToTalkMonitor::describe(QKeySequence()), QStringLiteral("none"));
    }

    void captureDialogTakesTheReleasedCombination()
    {
        KeyCaptureDialog dialog;
        QTimer::singleShot(0, &dialog, [&dialog] {
            QWidget *focus = dialog.focusWidget() != nullptr ? dialog.focusWidget() : &dialog;
            QTest::keyPress(focus, Qt::Key_Control, Qt::ControlModifier);
            QTest::keyPress(focus, Qt::Key_F12, Qt::ControlModifier);
            QTest::keyRelease(focus, Qt::Key_F12, Qt::ControlModifier);
            QTest::keyRelease(focus, Qt::Key_Control, Qt::NoModifier);
        });
        QCOMPARE(dialog.exec(), int(QDialog::Accepted));
        QCOMPARE(dialog.key(), QKeySequence(Qt::CTRL | Qt::Key_F12));
    }

    void captureDialogTakesModifierOnlyAndCancels()
    {
        KeyCaptureDialog dialog;
        QTimer::singleShot(0, &dialog, [&dialog] {
            QWidget *focus = dialog.focusWidget() != nullptr ? dialog.focusWidget() : &dialog;
            QTest::keyPress(focus, Qt::Key_Control, Qt::ControlModifier);
            QTest::keyPress(focus, Qt::Key_Meta, Qt::ControlModifier | Qt::MetaModifier);
            QTest::keyRelease(focus, Qt::Key_Meta, Qt::ControlModifier);
            QTest::keyRelease(focus, Qt::Key_Control, Qt::NoModifier);
        });
        QCOMPARE(dialog.exec(), int(QDialog::Accepted));
        QCOMPARE(dialog.key().toString(QKeySequence::PortableText), QStringLiteral("Ctrl+Meta"));

        KeyCaptureDialog cancel;
        QTimer::singleShot(0, &cancel, [&cancel] {
            QTest::keyClick(cancel.focusWidget() != nullptr ? cancel.focusWidget() : &cancel, Qt::Key_Escape);
        });
        QCOMPARE(cancel.exec(), int(QDialog::Rejected));
        QVERIFY(cancel.key().isEmpty());
    }

    void userVolumeDialog()
    {
        UserVolumeDialog dialog(QStringLiteral("alice"), 150);
        QSignalSpy changed(&dialog, &UserVolumeDialog::percentChanged);
        QCOMPARE(dialog.percent(), 150);
        auto *spin = dialog.findChild<QAbstractSpinBox *>();
        QVERIFY(spin != nullptr);
        QVERIFY(!spin->accessibleName().isEmpty());
        dialog.show();
        QTest::keyClick(spin, Qt::Key_Up);
        QCOMPARE(dialog.percent(), 151);
        QCOMPARE(changed.size(), 1);
    }
};

QTEST_MAIN(TestPushToTalk)
#include "tst_pushtotalk.moc"
