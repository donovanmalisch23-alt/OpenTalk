// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/plugins/PluginProcess.h"

#include "opentalk/logging/Logging.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>

#include <algorithm>
#include <cmath>

namespace opentalk::plugins {
namespace {

constexpr qint64 kMaxUnreadBytes = 4 * 1024 * 1024;

} // namespace

PluginProcess::PluginProcess(const InstalledPlugin &plugin, QString dataDir, QString logFile, Limits limits,
                             QObject *parent)
    : QObject(parent)
    , m_plugin(plugin)
    , m_dataDir(std::move(dataDir))
    , m_logFile(std::move(logFile))
    , m_limits(limits)
{
    m_lifetime.start();
    m_readyTimer.setSingleShot(true);
    connect(&m_readyTimer, &QTimer::timeout, this,
            [this] { fail(tr("it did not start in time (no \"ready\" message)")); });
    m_restartTimer.setSingleShot(true);
    connect(&m_restartTimer, &QTimer::timeout, this, &PluginProcess::launch);
    m_killTimer.setSingleShot(true);
    connect(&m_killTimer, &QTimer::timeout, this, [this] {
        if (m_process != nullptr && m_process->state() != QProcess::NotRunning) {
            m_process->kill();
        }
    });
}

PluginProcess::~PluginProcess()
{
    if (m_process != nullptr) {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
            m_process->waitForFinished(2000);
        }
    }
}

void PluginProcess::setState(State state)
{
    if (state != m_state) {
        m_state = state;
        emit stateChanged(state);
    }
}

void PluginProcess::start()
{
    if (m_state == State::Starting || m_state == State::Running) {
        return;
    }
    m_stopRequested = false;
    m_crashTimes.clear();
    m_problem.clear();
    launch();
}

void PluginProcess::launch()
{
    QString error;
    const auto command = launchCommand(m_plugin.manifest, m_plugin.directory, &error);
    if (!command) {
        fail(error);
        return;
    }
    QDir().mkpath(m_dataDir);
    QDir().mkpath(QFileInfo(m_logFile).absolutePath());
    if (m_process != nullptr) {
        m_process->disconnect(this);
        m_process->deleteLater();
    }
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(m_plugin.directory);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("OPENTALK_PLUGIN_ID"), m_plugin.manifest.id);
    environment.insert(QStringLiteral("OPENTALK_PLUGIN_DATA"), QDir::toNativeSeparators(m_dataDir));
    environment.insert(QStringLiteral("OPENTALK_PLUGIN_API"), QString::number(kPluginApiVersion));
    environment.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
#ifdef Q_OS_WIN
    // Last on the search path, so a plugin's own copies of libraries win, but a C++
    // plugin built with the same Qt can use the Qt libraries that come with OpenTalk.
    const QString path = environment.value(QStringLiteral("PATH"));
    environment.insert(QStringLiteral("PATH"),
                       path + QLatin1Char(';') + QDir::toNativeSeparators(QCoreApplication::applicationDirPath()));
#endif
    m_process->setProcessEnvironment(environment);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &PluginProcess::readOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &PluginProcess::readErrors);
    connect(m_process, &QProcess::finished, this, &PluginProcess::finished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            fail(tr("it could not be started: %1").arg(m_process->errorString()));
        }
    });
    m_buffer.clear();
    m_tokens = m_limits.burst;
    m_tokenClock.start();
    m_violations = 0;
    setState(State::Starting);
    writeLog(QStringLiteral("--- started %1\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate)).toUtf8());
    m_process->start(command->program, command->arguments);
    m_readyTimer.start(m_limits.readyTimeoutMs);

    QJsonArray granted;
    for (PluginCapability c : m_plugin.granted) {
        granted.append(capabilityName(c));
    }
    send(QJsonObject{{QStringLiteral("type"), QStringLiteral("hello")},
                     {QStringLiteral("apiVersion"), kPluginApiVersion},
                     {QStringLiteral("app"), QStringLiteral("OpenTalk")},
                     {QStringLiteral("appVersion"), QCoreApplication::applicationVersion()},
                     {QStringLiteral("pluginId"), m_plugin.manifest.id},
                     {QStringLiteral("granted"), granted},
                     {QStringLiteral("dataDir"), QDir::toNativeSeparators(m_dataDir)}});
}

void PluginProcess::stop()
{
    m_stopRequested = true;
    m_restartTimer.stop();
    m_readyTimer.stop();
    if (m_process == nullptr || m_process->state() == QProcess::NotRunning) {
        if (m_state != State::Failed) {
            setState(State::Stopped);
        }
        return;
    }
    setState(State::Stopping);
    m_process->write(R"({"type":"shutdown"})"
                     "\n");
    m_process->closeWriteChannel();
    m_killTimer.start(m_limits.stopGraceMs);
}

void PluginProcess::send(const QJsonObject &message)
{
    if (m_process == nullptr || m_process->state() == QProcess::NotRunning ||
        (m_state != State::Starting && m_state != State::Running)) {
        return;
    }
    if (m_process->bytesToWrite() > kMaxUnreadBytes) {
        fail(tr("it stopped reading its messages"));
        return;
    }
    // Compact JSON never contains a raw line break, so each message is exactly one line.
    m_process->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
}

void PluginProcess::readOutput()
{
    m_buffer += m_process->readAllStandardOutput();
    qsizetype newline = 0;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (line.size() > m_limits.maxLineBytes) {
            fail(tr("it sent a message longer than %1 KiB").arg(m_limits.maxLineBytes / 1024));
            return;
        }
        handleLine(line.trimmed());
        if (m_state == State::Failed || m_process == nullptr) {
            return;
        }
    }
    if (m_buffer.size() > m_limits.maxLineBytes) {
        fail(tr("it sent a message longer than %1 KiB").arg(m_limits.maxLineBytes / 1024));
    }
}

