// SPDX-License-Identifier: GPL-3.0-only
// LocalServer driving the real opentalk-server program: account creation, start-up,
// logging in as the new administrator, and graceful shutdown.

#include "opentalk/client/Connection.h"
#include "opentalk/client/LocalServer.h"
#include "opentalk/security/Tls.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>

using namespace opentalk;

namespace {

const QString kServer = QStringLiteral(OPENTALK_SERVER_EXECUTABLE);
constexpr int kWaitMs = 30'000;

quint16 freePort()
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    return probe.serverPort();
}

} // namespace

class TestLocalServer : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<client::DisconnectInfo>();
        QString error;
        QVERIFY2(security::initializeTls(&error), qPrintable(error));
    }

    void hostAndJoinAsAdministrator()
    {
        QTemporaryDir dir;
        client::LocalServer local;

        QString accountError = QStringLiteral("pending");
        local.createAccount(kServer, dir.path(), QStringLiteral("host"), QStringLiteral("a good password"), true,
                            [&](const QString &e) { accountError = e; });
        QTRY_VERIFY_WITH_TIMEOUT(accountError != QLatin1String("pending"), kWaitMs);
        QVERIFY2(accountError.isEmpty(), qPrintable(accountError));

        QString duplicateError = QStringLiteral("pending");
        local.createAccount(kServer, dir.path(), QStringLiteral("HOST"), QStringLiteral("another password"), true,
                            [&](const QString &e) { duplicateError = e; });
        QTRY_VERIFY_WITH_TIMEOUT(duplicateError != QLatin1String("pending"), kWaitMs);
        QVERIFY(duplicateError.contains(QStringLiteral("already exists")));

        QSignalSpy started(&local, &client::LocalServer::started);
        QSignalSpy failed(&local, &client::LocalServer::failed);
        client::LocalServer::Options options;
        options.executable = kServer;
        options.dataDir = dir.path();
        options.name = QStringLiteral("Living room");
        options.port = freePort();
        local.start(options);
        QTRY_VERIFY_WITH_TIMEOUT(!started.isEmpty() || !failed.isEmpty(), kWaitMs);
        QVERIFY2(failed.isEmpty(), qPrintable(local.output()));
        QVERIFY(local.state() == client::LocalServer::State::Running);
        QCOMPARE(local.port(), options.port);
        QVERIFY(local.fingerprint().startsWith(QStringLiteral("SHA256:")));

        client::Connection connection;
        client::ConnectOptions o;
        o.host = QStringLiteral("127.0.0.1");
        o.port = local.port();
        o.trust = security::ServerTrust::pinned(local.fingerprint()); // we started it, so we know it
        o.credentials.mode = client::Credentials::Mode::Account;
        o.credentials.name = QStringLiteral("host");
        o.credentials.password = QStringLiteral("a good password");
        o.autoReconnect = false;
        QSignalSpy connected(&connection, &client::Connection::connected);
        QSignalSpy disconnected(&connection, &client::Connection::disconnected);
        connection.connectToServer(o);
        QTRY_VERIFY_WITH_TIMEOUT(!connected.isEmpty() || !disconnected.isEmpty(), kWaitMs);
        QCOMPARE(connected.count(), 1);
        QVERIFY(connection.serverState().selfRole() == protocol::Role::Admin);
        QCOMPARE(connection.serverInfo().serverName, QStringLiteral("Living room"));

        // Stopping tells connected users the server is shutting down.
        QSignalSpy stopped(&local, &client::LocalServer::stopped);
        local.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, kWaitMs);
        QVERIFY(failed.isEmpty());
        QVERIFY(local.output().contains(QStringLiteral("Stopped")));
        QTRY_COMPARE_WITH_TIMEOUT(disconnected.count(), 1, kWaitMs);
        QVERIFY(disconnected.first().first().value<client::DisconnectInfo>().protocolCode ==
                protocol::ErrorCode::ServerShuttingDown);
    }

    void portInUseIsReported()
    {
        QTcpServer blocker;
        QVERIFY(blocker.listen(QHostAddress::LocalHost, 0));
        QTemporaryDir dir;
        client::LocalServer local;
        QSignalSpy failed(&local, &client::LocalServer::failed);
        client::LocalServer::Options options;
        options.executable = kServer;
        options.dataDir = dir.path();
        options.port = blocker.serverPort();
        local.start(options);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, kWaitMs);
        QVERIFY(local.state() == client::LocalServer::State::Stopped);
        QVERIFY(failed.first().first().toString().contains(QStringLiteral("cannot listen")));
    }

    void missingProgramIsReported()
    {
        client::LocalServer local;
        QSignalSpy failed(&local, &client::LocalServer::failed);
        client::LocalServer::Options options;
        options.executable = QStringLiteral("/nonexistent/opentalk-server");
        local.start(options);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, kWaitMs);
        QVERIFY(local.state() == client::LocalServer::State::Stopped);
    }
};

QTEST_GUILESS_MAIN(TestLocalServer)
#include "tst_local_server.moc"
