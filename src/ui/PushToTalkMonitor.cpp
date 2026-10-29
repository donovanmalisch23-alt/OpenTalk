// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/ui/PushToTalkMonitor.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace opentalk::ui {
namespace {

constexpr Qt::KeyboardModifiers kModifierMask =
    Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier;

bool isModifierKey(int key)
{
    return key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta ||
           key == Qt::Key_AltGr;
}

Qt::KeyboardModifier modifierFor(int key)
{
    switch (key) {
    case Qt::Key_Control:
        return Qt::ControlModifier;
    case Qt::Key_Shift:
        return Qt::ShiftModifier;
    case Qt::Key_Alt:
        return Qt::AltModifier;
    case Qt::Key_Meta:
        return Qt::MetaModifier;
    default:
        return Qt::NoModifier;
    }
}

#ifdef Q_OS_WIN
/// Windows virtual-key codes for a Qt key; several entries mean "either one".
QList<int> virtualKeysFor(int key, bool keypad)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return {'A' + (key - Qt::Key_A)};
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return {keypad ? VK_NUMPAD0 + (key - Qt::Key_0) : '0' + (key - Qt::Key_0)};
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        return {VK_F1 + (key - Qt::Key_F1)};
    }
    switch (key) {
    case Qt::Key_Control:
        return {VK_CONTROL};
    case Qt::Key_Shift:
        return {VK_SHIFT};
    case Qt::Key_Alt:
        return {VK_MENU};
    case Qt::Key_Meta:
        return {VK_LWIN, VK_RWIN};
    case Qt::Key_Space:
        return {VK_SPACE};
    case Qt::Key_Tab:
        return {VK_TAB};
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return {VK_RETURN};
    case Qt::Key_Backspace:
        return {VK_BACK};
    case Qt::Key_Insert:
        return {VK_INSERT};
    case Qt::Key_Delete:
        return {VK_DELETE};
    case Qt::Key_Home:
        return {VK_HOME};
    case Qt::Key_End:
        return {VK_END};
    case Qt::Key_PageUp:
        return {VK_PRIOR};
    case Qt::Key_PageDown:
        return {VK_NEXT};
    case Qt::Key_Left:
        return {VK_LEFT};
    case Qt::Key_Right:
        return {VK_RIGHT};
    case Qt::Key_Up:
        return {VK_UP};
    case Qt::Key_Down:
        return {VK_DOWN};
    case Qt::Key_Pause:
        return {VK_PAUSE};
    case Qt::Key_ScrollLock:
        return {VK_SCROLL};
    case Qt::Key_NumLock:
        return {VK_NUMLOCK};
    case Qt::Key_Menu:
        return {VK_APPS};
    case Qt::Key_Asterisk:
        return {VK_MULTIPLY};
    case Qt::Key_Plus:
        return {keypad ? VK_ADD : VK_OEM_PLUS};
    case Qt::Key_Minus:
        return {keypad ? VK_SUBTRACT : VK_OEM_MINUS};
    case Qt::Key_Slash:
        return {keypad ? VK_DIVIDE : VK_OEM_2};
    case Qt::Key_Period:
        return {keypad ? VK_DECIMAL : VK_OEM_PERIOD};
    case Qt::Key_Comma:
        return {VK_OEM_COMMA};
    case Qt::Key_Semicolon:
        return {VK_OEM_1};
    case Qt::Key_QuoteLeft:
        return {VK_OEM_3};
    case Qt::Key_BracketLeft:
        return {VK_OEM_4};
    case Qt::Key_Backslash:
        return {VK_OEM_5};
    case Qt::Key_BracketRight:
        return {VK_OEM_6};
    case Qt::Key_Apostrophe:
        return {VK_OEM_7};
    default:
        return {};
    }
}

/// All the virtual keys that must be down for `combination`, or empty if unsupported.
QList<QList<int>> virtualKeysFor(QKeyCombination combination)
{
    QList<QList<int>> result;
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    const auto addModifier = [&](Qt::KeyboardModifier modifier, int key) {
        if (modifiers.testFlag(modifier)) {
            result.append(virtualKeysFor(key, false));
        }
    };
    addModifier(Qt::ControlModifier, Qt::Key_Control);
    addModifier(Qt::ShiftModifier, Qt::Key_Shift);
    addModifier(Qt::AltModifier, Qt::Key_Alt);
    addModifier(Qt::MetaModifier, Qt::Key_Meta);
    const QList<int> main = virtualKeysFor(combination.key(), modifiers.testFlag(Qt::KeypadModifier));
    if (main.isEmpty()) {
        return {};
    }
    result.append(main);
    return result;
}
#endif

} // namespace

