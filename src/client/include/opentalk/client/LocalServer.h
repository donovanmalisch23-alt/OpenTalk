// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

#include <functional>

namespace opentalk::client {

/// Runs opentalk-server as a child process for "host a server on this computer"
/// (ADR 0004). The server is a separate process so that a problem in the app never takes
/// down a server other people are using, and vice versa.
///
/// The server is told to stop when its standard input closes, so it shuts down cleanly
/// when the app quits, and also if the app crashes.
class LocalServer : public QObject {
    Q_OBJECT

public:
    struct Options {
        QString executable; ///< Path to opentalk-server; see defaultExecutable().
        QString dataDir;
        QString name = QStringLiteral("My OpenTalk server");
        quint16 port = 10350;
        bool allowOtherComputers = false; ///< Listen on all interfaces instead of 127.0.0.1.
        bool allowGuests = true;
    };

    enum class State { Stopped, Starting, Running, Stopping };

    explicit LocalServer(QObject *parent = nullptr);
    /// Stops the server, waiting a few seconds for it to say goodbye to its users.
    ~LocalServer() override;

    /// opentalk-server next to the running program.
    static QString defaultExecutable();

    /// Creates an account in `dataDir` without blocking (hashing takes about a second).
    /// `role` is "owner", "admin", "moderator" or "user". `done` receives an empty string
    /// on success, else an error message.
    void createAccount(const QString &executable, const QString &dataDir, const QString &userName,
                       const QString &password, const QString &role, std::function<void(const QString &error)> done);

    void start(const Options &options);
    void stop();

    State state() const { return m_state; }
    quint16 port() const { return m_port; }
    QString fingerprint() const { return m_fingerprint; }
    /// The server's recent output, for troubleshooting.
    QString output() const { return m_output; }

signals:
    void stateChanged(opentalk::client::LocalServer::State state);
    void started(quint16 port, const QString &fingerprint);
    void stopped();
    void failed(const QString &message);

private:
    void setState(State state);
    void readOutput();

    QProcess m_process;
    QTimer m_stopTimer;
    State m_state = State::Stopped;
    quint16 m_port = 0;
    QString m_fingerprint;
    QString m_output;
    QByteArray m_partialLine;
};

} // namespace opentalk::client
