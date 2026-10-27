// SPDX-License-Identifier: GPL-3.0-only
// opentalk-server: the headless OpenTalk server. It needs no window and runs the same
// way on a desktop PC, a dedicated machine or a VPS.
//
// All server data lives in one data folder:
//   <data>/opentalk.db       accounts and persistent channels (SQLite)
//   <data>/tls/server.crt    certificate (created on first start if missing)
//   <data>/tls/server.key    private key (keep secret)

#include "Console.h"
#include "ShutdownSignal.h"

#include "opentalk/logging/Logging.h"
#include "opentalk/security/CertificateGenerator.h"
#include "opentalk/security/PasswordHash.h"
#include "opentalk/security/Tls.h"
#include "opentalk/server/Server.h"
#include "opentalk/server/ServerDatabase.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <cstdio>

#ifndef Q_OS_WIN
#include <unistd.h>
#endif

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

/// Asks for a new password twice (or reads it once from piped input).
std::optional<QString> readNewPassword(QString *error)
{
    const auto first = readSecretLine(QStringLiteral("New password: "));
    if (!first || first->isEmpty()) {
        *error = QStringLiteral("no password given");
        return std::nullopt;
    }
    if (first->size() < 8) {
        *error = QStringLiteral("passwords must be at least 8 characters");
        return std::nullopt;
    }
    if (first->size() > security::kMaxPasswordLength) {
        *error = QStringLiteral("password is too long");
        return std::nullopt;
    }
#ifdef Q_OS_WIN
    const bool interactive = false;
#else
    const bool interactive = isatty(0) != 0;
#endif
    if (interactive) {
        const auto second = readSecretLine(QStringLiteral("Repeat password: "));
        if (!second || *second != *first) {
            *error = QStringLiteral("the passwords did not match");
            return std::nullopt;
        }
    }
    return first;
}