PushToTalkMonitor::PushToTalkMonitor(QObject *parent)
    : QObject(parent)
{
    m_pollTimer.setInterval(10);
    m_pollTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_pollTimer, &QTimer::timeout, this, &PushToTalkMonitor::poll);
    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::instance()->installEventFilter(this);
    }
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(app, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive && m_mode == Mode::ApplicationOnly) {
                m_heldKeys.clear();
                setPressed(false);
            }
        });
    }
}

PushToTalkMonitor::~PushToTalkMonitor() = default;

bool PushToTalkMonitor::isUsable(const QKeySequence &key)
{
    if (key.count() != 1 || key[0].key() == Qt::Key_unknown || key[0].key() == Qt::Key_Escape) {
        return false;
    }
#ifdef Q_OS_WIN
    return !virtualKeysFor(key[0]).isEmpty();
#else
    return true;
#endif
}

QString PushToTalkMonitor::describe(const QKeySequence &key)
{
    if (key.isEmpty()) {
        return tr("none");
    }
    QString text = key.toString(QKeySequence::NativeText);
#ifdef Q_OS_WIN
    text.replace(QStringLiteral("Meta"), tr("Windows"));
#endif
    return text;
}

bool PushToTalkMonitor::setKey(const QKeySequence &key)
{
    const bool usable = key.isEmpty() || isUsable(key);
    m_key = usable ? key : QKeySequence();
    reconfigure();
    return usable;
}

void PushToTalkMonitor::setMode(Mode mode)
{
    m_mode = mode;
    reconfigure();
}

void PushToTalkMonitor::reconfigure()
{
    m_pollTimer.stop();
    m_heldKeys.clear();
    m_virtualKeys.clear();
    setPressed(false);
    if (m_key.isEmpty()) {
        return;
    }
#ifdef Q_OS_WIN
    if (m_mode == Mode::Global) {
        m_virtualKeys = virtualKeysFor(m_key[0]);
        m_pollTimer.start();
    }
#endif
}

void PushToTalkMonitor::poll()
{
#ifdef Q_OS_WIN
    bool allDown = !m_virtualKeys.isEmpty();
    for (const QList<int> &anyOf : std::as_const(m_virtualKeys)) {
        bool down = false;
        for (int vk : anyOf) {
            down = down || (GetAsyncKeyState(vk) & 0x8000) != 0;
        }
        allDown = allDown && down;
    }
    setPressed(allDown);
#endif
}

bool PushToTalkMonitor::eventFilter(QObject *watched, QEvent *event)
{
    const bool keyEvent = event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease;
    if (!keyEvent || m_key.isEmpty() || m_pollTimer.isActive()) {
        return QObject::eventFilter(watched, event);
    }
    const auto *keyEventPtr = static_cast<QKeyEvent *>(event);
    // The same event can pass here several times (window, focus widget, parents); the
    // bookkeeping below is idempotent, so that does no harm.
    if (keyEventPtr->isAutoRepeat()) {
        return QObject::eventFilter(watched, event);
    }
    if (event->type() == QEvent::KeyPress) {
        m_heldKeys.insert(keyEventPtr->key());
    } else {
        m_heldKeys.remove(keyEventPtr->key());
    }
    const QKeyCombination wanted = m_key[0];
    const Qt::KeyboardModifiers required = wanted.keyboardModifiers() & kModifierMask;
    Qt::KeyboardModifiers held = keyEventPtr->modifiers() & kModifierMask;
    // A modifier key's own press or release is not yet reflected in modifiers().
    for (int key : std::as_const(m_heldKeys)) {
        held |= modifierFor(key);
    }
    if (event->type() == QEvent::KeyRelease) {
        held &= ~Qt::KeyboardModifiers(modifierFor(keyEventPtr->key()));
    }
    const bool mainDown =
        isModifierKey(wanted.key()) ? held.testFlag(modifierFor(wanted.key())) : m_heldKeys.contains(wanted.key());
    setPressed(mainDown && (held & required) == required);
    return QObject::eventFilter(watched, event);
}

void PushToTalkMonitor::setPressed(bool pressed)
{
    if (pressed != m_pressed) {
        m_pressed = pressed;
        emit pressedChanged(pressed);
    }
}

} // namespace opentalk::ui
