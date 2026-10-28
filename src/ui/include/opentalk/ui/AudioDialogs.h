// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QDialog>
#include <QKeySequence>
#include <QSet>

class QLabel;
class QSlider;
class QSpinBox;

namespace opentalk::ui {

/// Asks the user to press the key or key combination to use for push-to-talk.
/// The combination is taken when every key has been released again, so modifier-only
/// combinations such as Ctrl+Windows work. Escape cancels.
class KeyCaptureDialog : public QDialog {
    Q_OBJECT

public:
    explicit KeyCaptureDialog(QWidget *parent = nullptr);
    QKeySequence key() const { return m_key; }

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    QLabel *m_prompt = nullptr;
    QSet<int> m_down;
    int m_mainKey = 0;
    Qt::KeyboardModifiers m_modifiers;
    QKeySequence m_key;
};

/// Sets how loud one person is for you (0 to 400 percent), with a mute shortcut.
class UserVolumeDialog : public QDialog {
    Q_OBJECT

public:
    UserVolumeDialog(const QString &userName, int percent, QWidget *parent = nullptr);
    int percent() const;

signals:
    /// Emitted while the value changes, so the change can be heard at once.
    void percentChanged(int percent);

private:
    QSpinBox *m_spin = nullptr;
    QSlider *m_slider = nullptr;
};

} // namespace opentalk::ui
