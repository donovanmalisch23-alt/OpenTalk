// SPDX-License-Identifier: GPL-3.0-only
// opentalk-ping: connects to an OpenTalk server, shows what it announces, measures the
// round-trip time and disconnects. A minimal example of the OpenTalk SDK, and a handy
// tool for checking that a server is reachable.
//
//   opentalk-ping example.org --port 10350 --fingerprint SHA256:AB:CD:...
//   opentalk-ping example.org --user alice < password.txt
//   opentalk-ping example.org --fingerprint SHA256:AB:CD:... --voice
//
// It logs in as a guest unless --user is given (the password is then read from the
// first line of standard input). With --voice it also checks that voice gets through
// on the faster UDP path. Run it from outside the server's network to check that
// people on the internet can reach the server.
//
// Exit codes: 0 reachable (and, with --voice, UDP works); 1 not reachable; 2 bad
// arguments; 3 reachable, but voice has to use the main connection because UDP is blocked.

#include <opentalk/sdk/Client.h>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QTextStream>
#include <QTimer>

#include <iostream>
#include <string>

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
    const QCommandLineOption userOption(QStringLiteral("user"),
                                        QStringLiteral("Log in to this account; the password is read from "
                                                       "standard input."),
                                        QStringLiteral("username"));
    const QCommandLineOption guestOption(
        QStringLiteral("guest-name"), QStringLiteral("Nickname when logging in as a guest (default: opentalk-ping)."),
        QStringLiteral("name"), QStringLiteral("opentalk-ping"));
    const QCommandLineOption voiceOption(QStringLiteral("voice"),
                                         QStringLiteral("Also check that voice gets through on UDP (exit code 3 "
                                                        "if only the slower fallback works)."));
    parser.addOptions({portOption, fingerprintOption, verboseOption, userOption, guestOption, voiceOption});
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
    const bool checkVoice = parser.isSet(voiceOption);
    bool pinged = false;
    bool voiceChecked = !checkVoice;
    QTimer voiceTimeout;
    voiceTimeout.setSingleShot(true);
    voiceTimeout.setInterval(8000); // UDP is tried a few times before giving up
    const auto finishIfDone = [&] {
        if (pinged && voiceChecked) {
            client.disconnectFromServer();
        }
    };
    const auto reportVoice = [&](bool udp) {
        if (voiceChecked) {
            return;
        }
        voiceChecked = true;
        voiceTimeout.stop();
        if (udp) {
            out << "Voice: UDP works\n";
        } else {
            out << "Voice: UDP does not get through, so voice uses the main connection (it works, with more "
                   "delay). Check that the server's UDP port is open in its firewall and forwarded by its router.\n";
            exitCode = 3;
        }
        out.flush();
        finishIfDone();
    };

    QObject::connect(&client, &opentalk::sdk::Client::connected, [&](const opentalk::sdk::ServerInfo &server) {
        out << "Connected to \"" << server.name << "\" (OpenTalk " << server.softwareVersion << ", protocol "
            << server.protocolVersion << ")\n"
            << "Certificate: " << server.certificateFingerprint << '\n'
            << "Logged in as " << client.self().name << "; " << client.users().size() << " user(s) and "
            << client.channels().size() << " channel(s) on the server\n";
        out.flush();
        client.ping();
        if (checkVoice) {
            voiceTimeout.start();
            if (client.isAudioUsingUdp()) {
                reportVoice(true);
            }
        }
    });
    QObject::connect(&client, &opentalk::sdk::Client::audioPathChanged, [&](bool udp) {
        if (udp) {
            reportVoice(true);
        }
    });
    QObject::connect(&voiceTimeout, &QTimer::timeout, [&] { reportVoice(client.isAudioUsingUdp()); });
    QObject::connect(&client, &opentalk::sdk::Client::pingReplied, [&](qint64 ms) {
        out << "Round trip: " << ms << " ms\n";
        out.flush();
        pinged = true;
        if (exitCode == 1) {
            exitCode = 0;
        }
        finishIfDone();
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

    opentalk::sdk::Login login;
    if (parser.isSet(userOption)) {
        login.mode = opentalk::sdk::Login::Mode::Account;
        login.name = parser.value(userOption);
        std::string password;
        std::getline(std::cin, password);
        if (!password.empty() && password.back() == '\r') {
            password.pop_back();
        }
        login.password = QString::fromStdString(password);
    } else {
        login.name = parser.value(guestOption);
    }
    client.connectToServer({parser.positionalArguments().first(), static_cast<quint16>(port)},
                           {parser.value(fingerprintOption)}, login);
    return QCoreApplication::exec();
}
