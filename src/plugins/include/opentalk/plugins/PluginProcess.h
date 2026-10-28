// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/plugins/PluginRegistry.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QTimer>

namespace opentalk::plugins {

/// One running plugin: its own process, talking to the app in JSON lines over its
/// standard input and output (see docs/plugins/ipc.md). Standard error goes to the
/// plugin's log file.
///
/// It enforces the transport rules: the handshake, line length, message rate and
/// misbehaviour limits; and it restarts a plugin that crashes, a few times. What each
/// message may do is checked by PluginHost.
class PluginProcess : public QObject {
    Q_OBJECT

public:
    enum class State {
        Stopped,
        Starting, ///< Started; waiting for its "ready" message.
        Running,
        Stopping,
        Failed, ///< Gave up: see problem().
    };
    Q_ENUM(State)

    struct Limits {
        int maxLineBytes = 64 * 1024;
        double messagesPerSecond = 20.0;
        double burst = 40.0;
        int maxViolations = 20;
        int readyTimeoutMs = 10'000;
        int stopGraceMs = 3'000;
        int maxRestarts = 3; ///< Within restartWindowMs; then the plugin is turned off.
        int restartWindowMs = 60'000;
        qint64 maxLogBytes = 1024 * 1024;
    };

    PluginProcess(const InstalledPlugin &plugin, QString dataDir, QString logFile, Limits limits,
                  QObject *parent = nullptr);
    ~PluginProcess() override;

    QString id() const { return m_plugin.manifest.id; }
    const InstalledPlugin &plugin() const { return m_plugin; }
    State state() const { return m_state; }
    QString problem() const { return m_problem; }
    QString logFile() const { return m_logFile; }

    void start();
    /// Asks the plugin to exit ("shutdown"), then ends it after the grace period.
    void stop();
    /// Sends a message if the plugin is running (or starting, for the handshake).
    void send(const QJsonObject &message);
    /// Counts a broken rule; too many and the plugin is stopped with `why`.
    void violation(const QString &why);

signals:
    void stateChanged(opentalk::plugins::PluginProcess::State state);
    /// The plugin finished its handshake ("ready" message).
    void ready(const QJsonObject &message);
    /// Any other message from the plugin, already parsed and rate-limited.
    void messageReceived(const QJsonObject &message);
    /// Something the user should know: a crash, a restart, misbehaviour.
    void problemReported(const QString &text);

private:
    void setState(State state);
    void launch();
    void readOutput();
    void readErrors();
    void handleLine(const QByteArray &line);
    void finished(int exitCode, QProcess::ExitStatus status);
    void fail(const QString &why);
    void writeLog(const QByteArray &text);

    InstalledPlugin m_plugin;
    QString m_dataDir;
    QString m_logFile;
    Limits m_limits;
    QProcess *m_process = nullptr;
    State m_state = State::Stopped;
    QString m_problem;
    QByteArray m_buffer;
    double m_tokens = 0;
    QElapsedTimer m_tokenClock;
    int m_violations = 0;
    QList<qint64> m_crashTimes;
    QElapsedTimer m_lifetime;
    QTimer m_readyTimer;
    QTimer m_restartTimer;
    QTimer m_killTimer;
    bool m_stopRequested = false;
};

} // namespace opentalk::plugins
