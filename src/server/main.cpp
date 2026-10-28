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
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QStandardPaths>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

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
            return fail(QStringLiteral("--role must be user, moderator, restricted, admin or owner"));
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
        moderation::AuditEntry entry;
        entry.timeMs = QDateTime::currentMSecsSinceEpoch();
        entry.actorName = QStringLiteral("(command line)");
        entry.action = QStringLiteral("account.created");
        entry.target = name;
        entry.details = QStringLiteral("role=%1").arg(server::toString(*role));
        db.appendAudit(entry);
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
    const QCommandLineOption noUdpOption(QStringLiteral("no-udp"),
                                         QStringLiteral("Carry voice only over the encrypted TCP connections (for "
                                                        "networks where the UDP port cannot be opened)."));
    const QCommandLineOption logOption(QStringLiteral("log-level"),
                                       QStringLiteral("warning, info or debug (default: info)."),
                                       QStringLiteral("level"), QStringLiteral("info"));
    const QCommandLineOption createOption(QStringLiteral("create-account"),
                                          QStringLiteral("Create an account, read its password from the "
                                                         "terminal or standard input, and exit."),
                                          QStringLiteral("username"));
    const QCommandLineOption roleOption(QStringLiteral("role"),
                                        QStringLiteral("Role for --create-account: user, moderator, admin or owner "
                                                       "(default: user). A server has one owner."),
                                        QStringLiteral("role"), QStringLiteral("user"));
    const QCommandLineOption setPasswordOption(QStringLiteral("set-password"),
                                               QStringLiteral("Change an account's password and exit."),
                                               QStringLiteral("username"));
    const QCommandLineOption deleteOption(QStringLiteral("delete-account"),
                                          QStringLiteral("Delete an account and exit."), QStringLiteral("username"));
    const QCommandLineOption listOption(QStringLiteral("list-accounts"), QStringLiteral("List accounts and exit."));
    const QCommandLineOption recoverOption(
        QStringLiteral("recover-owner"),
        QStringLiteral("Make an existing account the owner (for when the owner is locked out), and exit. Stop the "
                       "server first. The change is recorded in the audit log."),
        QStringLiteral("username"));
    const QCommandLineOption verifyAuditOption(QStringLiteral("verify-audit"),
                                               QStringLiteral("Check the audit log's hash chain and exit (exit code 2 "
                                                              "if it was changed)."));
    const QCommandLineOption exportAuditOption(QStringLiteral("export-audit"),
                                               QStringLiteral("Write the audit log as text to a file and exit. Keep "
                                                              "copies elsewhere: they show later tampering."),
                                               QStringLiteral("file"));
    const QCommandLineOption retentionOption(QStringLiteral("audit-retention-days"),
                                             QStringLiteral("Remove audit log entries older than this many days "
                                                            "(default: 365; 0 keeps them forever)."),
                                             QStringLiteral("days"), QStringLiteral("365"));
    const QCommandLineOption stdinOption(QStringLiteral("stop-when-stdin-closes"),
                                         QStringLiteral("Shut down gracefully when standard input closes. Used when "
                                                        "the OpenTalk app runs the server for you."));
    parser.addOptions({dataOption,    listenOption,      portOption,        certOption,      keyOption,
                       nameOption,    maxOption,         guestsOption,      noUdpOption,     logOption,
                       createOption,  roleOption,        setPasswordOption, deleteOption,    listOption,
                       recoverOption, verifyAuditOption, exportAuditOption, retentionOption, stdinOption});
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
    if (parser.isSet(recoverOption)) {
        const QString name = parser.value(recoverOption);
        if (!database.setOwner(name, &error)) {
            return fail(error);
        }
        moderation::AuditEntry entry;
        entry.timeMs = QDateTime::currentMSecsSinceEpoch();
        entry.actorName = QStringLiteral("(owner recovery)");
        entry.action = QStringLiteral("owner.recovered");
        entry.target = name;
        entry.details = QStringLiteral("from the command line with access to the data folder");
        database.appendAudit(entry);
        printLine(
            QStringLiteral("\"%1\" is now the owner. The previous owner, if any, is an administrator.").arg(name));
        return 0;
    }
    if (parser.isSet(verifyAuditOption)) {
        const moderation::AuditVerification result = database.verifyAudit();
        if (!result.intact) {
            printLine(QStringLiteral("The audit log was changed: %1.").arg(result.problem));
            return 2;
        }
        printLine(QStringLiteral("The audit log is intact (%1 entries checked).").arg(result.checked));
        return 0;
    }
    if (parser.isSet(exportAuditOption)) {
        QFile file(parser.value(exportAuditOption));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            return fail(QStringLiteral("cannot write %1: %2").arg(file.fileName(), file.errorString()));
        }
        // Read newest first in pages, then write oldest first.
        QList<moderation::AuditEntry> entries;
        quint64 before = 0;
        while (true) {
            const QList<moderation::AuditEntry> page = database.auditEntries(before, 1000);
            if (page.isEmpty()) {
                break;
            }
            entries.append(page);
            before = page.last().sequence;
        }
        std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.sequence < b.sequence; });
        for (const moderation::AuditEntry &e : std::as_const(entries)) {
            file.write(moderation::formatAuditEntry(e).toUtf8() + '\n');
        }
        const moderation::AuditVerification result = database.verifyAudit();
        file.write(QStringLiteral("# %1 entries; chain %2\n")
                       .arg(entries.size())
                       .arg(result.intact ? QStringLiteral("intact") : QStringLiteral("BROKEN: ") + result.problem)
                       .toUtf8());
        printLine(QStringLiteral("Wrote %1 audit entries to %2.").arg(entries.size()).arg(file.fileName()));
        return 0;
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
    const int retention = parser.value(retentionOption).toInt(&ok);
    if (!ok || retention < 0 || retention > 36500) {
        return fail(QStringLiteral("--audit-retention-days must be between 0 and 36500"));
    }
    config.auditRetentionDays = retention;
    const QString name = parser.value(nameOption).trimmed();
    if (name.isEmpty() || name.size() > protocol::kMaxNameLength) {
        return fail(QStringLiteral("--name must be 1 to %1 characters").arg(protocol::kMaxNameLength));
    }
    config.session.serverName = name;
    config.session.serverVersion = QCoreApplication::applicationVersion();
    config.allowGuests = parser.isSet(guestsOption);
    config.enableUdp = !parser.isSet(noUdpOption);
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
    if (server.udpPort() != 0) {
        printLine(QStringLiteral("Voice uses UDP port %1 (allow it through firewalls too; without it voice still "
                                 "works, with more delay).")
                      .arg(server.udpPort()));
    } else {
        printLine(QStringLiteral("Voice uses the TCP connections only (no UDP port)."));
    }
    printLine(QStringLiteral("Data folder: %1").arg(QDir::toNativeSeparators(dataDir)));
    if (database.accounts().isEmpty()) {
        printLine(QStringLiteral("There are no accounts yet. Create the owner (yourself) with:"));
        printLine(QStringLiteral("  opentalk-server --data-dir \"%1\" --create-account <name> --role owner")
                      .arg(QDir::toNativeSeparators(dataDir)));
    }
    printLine(QStringLiteral("Certificate fingerprint (share it so users can verify this server):"));
    printLine(QStringLiteral("  ") + server.certificateFingerprint());
    printLine(QStringLiteral("Press Ctrl+C to stop."));

    ShutdownSignal shutdown;
    QObject::connect(&shutdown, &ShutdownSignal::requested, &server, &server::Server::stop);
    if (parser.isSet(stdinOption)) {
        // A blocking read on a helper thread works the same on every platform (Windows
        // cannot watch console or pipe handles with QSocketNotifier).
        QPointer<server::Server> guard(&server);
        std::thread([guard] {
            std::string ignored;
            while (std::getline(std::cin, ignored)) {
            }
            // The guard is only checked on the main thread, where that is safe.
            if (QCoreApplication *instance = QCoreApplication::instance()) {
                QMetaObject::invokeMethod(
                    instance,
                    [guard] {
                        if (guard) {
                            guard->stop();
                        }
                    },
                    Qt::QueuedConnection);
            }
        }).detach();
    }
    QObject::connect(&server, &server::Server::stopped, &app, &QCoreApplication::quit);

    const int code = QCoreApplication::exec();
    logging::shutdown();
    return code;
}