int runAccountCommand(const QCommandLineParser &parser, const QCommandLineOption &create,
                      const QCommandLineOption &setPassword, const QCommandLineOption &remove,
                      const QCommandLineOption &list, const QCommandLineOption &roleOption, server::ServerDatabase &db)
{
    if (parser.isSet(list)) {
        const QList<server::Account> accounts = db.accounts();
        if (accounts.isEmpty()) {
            printLine(QStringLiteral("No accounts."));
        }
        for (const server::Account &a : accounts) {
            printLine(QStringLiteral("%1 (%2)").arg(a.username, server::toString(a.role)));
        }
        return 0;
    }
    QString error;
    if (parser.isSet(create)) {
        const auto role = server::roleFromString(parser.value(roleOption));
        if (!role) {
            return fail(QStringLiteral("--role must be user or admin"));
        }
        const QString name = parser.value(create);
        if (!server::ServerDatabase::isValidUsername(name)) {
            return fail(QStringLiteral("usernames are 1 to 32 characters: letters A-Z, digits, '.', '_' or '-'"));
        }
        const auto password = readNewPassword(&error);
        if (!password) {
            return fail(error);
        }
        const auto hash = security::hashPassword(*password, security::PasswordHashCost::interactive());
        if (!hash || !db.createAccount(name, *hash, *role, &error)) {
            return fail(hash ? error : QStringLiteral("could not hash the password"));
        }
        printLine(QStringLiteral("Created %1 account \"%2\".").arg(server::toString(*role), name));
        return 0;
    }
    if (parser.isSet(setPassword)) {
        const QString name = parser.value(setPassword);
        if (!db.findAccount(name)) {
            return fail(QStringLiteral("no account named %1").arg(name));
        }
        const auto password = readNewPassword(&error);
        if (!password) {
            return fail(error);
        }
        const auto hash = security::hashPassword(*password, security::PasswordHashCost::interactive());
        if (!hash || !db.setPasswordHash(name, *hash)) {
            return fail(QStringLiteral("could not change the password"));
        }
        printLine(QStringLiteral("Password changed for \"%1\". Existing sessions are not affected.").arg(name));
        return 0;
    }
    if (parser.isSet(remove)) {
        const QString name = parser.value(remove);
        if (!db.deleteAccount(name)) {
            return fail(QStringLiteral("no account named %1").arg(name));
        }
        printLine(QStringLiteral("Deleted account \"%1\".").arg(name));
        return 0;
    }
    return -1; // not an account command
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("opentalk-server"));
    QCoreApplication::setOrganizationName(QStringLiteral("OpenTalk"));
    QCoreApplication::setApplicationVersion(QStringLiteral(OPENTALK_VERSION_STRING));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("OpenTalk server (headless)"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption dataOption(QStringLiteral("data-dir"),
                                        QStringLiteral("Folder for the database and certificate (default: the "
                                                       "per-user application data folder)."),
                                        QStringLiteral("folder"));
    const QCommandLineOption listenOption(QStringLiteral("listen"),
                                          QStringLiteral("Address to listen on (default: all interfaces)."),
                                          QStringLiteral("address"), QStringLiteral("any"));
    const QCommandLineOption portOption(QStringLiteral("port"),
                                        QStringLiteral("TCP port (default: 10350; 0 picks a free port)."),
                                        QStringLiteral("port"), QStringLiteral("10350"));
    const QCommandLineOption certOption(QStringLiteral("tls-cert"),
                                        QStringLiteral("PEM certificate file (default: <data>/tls/server.crt, "
                                                       "created if missing)."),
                                        QStringLiteral("file"));
    const QCommandLineOption keyOption(QStringLiteral("tls-key"),
                                       QStringLiteral("PEM private key file (default: <data>/tls/server.key)."),
                                       QStringLiteral("file"));
    const QCommandLineOption nameOption(QStringLiteral("name"), QStringLiteral("Server name shown to clients."),
                                        QStringLiteral("name"), QStringLiteral("OpenTalk server"));
    const QCommandLineOption maxOption(QStringLiteral("max-users"),
                                       QStringLiteral("Maximum simultaneous connections (default: 200)."),
                                       QStringLiteral("count"), QStringLiteral("200"));
    const QCommandLineOption guestsOption(QStringLiteral("allow-guests"),
                                          QStringLiteral("Let people join without an account, using a nickname."));
    const QCommandLineOption logOption(QStringLiteral("log-level"),
                                       QStringLiteral("warning, info or debug (default: info)."),
                                       QStringLiteral("level"), QStringLiteral("info"));
    const QCommandLineOption createOption(QStringLiteral("create-account"),
                                          QStringLiteral("Create an account, read its password from the "
                                                         "terminal or standard input, and exit."),
                                          QStringLiteral("username"));
    const QCommandLineOption roleOption(QStringLiteral("role"),
                                        QStringLiteral("Role for --create-account: user or admin (default: user)."),
                                        QStringLiteral("role"), QStringLiteral("user"));
    const QCommandLineOption setPasswordOption(QStringLiteral("set-password"),
                                               QStringLiteral("Change an account's password and exit."),
                                               QStringLiteral("username"));
    const QCommandLineOption deleteOption(QStringLiteral("delete-account"),
                                          QStringLiteral("Delete an account and exit."), QStringLiteral("username"));
    const QCommandLineOption listOption(QStringLiteral("list-accounts"), QStringLiteral("List accounts and exit."));
    parser.addOptions({dataOption, listenOption, portOption, certOption, keyOption, nameOption, maxOption, guestsOption,
                       logOption, createOption, roleOption, setPasswordOption, deleteOption, listOption});
    parser.process(app);

    const std::optional<logging::Level> level = logging::levelFromString(parser.value(logOption));
    if (!level) {
        return fail(QStringLiteral("--log-level must be warning, info or debug"));
    }
    logging::install({QString(), *level, true});
    if (!security::initializePasswordHashing()) {
        return fail(QStringLiteral("could not initialise the password hashing library"));
    }

    const QString dataDir = QDir::cleanPath(
        parser.isSet(dataOption) ? QDir(parser.value(dataOption)).absolutePath()
                                 : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    if (!QDir().mkpath(dataDir)) {
        return fail(QStringLiteral("cannot create the data folder %1").arg(QDir::toNativeSeparators(dataDir)));
    }
    server::ServerDatabase database;
    QString error;
    if (!database.open(dataDir + QStringLiteral("/opentalk.db"), &error)) {
        return fail(error);
    }

    const int accountResult =
        runAccountCommand(parser, createOption, setPasswordOption, deleteOption, listOption, roleOption, database);
    if (accountResult >= 0) {
        return accountResult;
    }

    if (!security::initializeTls(&error)) {
        return fail(error);
    }
    server::ServerConfig config;
    const QString listen = parser.value(listenOption);
    if (listen != QLatin1String("any") && !config.address.setAddress(listen)) {
        return fail(QStringLiteral("--listen is not a valid IP address: %1").arg(listen));
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
    config.allowGuests = parser.isSet(guestsOption);
    config.database = &database;

    QString certPath = parser.value(certOption);
    QString keyPath = parser.value(keyOption);
    if (certPath.isEmpty() != keyPath.isEmpty()) {
        return fail(QStringLiteral("give both --tls-cert and --tls-key, or neither"));
    }
    if (certPath.isEmpty()) {
        certPath = dataDir + QStringLiteral("/tls/server.crt");
        keyPath = dataDir + QStringLiteral("/tls/server.key");
        const bool certExists = QFileInfo::exists(certPath);
        const bool keyExists = QFileInfo::exists(keyPath);
        if (certExists != keyExists) {
            return fail(QStringLiteral("found only one of %1 and %2; restore the missing file or delete both "
                                       "to create a new certificate")
                            .arg(QDir::toNativeSeparators(certPath), QDir::toNativeSeparators(keyPath)));
        }
        if (!certExists) {
            security::SelfSignedCertificateRequest request;
            request.commonName = name;
            if (!security::generateSelfSignedCertificate(request, certPath, keyPath, &error)) {
                return fail(error);
            }
            printLine(QStringLiteral("Created a new self-signed certificate in %1")
                          .arg(QDir::toNativeSeparators(dataDir + QStringLiteral("/tls"))));
        }
    }
    const std::optional<QSslConfiguration> tls = security::serverTlsConfiguration(certPath, keyPath, &error);
    if (!tls) {
        return fail(error);
    }
    config.tls = *tls;

    server::Server server(config);
    if (!server.start(&error)) {
        return fail(error);
    }
    printLine(QStringLiteral("OpenTalk server \"%1\" is running on port %2.").arg(name).arg(server.serverPort()));
    printLine(QStringLiteral("Data folder: %1").arg(QDir::toNativeSeparators(dataDir)));
    if (database.accounts().isEmpty()) {
        printLine(QStringLiteral("There are no accounts yet. Create an administrator with:"));
        printLine(QStringLiteral("  opentalk-server --data-dir \"%1\" --create-account <name> --role admin")
                      .arg(QDir::toNativeSeparators(dataDir)));
    }
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
