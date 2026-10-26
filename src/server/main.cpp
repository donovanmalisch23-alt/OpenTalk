// SPDX-License-Identifier: GPL-3.0-only
// opentalk-server: the headless OpenTalk server. It needs no window and runs the same
// way on a desktop PC, a dedicated machine or a VPS.

#include "ShutdownSignal.h"

#include "opentalk/logging/Logging.h"
#include "opentalk/security/Tls.h"
#include "opentalk/server/Server.h"

#include <QCommandLineParser>
#include <QCoreApplication>

#include <cstdio>

using namespace opentalk;

namespace {

void printLine(const QString &text)
{
    std::fputs(qPrintable(text + QLatin1Char('\n')), stdout);
    std::fflush(stdout);
}

int fail(const QString &message)
{
    std::fputs(qPrintable(QStringLiteral("opentalk-server: ") + message + QLatin1Char('\n')), stderr);
    return 1;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("opentalk-server"));
    QCoreApplication::setApplicationVersion(QStringLiteral(OPENTALK_VERSION_STRING));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("OpenTalk server (headless)"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption listenOption(QStringLiteral("listen"),
                                          QStringLiteral("Address to listen on (default: all interfaces)."),
                                          QStringLiteral("address"), QStringLiteral("any"));
    const QCommandLineOption portOption(QStringLiteral("port"), QStringLiteral("TCP port (default: 10350)."),
                                        QStringLiteral("port"), QStringLiteral("10350"));
    const QCommandLineOption certOption(QStringLiteral("tls-cert"),
                                        QStringLiteral("PEM certificate file, optionally followed by its chain."),
                                        QStringLiteral("file"));
    const QCommandLineOption keyOption(QStringLiteral("tls-key"), QStringLiteral("PEM private key file (EC or RSA)."),
                                       QStringLiteral("file"));
    const QCommandLineOption nameOption(QStringLiteral("name"), QStringLiteral("Server name shown to clients."),
                                        QStringLiteral("name"), QStringLiteral("OpenTalk server"));
    const QCommandLineOption maxOption(QStringLiteral("max-users"),
                                       QStringLiteral("Maximum simultaneous connections (default: 200)."),
                                       QStringLiteral("count"), QStringLiteral("200"));
    const QCommandLineOption logOption(QStringLiteral("log-level"),
                                       QStringLiteral("warning, info or debug (default: info)."),
                                       QStringLiteral("level"), QStringLiteral("info"));
    parser.addOptions({listenOption, portOption, certOption, keyOption, nameOption, maxOption, logOption});
    parser.process(app);

    const std::optional<logging::Level> level = logging::levelFromString(parser.value(logOption));
    if (!level) {
        return fail(QStringLiteral("--log-level must be warning, info or debug"));
    }
    logging::install({QString(), *level, true});

    QString error;
    if (!security::initializeTls(&error)) {
        return fail(error);
    }
    if (!parser.isSet(certOption) || !parser.isSet(keyOption)) {
        return fail(QStringLiteral("--tls-cert and --tls-key are required. See docs/server-administration/"
                                   "quick-start.md for how to create a certificate."));
    }

    server::ServerConfig config;
    const QString listen = parser.value(listenOption);
    if (listen != QLatin1String("any")) {
        if (!config.address.setAddress(listen)) {
            return fail(QStringLiteral("--listen is not a valid IP address: %1").arg(listen));
        }
    }
    bool ok = false;
    const uint port = parser.value(portOption).toUInt(&ok);
    if (!ok || port > 65535) {
        return fail(QStringLiteral("--port must be between 0 and 65535 (0 picks a free port)"));
    }
    config.port = static_cast<quint16>(port);
    const int maxUsers = parser.value(maxOption).toInt(&ok);
    if (!ok || maxUsers < 1 || maxUsers > 100000) {
        return fail(QStringLiteral("--max-users must be between 1 and 100000"));
    }
    config.maxSessions = maxUsers;
    const QString name = parser.value(nameOption).trimmed();
    if (name.isEmpty() || name.size() > protocol::kMaxNameLength) {
        return fail(QStringLiteral("--name must be 1 to %1 characters").arg(protocol::kMaxNameLength));
    }
    config.session.serverName = name;
    config.session.serverVersion = QCoreApplication::applicationVersion();

    const std::optional<QSslConfiguration> tls =
        security::serverTlsConfiguration(parser.value(certOption), parser.value(keyOption), &error);
    if (!tls) {
        return fail(error);
    }
    config.tls = *tls;

    server::Server server(config);
    if (!server.start(&error)) {
        return fail(error);
    }
    printLine(QStringLiteral("OpenTalk server \"%1\" is running on port %2.").arg(name).arg(server.serverPort()));
    printLine(QStringLiteral("Certificate fingerprint (share it so users can verify this server):"));
    printLine(QStringLiteral("  ") + server.certificateFingerprint());
    printLine(QStringLiteral("Press Ctrl+C to stop."));

    ShutdownSignal shutdown;
    QObject::connect(&shutdown, &ShutdownSignal::requested, &server, &server::Server::stop);
    QObject::connect(&server, &server::Server::stopped, &app, &QCoreApplication::quit);

    const int code = QCoreApplication::exec();
    logging::shutdown();
    return code;
}