void PluginProcess::handleLine(const QByteArray &line)
{
    if (line.isEmpty() || (m_state != State::Starting && m_state != State::Running)) {
        return;
    }
    const double elapsed = static_cast<double>(m_tokenClock.restart()) / 1000.0;
    m_tokens = std::min(m_limits.burst, m_tokens + elapsed * m_limits.messagesPerSecond);
    if (m_tokens < 1.0) {
        violation(tr("it sends messages too fast"));
        return;
    }
    m_tokens -= 1.0;
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        violation(tr("it sent something that is not a JSON object"));
        return;
    }
    const QJsonObject message = document.object();
    const QString type = message.value(QLatin1String("type")).toString();
    if (type == QLatin1String("ready")) {
        if (m_state != State::Starting) {
            violation(tr("it said \"ready\" twice"));
            return;
        }
        m_readyTimer.stop();
        setState(State::Running);
        emit ready(message);
        return;
    }
    if (m_state != State::Running) {
        violation(tr("it sent \"%1\" before \"ready\"").arg(type.left(40)));
        return;
    }
    if (type == QLatin1String("log")) {
        writeLog(message.value(QLatin1String("text")).toString().left(2000).toUtf8() + '\n');
        return;
    }
    emit messageReceived(message);
}

void PluginProcess::readErrors()
{
    writeLog(m_process->readAllStandardError());
}

void PluginProcess::finished(int exitCode, QProcess::ExitStatus status)
{
    m_readyTimer.stop();
    m_killTimer.stop();
    writeLog(QStringLiteral("--- exited (%1)\n")
                 .arg(status == QProcess::CrashExit ? QStringLiteral("crashed") : QString::number(exitCode))
                 .toUtf8());
    if (m_state == State::Failed) {
        return;
    }
    if (m_stopRequested || m_state == State::Stopping) {
        setState(State::Stopped);
        return;
    }
    // An unexpected exit: restart a few times, then give up.
    const qint64 now = m_lifetime.elapsed();
    m_crashTimes.append(now);
    m_crashTimes.erase(std::remove_if(m_crashTimes.begin(), m_crashTimes.end(),
                                      [&](qint64 t) { return now - t > m_limits.restartWindowMs; }),
                       m_crashTimes.end());
    if (m_crashTimes.size() > m_limits.maxRestarts) {
        fail(tr("it stopped unexpectedly %1 times in a row, so it was turned off. Start it again from the "
                "plugin manager when it is fixed.")
                 .arg(m_crashTimes.size()));
        return;
    }
    qCInfo(lcPlugins).noquote() << "Plugin" << m_plugin.manifest.id << "exited unexpectedly; restarting";
    emit problemReported(tr("%1 stopped unexpectedly and is being restarted.").arg(m_plugin.manifest.name));
    setState(State::Starting);
    const int delay = 1000 * (1 << std::min(static_cast<int>(m_crashTimes.size()) - 1, 4));
    m_restartTimer.start(delay);
}

void PluginProcess::fail(const QString &why)
{
    if (m_state == State::Failed) {
        return;
    }
    m_problem = why;
    m_readyTimer.stop();
    m_restartTimer.stop();
    writeLog(QStringLiteral("--- turned off: %1\n").arg(why).toUtf8());
    qCWarning(lcPlugins).noquote() << "Plugin" << m_plugin.manifest.id << "turned off:" << why;
    setState(State::Failed);
    emit problemReported(tr("%1 was turned off: %2").arg(m_plugin.manifest.name, why));
    if (m_process != nullptr && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
    }
}

void PluginProcess::violation(const QString &why)
{
    writeLog(QStringLiteral("--- refused: %1\n").arg(why).toUtf8());
    if (++m_violations >= m_limits.maxViolations) {
        fail(tr("it broke the plugin rules too often (last: %1)").arg(why));
    }
}

void PluginProcess::writeLog(const QByteArray &text)
{
    if (text.isEmpty() || m_logFile.isEmpty()) {
        return;
    }
    if (QFileInfo(m_logFile).size() > m_limits.maxLogBytes) {
        QFile::remove(m_logFile + QStringLiteral(".old"));
        QFile::rename(m_logFile, m_logFile + QStringLiteral(".old"));
    }
    QFile file(m_logFile);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        file.write(text);
    }
}

} // namespace opentalk::plugins
