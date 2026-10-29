// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QList>
#include <QString>

namespace opentalk::ui {

/// A program whose sound can be shared on its own (Audio, "Share one program's sound").
struct RunningProgram {
    quint32 processId = 0;
    QString title;      ///< The title of its window.
    QString executable; ///< The program file's name, for example "vlc.exe", or empty.
};

/// Programs with a visible window, except OpenTalk itself, sorted by title. Windows only;
/// elsewhere the list is empty.
QList<RunningProgram> runningPrograms();

} // namespace opentalk::ui
