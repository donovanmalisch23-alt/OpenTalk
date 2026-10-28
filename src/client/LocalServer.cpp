// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/client/LocalServer.h"

#include "opentalk/logging/Logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QRegularExpression>

namespace opentalk::client {
namespace {

constexpr qsizetype kMaxOutput = 16 * 1024;

} // namespace

LocalServer::LocalServer(QObject *parent)
    : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &LocalServer::readOutput);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            setState(State::Stopped);
            emit failed(tr("The server program could not be started: %1").arg(m_process.errorString()));
        }
    });
    connect(&m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        m_stopTimer.stop();
        readOutput();
        const State previous = m_state;
        setState(State::Stopped);
        if (previous == State::Starting) {
            QString detail = m_output.trimmed().section(QLatin1Char('\n'), -3);
            emit failed(tr("The server stopped while starting (exit code %1).\n%2").arg(exitCode).arg(detail));
        } else if (previous == State::Running && status == QProcess::CrashExit) {
            emit failed(tr("The server stopped unexpectedly."));
        }
        emit stopped();
    });
    m_stopTimer.setSingleShot(true);
    connect(&m_stopTimer, &QTimer::timeout, this, [this] {
        qCWarning(lcClient) << "Local server did not stop in time; ending it";
        m_process.kill();
    });
}

LocalServer::~LocalServer()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.disconnect(this);
        m_process.closeWriteChannel();
        if (!m_process.waitForFinished(5000)) {
            m_process.kill();
            m_process.waitForFinished(2000);
        }
    }
}

QString LocalServer::defaultExecutable()
{
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("opentalk-server.exe");
#else
    const QString name = QStringLiteral("opentalk-server");
#endif
    return QDir(QCoreApplication::applicationDirPath()).filePath(name);
}

void LocalServer::setState(State state)
{
    if (m_state != state) {
        m_state = state;
        emit stateChanged(state);
    }
}

void LocalServer::createAccount(const QString &executable, const QString &dataDir, const QString &userName,
                                const QString &password, const QString &role, std::function<void(const QString &)> done)
{
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::finished, this, [process, done](int exitCode, QProcess::ExitStatus status) {
        const QString output = QString::fromUtf8(process->readAll()).trimmed();
        process->deleteLater();
        if (status == QProcess::NormalExit && exitCode == 0) {
            done(QString());
        } else {
            // The tool prints "opentalk-server: <reason>" on failure.
            const QString reason = output.section(QStringLiteral("opentalk-server: "), -1).trimmed();
            done(reason.isEmpty() ? tr("the account could not be created") : reason);
        }
    });
    connect(process, &QProcess::errorOccurred, this, [process, done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            done(tr("the server program could not be started"));
        }
    });
    process->start(executable, {QStringLiteral("--data-dir"), dataDir, QStringLiteral("--create-account"), userName,
                                QStringLiteral("--role"), role});
    // The password goes through a pipe, never the command line (which other users of the
    // computer can see).
    process->write(password.toUtf8() + '\n');
    process->closeWriteChannel();
}

void LocalServer::start(const Options &options)
{
    if (m_process.state() != QProcess::NotRunning) {
        return;
    }
    m_port = 0;
    m_fingerprint.clear();
    m_output.clear();
    m_partialLine.clear();
    QStringList args{QStringLiteral("--data-dir"),
                     options.dataDir,
                     QStringLiteral("--listen"),
                     options.allowOtherComputers ? QStringLiteral("any") : QStringLiteral("127.0.0.1"),
                     QStringLiteral("--port"),
                     QString::number(options.port),
                     QStringLiteral("--name"),
                     options.name,
                     QStringLiteral("--stop-when-stdin-closes")};
    if (options.allowGuests) {
        args << QStringLiteral("--allow-guests");
    }
    setState(State::Starting);
    qCInfo(lcClient) << "Starting local server on port" << options.port;
    m_process.start(options.executable, args);
}

void LocalServer::stop()
{
    if (m_process.state() == QProcess::NotRunning || m_state == State::Stopping) {
        return;
    }
    setState(State::Stopping);
    m_process.closeWriteChannel(); // the server stops gracefully when its input closes
    m_stopTimer.start(8000);
}

void LocalServer::readOutput()
{
    const QByteArray data = m_process.readAllStandardOutput();
    m_output += QString::fromUtf8(data);
    if (m_output.size() > kMaxOutput) {
        m_output = m_output.right(kMaxOutput);
    }
    if (m_state != State::Starting) {
        return;
    }
    static const QRegularExpression portPattern(QStringLiteral("running on port (\\d+)"));
    static const QRegularExpression fingerprintPattern(QStringLiteral("^\\s*(SHA256:[0-9A-F:]+)\\s*$"),
                                                       QRegularExpression::MultilineOption);
    if (const auto match = portPattern.match(m_output); match.hasMatch()) {
        m_port = static_cast<quint16>(match.captured(1).toUInt());
    }
    if (const auto match = fingerprintPattern.match(m_output); match.hasMatch()) {
        m_fingerprint = match.captured(1);
    }
    if (m_port != 0 && !m_fingerprint.isEmpty() && m_output.contains(QStringLiteral("Press Ctrl+C"))) {
        setState(State::Running);
        qCInfo(lcClient) << "Local server running on port" << m_port;
        emit started(m_port, m_fingerprint);
    }
}

} // namespace opentalk::client
