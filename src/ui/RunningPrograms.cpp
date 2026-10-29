// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/ui/RunningPrograms.h"

#include <QFileInfo>

#include <algorithm>
#include <cstddef>
#include <utility>

#ifdef Q_OS_WIN
// <windows.h> first: the other Windows headers need it.
#include <windows.h>

#include <string>
#endif

namespace opentalk::ui {

#ifdef Q_OS_WIN
namespace {

struct Collector {
    QList<RunningProgram> programs;
    DWORD self = 0;
};

QString executableName(DWORD processId)
{
    QString name;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process != nullptr) {
        wchar_t path[MAX_PATH] = {};
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(process, 0, path, &size) != 0) {
            name = QFileInfo(QString::fromWCharArray(path, static_cast<qsizetype>(size))).fileName();
        }
        CloseHandle(process);
    }
    return name;
}

BOOL CALLBACK addWindow(HWND window, LPARAM data)
{
    auto *collector = reinterpret_cast<Collector *>(data);
    // Top-level windows people see in the taskbar: visible, titled and not owned by another.
    if (IsWindowVisible(window) == 0 || GetWindow(window, GW_OWNER) != nullptr) {
        return TRUE;
    }
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) {
        return TRUE;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == 0 || processId == collector->self) {
        return TRUE;
    }
    for (const RunningProgram &program : std::as_const(collector->programs)) {
        if (program.processId == processId) {
            return TRUE; // one entry per program, named after its first window
        }
    }
    std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(window, title.data(), length + 1);
    if (copied <= 0) {
        return TRUE;
    }
    collector->programs.append({processId, QString::fromWCharArray(title.c_str(), copied), executableName(processId)});
    return TRUE;
}

} // namespace
#endif

QList<RunningProgram> runningPrograms()
{
    QList<RunningProgram> programs;
#ifdef Q_OS_WIN
    Collector collector;
    collector.self = GetCurrentProcessId();
    EnumWindows(&addWindow, reinterpret_cast<LPARAM>(&collector));
    programs = collector.programs;
#endif
    std::sort(programs.begin(), programs.end(), [](const RunningProgram &a, const RunningProgram &b) {
        return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });
    return programs;
}

} // namespace opentalk::ui
