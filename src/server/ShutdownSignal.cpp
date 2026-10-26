// SPDX-License-Identifier: GPL-3.0-only
#include "ShutdownSignal.h"

#include <QMetaObject>
#include <QPointer>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <QSocketNotifier>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

QPointer<ShutdownSignal> g_instance;

#ifdef Q_OS_WIN
BOOL WINAPI consoleHandler(DWORD type)
{
    switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        // Runs on a separate thread; a queued invocation is thread-safe.
        if (g_instance) {
            QMetaObject::invokeMethod(g_instance.data(), &ShutdownSignal::requested, Qt::QueuedConnection);
        }
        return TRUE;
    default:
        return FALSE;
    }
}
#else
int g_pipe[2] = {-1, -1};

extern "C" void signalHandler(int)
{
    // Only async-signal-safe work here: wake the event loop through the pipe.
    const char byte = 1;
    [[maybe_unused]] const ssize_t written = ::write(g_pipe[1], &byte, 1);
}
#endif

} // namespace

ShutdownSignal::ShutdownSignal(QObject *parent)
    : QObject(parent)
{
    g_instance = this;
#ifdef Q_OS_WIN
    SetConsoleCtrlHandler(consoleHandler, TRUE);
#else
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_pipe) == 0) {
        auto *notifier = new QSocketNotifier(g_pipe[0], QSocketNotifier::Read, this);
        connect(notifier, &QSocketNotifier::activated, this, [this] {
            char byte = 0;
            [[maybe_unused]] const ssize_t got = ::read(g_pipe[0], &byte, 1);
            emit requested();
        });
        struct sigaction action {};
        action.sa_handler = signalHandler;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;
        sigaction(SIGINT, &action, nullptr);
        sigaction(SIGTERM, &action, nullptr);
    }
#endif
}

ShutdownSignal::~ShutdownSignal()
{
#ifdef Q_OS_WIN
    SetConsoleCtrlHandler(consoleHandler, FALSE);
#endif
    g_instance = nullptr;
}
