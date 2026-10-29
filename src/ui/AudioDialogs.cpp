// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/ui/AudioDialogs.h"

#include "opentalk/ui/PushToTalkMonitor.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace opentalk::ui {
namespace {

Qt::KeyboardModifier modifierOf(int key)
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

} // namespace

KeyCaptureDialog::KeyCaptureDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Choose push-to-talk key"));
    m_prompt = new QLabel(tr("Press and release the key or key combination you want to hold while talking, "
                             "for example F12 or Control+Windows. Press Escape to cancel."));
    m_prompt->setWordWrap(true);
    m_prompt->setFocusPolicy(Qt::StrongFocus);
    m_prompt->setAccessibleName(windowTitle());
    m_prompt->setAccessibleDescription(m_prompt->text());
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_prompt);
    setFocusProxy(m_prompt);
    resize(420, 120);
}

bool KeyCaptureDialog::event(QEvent *event)
{
    // Tab, Enter and shortcuts are keys like any other here.
    if (event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
    }
    if (event->type() == QEvent::KeyPress) {
        keyPressEvent(static_cast<QKeyEvent *>(event));
        return true;
    }
    return QDialog::event(event);
}

void KeyCaptureDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat()) {
        return;
    }
    const int key = event->key();
    if (key == Qt::Key_Escape && m_down.isEmpty()) {
        reject();
        return;
    }
    if (key == 0 || key == Qt::Key_unknown) {
        return;
    }
    m_down.insert(key);
    if (const Qt::KeyboardModifier modifier = modifierOf(key); modifier != Qt::NoModifier) {
        m_modifiers |= modifier;
        if (m_mainKey == 0 || modifierOf(m_mainKey) != Qt::NoModifier) {
            m_mainKey = key; // the last modifier pressed is the "key" of a modifier-only combination
        }
    } else {
        m_mainKey = key;
        m_modifiers |= event->modifiers() & Qt::KeypadModifier;
    }
}

void KeyCaptureDialog::keyReleaseEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat()) {
        return;
    }
    m_down.remove(event->key());
    if (!m_down.isEmpty() || m_mainKey == 0) {
        return;
    }
    Qt::KeyboardModifiers modifiers = m_modifiers & ~Qt::KeyboardModifiers(modifierOf(m_mainKey));
    const QKeySequence candidate(QKeyCombination(modifiers, static_cast<Qt::Key>(m_mainKey)));
    m_mainKey = 0;
    m_modifiers = {};
    if (!PushToTalkMonitor::isUsable(candidate)) {
        m_prompt->setText(tr("%1 cannot be used for push-to-talk. Try another key, or press Escape to cancel.")
                              .arg(PushToTalkMonitor::describe(candidate)));
        return;
    }
    m_key = candidate;
    accept();
}

UserVolumeDialog::UserVolumeDialog(const QString &userName, int percent, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Volume for %1").arg(userName));
    m_spin = new QSpinBox;
    m_spin->setRange(0, 400);
    m_spin->setSuffix(tr(" %"));
    m_spin->setValue(percent);
    m_spin->setAccessibleName(tr("Volume for %1, in percent").arg(userName));
    m_spin->setAccessibleDescription(tr("100 is unchanged, 0 is silent. Only affects what you hear."));
    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(0, 400);
    m_slider->setPageStep(25);
    m_slider->setValue(percent);
    m_slider->setFocusPolicy(Qt::NoFocus); // the spin box is the keyboard control; this is visual
    connect(m_spin, &QSpinBox::valueChanged, m_slider, &QSlider::setValue);
    connect(m_slider, &QSlider::valueChanged, m_spin, &QSpinBox::setValue);
    connect(m_spin, &QSpinBox::valueChanged, this, &UserVolumeDialog::percentChanged);

    auto *label = new QLabel(tr("&Volume:"));
    label->setBuddy(m_spin);
    auto *mute = new QPushButton(tr("&Silence"));
    auto *reset = new QPushButton(tr("&Normal (100%)"));
    connect(mute, &QPushButton::clicked, this, [this] { m_spin->setValue(0); });
    connect(reset, &QPushButton::clicked, this, [this] { m_spin->setValue(100); });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *row = new QHBoxLayout;
    row->addWidget(label);
    row->addWidget(m_spin);
    row->addWidget(m_slider, 1);
    auto *quick = new QHBoxLayout;
    quick->addWidget(mute);
    quick->addWidget(reset);
    quick->addStretch(1);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(row);
    layout->addLayout(quick);
    layout->addWidget(buttons);
    m_spin->setFocus();
}

int UserVolumeDialog::percent() const
{
    return m_spin->value();
}

} // namespace opentalk::ui
