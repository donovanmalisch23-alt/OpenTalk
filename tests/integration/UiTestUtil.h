// SPDX-License-Identifier: GPL-3.0-only
// Helpers for tests that drive real windows and dialogs.
#pragma once

#include <QAbstractSpinBox>
#include <QAccessible>
#include <QApplication>
#include <QPushButton>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <memory>

namespace opentalk::test {

/// Focusable, visible widgets under `root` that a screen reader would announce without a name.
inline QStringList unnamedFocusableWidgets(QWidget *root)
{
    QStringList problems;
    for (QWidget *widget : root->findChildren<QWidget *>()) {
        if (widget->focusPolicy() == Qt::NoFocus || !widget->isVisibleTo(root)) {
            continue;
        }
        if (qobject_cast<QAbstractSpinBox *>(widget->parentWidget()) != nullptr) {
            continue; // a spin box's editor; the spin box itself is checked
        }
        QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(widget);
        if (iface == nullptr || iface->text(QAccessible::Name).trimmed().isEmpty()) {
            problems << QString::fromLatin1(widget->metaObject()->className()) + QLatin1Char(' ') +
                            widget->objectName();
        }
    }
    return problems;
}

template <typename T> T *named(QWidget *root, const QString &accessibleName)
{
    for (T *widget : root->findChildren<T *>()) {
        if (widget->accessibleName() == accessibleName) {
            return widget;
        }
    }
    return nullptr;
}

inline QPushButton *buttonWithText(QWidget *root, const QString &text)
{
    for (QPushButton *b : root->findChildren<QPushButton *>()) {
        if (b->text() == text) {
            return b;
        }
    }
    return nullptr;
}

/// Calls `answer` with the next modal dialog of type T that opens (at most one), as a
/// person would answer it. Gives up after about ten seconds.
template <typename T> void whenDialogOpens(std::function<void(T *)> answer)
{
    auto *timer = new QTimer;
    timer->setInterval(20);
    auto remaining = std::make_shared<int>(500);
    QObject::connect(timer, &QTimer::timeout, [timer, answer, remaining] {
        if (auto *dialog = qobject_cast<T *>(QApplication::activeModalWidget())) {
            timer->stop();
            timer->deleteLater();
            answer(dialog);
        } else if (--*remaining <= 0) {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start();
}

} // namespace opentalk::test
