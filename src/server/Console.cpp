// SPDX-License-Identifier: GPL-3.0-only
#include "Console.h"

#include <cstdio>
#include <iostream>
#include <string>

#ifdef Q_OS_WIN
#include <io.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace {

/// Turns terminal echo off for its lifetime, if standard input is a terminal.
class EchoOff {
public:
    EchoOff()
    {
#ifdef Q_OS_WIN
        m_handle = GetStdHandle(STD_INPUT_HANDLE);
        if (_isatty(_fileno(stdin)) && GetConsoleMode(m_handle, &m_mode)) {
            m_active = SetConsoleMode(m_handle, m_mode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT)) != 0;
        }
#else
        if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &m_saved) == 0) {
            termios silent = m_saved;
            silent.c_lflag &= ~static_cast<tcflag_t>(ECHO);
            m_active = tcsetattr(STDIN_FILENO, TCSAFLUSH, &silent) == 0;
        }
#endif
    }
    ~EchoOff()
    {
        if (!m_active) {
            return;
        }
#ifdef Q_OS_WIN
        SetConsoleMode(m_handle, m_mode);
#else
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &m_saved);
#endif
        std::fputs("\n", stderr);
    }
    EchoOff(const EchoOff &) = delete;
    EchoOff &operator=(const EchoOff &) = delete;

private:
    bool m_active = false;
#ifdef Q_OS_WIN
    HANDLE m_handle = nullptr;
    DWORD m_mode = 0;
#else
    termios m_saved{};
#endif
};

} // namespace

std::optional<QString> readSecretLine(const QString &prompt)
{
    std::fputs(qPrintable(prompt), stderr);
    std::fflush(stderr);
    EchoOff echoOff;
    std::string line;
    if (!std::getline(std::cin, line)) {
        return std::nullopt;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return QString::fromStdString(line);
}
