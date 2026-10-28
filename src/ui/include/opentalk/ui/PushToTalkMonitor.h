// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QKeySequence>
#include <QObject>
#include <QSet>
#include <QTimer>

namespace opentalk::ui {

/// Watches the push-to-talk key and reports when it is held down.
///
/// On Windows the key works everywhere, even while another program has focus: the
/// keyboard state is read every 10 ms (GetAsyncKeyState), which needs no keyboard hook
/// and does not stop the key from reaching the focused program. Elsewhere, and in
/// tests, it works while an OpenTalk window has focus.
///
/// A key is a QKeySequence with one combination, such as "F12", "Ctrl+Shift+Space" or a
/// modifier-only combination such as "Ctrl+Meta" (Meta is the Windows key).
class PushToTalkMonitor : public QObject {
    Q_OBJECT

public:
    enum class Mode {
        Global,          ///< Anywhere (Windows only; elsewhere behaves like FocusedWindow).
        ApplicationOnly, ///< Only while an OpenTalk window is active.
    };

    explicit PushToTalkMonitor(QObject *parent = nullptr);
    ~PushToTalkMonitor() override;

    /// Sets the key; an empty sequence switches push-to-talk keys off. Returns false
    /// (and switches off) if the key cannot be watched.
    bool setKey(const QKeySequence &key);
    QKeySequence key() const { return m_key; }
    void setMode(Mode mode);
    Mode mode() const { return m_mode; }
    bool isPressed() const { return m_pressed; }

    /// True if `key` can be used: exactly one combination that this platform can watch.
    static bool isUsable(const QKeySequence &key);
    /// Readable name for settings and announcements, for example "Control+F12".
    static QString describe(const QKeySequence &key);

signals:
    void pressedChanged(bool pressed);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setPressed(bool pressed);
    void poll();
    void reconfigure();

    QKeySequence m_key;
    Mode m_mode = Mode::Global;
    bool m_pressed = false;
    QTimer m_pollTimer;
    QSet<int> m_heldKeys;            ///< Application-only mode: keys currently down.
    QList<QList<int>> m_virtualKeys; ///< Windows: each entry is "any of these keys".
};

} // namespace opentalk::ui
