// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/logging/Logging.h"

#include <QFile>
#include <QMutex>
#include <QMutexLocker>

#include <cstdio>
#include <memory>

Q_LOGGING_CATEGORY(lcApp, "opentalk.app")
Q_LOGGING_CATEGORY(lcUi, "opentalk.ui")
Q_LOGGING_CATEGORY(lcStorage, "opentalk.storage")
Q_LOGGING_CATEGORY(lcSecurity, "opentalk.security")
Q_LOGGING_CATEGORY(lcClient, "opentalk.client")
Q_LOGGING_CATEGORY(lcServer, "opentalk.server")

namespace opentalk::logging {
namespace {

struct State {
    QMutex mutex;
    std::unique_ptr<QFile> file;
    Level minimumLevel = Level::Info;
    bool alsoWriteToStderr = true;
    QtMessageHandler previousHandler = nullptr;
    bool installed = false;
};

State &state()
{
    static State s;
    return s;
}

int severity(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return 0;
    case QtInfoMsg:
        return 1;
    case QtWarningMsg:
        return 2;
    case QtCriticalMsg:
        return 3;
    case QtFatalMsg:
        return 4;
    }
    return 4;
}

int severity(Level level)
{
    switch (level) {
    case Level::Debug:
        return 0;
    case Level::Info:
        return 1;
    case Level::Warning:
        return 2;
    }
    return 1;
}

QString typeLabel(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO ");
    case QtWarningMsg:
        return QStringLiteral("WARN ");
    case QtCriticalMsg:
        return QStringLiteral("ERROR");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }
    return QStringLiteral("?????");
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    State &s = state();
    QMutexLocker lock(&s.mutex);
    if (severity(type) < severity(s.minimumLevel)) {
        return;
    }
    const QByteArray line =
        formatLine(type, context.category, message, QDateTime::currentDateTimeUtc()).toUtf8() + '\n';
    if (s.file && s.file->isOpen()) {
        s.file->write(line);
        s.file->flush();
    }
    if (s.alsoWriteToStderr) {
        std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stderr);
        std::fflush(stderr);
    }
}

} // namespace

std::optional<Level> levelFromString(const QString &text)
{
    if (text == QLatin1String("debug")) {
        return Level::Debug;
    }
    if (text == QLatin1String("info")) {
        return Level::Info;
    }
    if (text == QLatin1String("warning")) {
        return Level::Warning;
    }
    return std::nullopt;
}

QString levelToString(Level level)
{
    switch (level) {
    case Level::Debug:
        return QStringLiteral("debug");
    case Level::Info:
        return QStringLiteral("info");
    case Level::Warning:
        return QStringLiteral("warning");
    }
    return QStringLiteral("info");
}

QString formatLine(QtMsgType type, const char *category, const QString &message, const QDateTime &timestampUtc)
{
    const QString categoryName =
        (category != nullptr && *category != '\0') ? QString::fromUtf8(category) : QStringLiteral("default");
    // Messages are single-line by contract so log parsers stay simple.
    QString singleLine = message;
    singleLine.replace(QLatin1Char('\r'), QLatin1String("\\r"));
    singleLine.replace(QLatin1Char('\n'), QLatin1String("\\n"));
    return timestampUtc.toUTC().toString(Qt::ISODateWithMs) + QLatin1Char(' ') + typeLabel(type) + QLatin1Char(' ') +
           categoryName + QLatin1String(": ") + singleLine;
}

bool install(const Options &options)
{
    State &s = state();
    bool fileOk = true;
    {
        QMutexLocker lock(&s.mutex);
        s.minimumLevel = options.minimumLevel;
        s.alsoWriteToStderr = options.alsoWriteToStderr;
        s.file.reset();
        if (!options.filePath.isEmpty()) {
            auto file = std::make_unique<QFile>(options.filePath);
            if (file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                s.file = std::move(file);
            } else {
                fileOk = false;
            }
        }
        if (!s.installed) {
            s.previousHandler = qInstallMessageHandler(messageHandler);
            s.installed = true;
        }
    }
    // Debug output is filtered by our own level check, so let Qt deliver it.
    QLoggingCategory::setFilterRules(QStringLiteral("opentalk.*.debug=true"));
    return fileOk;
}

void setMinimumLevel(Level level)
{
    State &s = state();
    QMutexLocker lock(&s.mutex);
    s.minimumLevel = level;
}

void shutdown()
{
    State &s = state();
    QMutexLocker lock(&s.mutex);
    if (s.installed) {
        qInstallMessageHandler(s.previousHandler);
        s.installed = false;
        s.previousHandler = nullptr;
    }
    s.file.reset();
}

} // namespace opentalk::logging
