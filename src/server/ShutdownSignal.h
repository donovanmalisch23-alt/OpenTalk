// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>

/// Turns Ctrl+C (and SIGTERM on Unix, console close on Windows) into a Qt signal
/// delivered on the main thread, so the server can shut down gracefully.
class ShutdownSignal : public QObject {
    Q_OBJECT

public:
    explicit ShutdownSignal(QObject *parent = nullptr);
    ~ShutdownSignal() override;

signals:
    void requested();
};
