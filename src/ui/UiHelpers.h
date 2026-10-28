// SPDX-License-Identifier: GPL-3.0-only
// Private helpers shared by the dialogs.
#pragma once

#include <QDateTime>
#include <QLabel>
#include <QMessageBox>
#include <QString>
#include <QWidget>

namespace opentalk::ui::detail {

inline QLabel *buddyLabel(const QString &text, QWidget *buddy)
{
    auto *label = new QLabel(text);
    label->setBuddy(buddy);
    return label;
}

/// Shows a problem with one field and puts focus back on it.
inline void complain(QWidget *parent, QWidget *field, const QString &message)
{
    QMessageBox::warning(parent, parent->windowTitle(), message);
    field->setFocus(Qt::OtherFocusReason);
}

/// Help text under a control that is also the control's accessible description.
inline QLabel *helpLabel(const QString &text, QWidget *describedControl)
{
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    describedControl->setAccessibleDescription(text);
    return label;
}

/// "2026-11-02 14:30" in local time, or `never` for 0.
inline QString localTime(qint64 msSinceEpoch, const QString &never)
{
    if (msSinceEpoch == 0) {
        return never;
    }
    return QDateTime::fromMSecsSinceEpoch(msSinceEpoch).toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

} // namespace opentalk::ui::detail
