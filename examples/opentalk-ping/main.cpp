// SPDX-License-Identifier: GPL-3.0-only
// opentalk-ping: connects to an OpenTalk server, shows what it announces, measures the
// round-trip time and disconnects. A minimal example of the OpenTalk SDK, and a handy
// tool for checking that a server is reachable.
//
//   opentalk-ping example.org --port 10350 --fingerprint SHA256:AB:CD:...

#include <opentalk/sdk/Client.h>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QTextStream>
#include <QTimer>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("opentalk-ping"));
    QTextStream out(stdout);
    QTextStream err(stderr);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Check that an OpenTalk server is reachable."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("host"), QStringLiteral("Server host name or IP address."));
    const QCommandLineOption portOption(QStringLiteral("port"), QStringLiteral("Server port (default 10350)."),
                                        QStringLiteral("port"), QStringLiteral("10350"));
    const QCommandLineOption fingerprintOption(QStringLiteral("fingerprint"),
                                               QStringLiteral("Expected SHA-256 certificate fingerprint."),
                                               QStringLiteral("fingerprint"));
    const QCommandLineOption verboseOption(QStringLiteral("verbose"), QStringLiteral("Show diagnostic messages."));
    parser.addOptions({portOption, fingerprintOption, verboseOption});
    parser.process(app);
    if (!parser.isSet(verboseOption)) {
        QLoggingCategory::setFilterRules(QStringLiteral("opentalk.*=false"));
    }
    if (parser.positionalArguments().size() != 1) {
        parser.showHelp(2);
    }
    bool portOk = false;
    const uint port = parser.value(portOption).toUInt(&portOk);
    if (!portOk || port == 0 || port > 65535) {
        err << "--port must be between 1 and 65535\n";
        return 2;
    }

    QString error;
    if (!opentalk::sdk::Client::initialize(&error)) {
        err << error << '\n';
        return 1;
    }

    opentalk::sdk::Client client;
    client.setApplication(QStringLiteral("opentalk-ping"), QStringLiteral("1.0"));
    int exitCode = 1;

    QObject::connect(&client, &opentalk::sdk::Client::connected, [&](const opentalk::sdk::ServerInfo &server) {
        out << "Connected to \"" << server.name << "\" (OpenTalk " << server.softwareVersion << ", protocol "
            << server.protocolVersion << ")\n"
            << "Certificate: " << server.certificateFingerprint << '\n';
        out.flush();
        client.ping();
    });
    QObject::connect(&client, &opentalk::sdk::Client::pingReplied, [&](qint64 ms) {
        out << "Round trip: " << ms << " ms\n";
        out.flush();
        exitCode = 0;
        client.disconnectFromServer();
    });
    QObject::connect(&client, &opentalk::sdk::Client::disconnected, [&](const opentalk::sdk::DisconnectReason &r) {
        using Kind = opentalk::sdk::DisconnectReason::Kind;
        if (r.kind != Kind::UserRequested) {
            err << "Failed: " << r.message << '\n';
            if (r.kind == Kind::UntrustedCertificate && !r.certificateFingerprint.isEmpty()) {
                err << "The server presented " << r.certificateFingerprint << "\n"
                    << "If the server administrator confirms this fingerprint, run again with --fingerprint.\n";
            }
            err.flush();
        }
        QCoreApplication::exit(exitCode);
    });

    client.connectToServer({parser.positionalArguments().first(), static_cast<quint16>(port)},
                           {parser.value(fingerprintOption)});
    return QCoreApplication::exec();
}
