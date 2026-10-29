// SPDX-License-Identifier: GPL-3.0-only
// End-to-end tests: a real Server and a real client Connection over TLS 1.3 on
// localhost, plus hostile clients that misbehave at the TCP, TLS and protocol layers.

#include "opentalk/client/Connection.h"
#include "opentalk/protocol/Frame.h"
#include "opentalk/protocol/Messages.h"
#include "opentalk/security/Tls.h"
#include "opentalk/server/Server.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTest>

#include <memory>
#include <vector>

using namespace opentalk;

namespace {

const QString kDataDir = QStringLiteral(OPENTALK_TEST_DATA_DIR);
constexpr int kWaitMs = 10'000;

client::DisconnectInfo disconnectInfo(const QSignalSpy &spy)
{
    return spy.first().first().value<client::DisconnectInfo>();
}

} // namespace

class TestTlsSession : public QObject {
    Q_OBJECT

private:
    QSslConfiguration m_serverTls;
    std::unique_ptr<server::Server> m_server;
    mutable int m_nextGuest = 1;

    void startServer(const std::function<void(server::ServerConfig &)> &adjust = {})
    {
        server::ServerConfig config;
        config.address = QHostAddress::LocalHost;
        config.port = 0;
        config.tls = m_serverTls;
        config.session.serverName = QStringLiteral("Integration server");
        config.shutdownGraceMs = 2'000;
        config.allowGuests = true;
        config.passwordCost = security::PasswordHashCost::minimumForTests();
        if (adjust) {
            adjust(config);
        }
        m_server = std::make_unique<server::Server>(config);
        QString error;
        QVERIFY2(m_server->start(&error), qPrintable(error));
    }

    client::ConnectOptions options(const security::ServerTrust &trust) const
    {
        client::ConnectOptions o;
        o.host = QStringLiteral("127.0.0.1");
        o.port = m_server->serverPort();
        o.trust = trust;
        o.session.clientVersion = QStringLiteral("test");
        o.credentials.name = QStringLiteral("Tester%1").arg(m_nextGuest++);
        o.autoReconnect = false;
        return o;
    }

    security::ServerTrust pinnedTrust() const
    {
        return security::ServerTrust::pinned(m_server->certificateFingerprint());
    }

    /// Opens a raw TLS socket that ignores certificate problems, for hostile-client tests.
    std::unique_ptr<QSslSocket> rawTlsSocket()
    {
        auto socket = std::make_unique<QSslSocket>();
        QSslConfiguration config = security::clientTlsConfiguration();
        config.setPeerVerifyMode(QSslSocket::VerifyNone);
        socket->setSslConfiguration(config);
        // Never use the blocking waitFor*() calls here: the server runs on this same thread
        // and needs the event loop to answer.
        QSignalSpy encrypted(socket.get(), &QSslSocket::encrypted);
        socket->connectToHostEncrypted(QStringLiteral("127.0.0.1"), m_server->serverPort());
        if (!encrypted.wait(kWaitMs)) {
            qWarning() << "raw TLS socket failed:" << socket->errorString();
            return nullptr;
        }
        return socket;
    }

private slots:
    void initTestCase()
    {
        qRegisterMetaType<client::DisconnectInfo>();
        qRegisterMetaType<protocol::CloseInfo>();
        qRegisterMetaType<protocol::Welcome>();
        qRegisterMetaType<protocol::Hello>();
        QString error;
        QVERIFY2(security::initializeTls(&error), qPrintable(error));
        const auto tls = security::serverTlsConfiguration(kDataDir + QStringLiteral("/test-server.crt"),
                                                          kDataDir + QStringLiteral("/test-server.key"), &error);
        QVERIFY2(tls.has_value(), qPrintable(error));
        m_serverTls = *tls;
    }

    void cleanup()
    {
        if (m_server && m_server->isListening()) {
            QSignalSpy stopped(m_server.get(), &server::Server::stopped);
            m_server->stop();
            QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, kWaitMs);
        }
        m_server.reset();
    }

    void fingerprintFormat()
    {
        startServer();
        const QString fp = m_server->certificateFingerprint();
        QVERIFY(fp.startsWith(QStringLiteral("SHA256:")));
        // Users may paste fingerprints without the prefix, in lower case, with spaces or colons.
        const QString hexOnly = fp.mid(7).remove(QLatin1Char(':')).toLower();
        QCOMPARE(security::normalizeFingerprint(hexOnly), std::optional<QString>(fp));
        QCOMPARE(security::normalizeFingerprint(QStringLiteral(" sha256:") +
                                                fp.mid(7).replace(QLatin1Char(':'), QLatin1Char(' '))),
                 std::optional<QString>(fp));
        QCOMPARE(security::normalizeFingerprint(QStringLiteral("not a fingerprint")), std::nullopt);
    }

    void connectPingAndDisconnect()
    {
        startServer();
        QSignalSpy established(m_server.get(), &server::Server::sessionEstablished);
        QSignalSpy closedOnServer(m_server.get(), &server::Server::sessionClosed);

        client::Connection connection;
        QSignalSpy connected(&connection, &client::Connection::connected);
        QSignalSpy disconnected(&connection, &client::Connection::disconnected);
        QSignalSpy rtt(&connection, &client::Connection::roundTripMeasured);
        connection.connectToServer(options(pinnedTrust()));

        QVERIFY(connected.wait(kWaitMs));
        QVERIFY(connection.state() == client::ConnectionState::Connected);
        QCOMPARE(connection.serverInfo().serverName, QStringLiteral("Integration server"));
        QCOMPARE(connection.serverInfo().version, quint16(1));
        QCOMPARE(connection.serverFingerprint(), m_server->certificateFingerprint());
        QTRY_COMPARE_WITH_TIMEOUT(established.count(), 1, kWaitMs);
        QCOMPARE(established.first().at(1).value<protocol::Hello>().clientVersion, QStringLiteral("test"));
        QCOMPARE(m_server->sessionCount(), 1);

        QVERIFY(connection.measureRoundTrip());
        QVERIFY(rtt.wait(kWaitMs));
        QVERIFY(rtt.first().first().toLongLong() >= 0);

        connection.disconnectFromServer();
        QTRY_COMPARE_WITH_TIMEOUT(disconnected.count(), 1, kWaitMs);
        QVERIFY(disconnectInfo(disconnected).reason == client::DisconnectInfo::Reason::UserRequested);
        QTRY_COMPARE_WITH_TIMEOUT(closedOnServer.count(), 1, kWaitMs);
        const auto info = closedOnServer.first().at(1).value<protocol::CloseInfo>();
        QVERIFY(info.initiator == protocol::CloseInfo::Initiator::Peer);
        QTRY_COMPARE_WITH_TIMEOUT(m_server->sessionCount(), 0, kWaitMs);
    }

    void selfSignedCertificateIsNotTrustedByDefault()
    {
        startServer();
        client::Connection connection;
        QSignalSpy disconnected(&connection, &client::Connection::disconnected);
        connection.connectToServer(options(security::ServerTrust::systemAuthorities()));
        QVERIFY(disconnected.wait(kWaitMs));
        const client::DisconnectInfo info = disconnectInfo(disconnected);
        QVERIFY(info.reason == client::DisconnectInfo::Reason::UntrustedCertificate);
        // The user is shown this fingerprint and can choose to trust it.
        QCOMPARE(info.presentedFingerprint, m_server->certificateFingerprint());
        QCOMPARE(m_server->sessionCount(), 0);
    }

    void wrongPinIsRejected()
    {
        startServer();
        client::Connection connection;
        QSignalSpy disconnected(&connection, &client::Connection::disconnected);
        connection.connectToServer(options(security::ServerTrust::pinned(QString(64, QLatin1Char('A')))));
        QVERIFY(disconnected.wait(kWaitMs));
        QVERIFY(disconnectInfo(disconnected).reason == client::DisconnectInfo::Reason::UntrustedCertificate);
    }

    void serverShutdownReachesClient()
    {
        startServer();
        client::Connection connection;
        QSignalSpy connected(&connection, &client::Connection::connected);
        QSignalSpy disconnected(&connection, &client::Connection::disconnected);
        connection.connectToServer(options(pinnedTrust()));
        QVERIFY(connected.wait(kWaitMs));

        QSignalSpy stopped(m_server.get(), &server::Server::stopped);
        m_server->stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, kWaitMs);
        QTRY_COMPARE_WITH_TIMEOUT(disconnected.count(), 1, kWaitMs);
        const client::DisconnectInfo info = disconnectInfo(disconnected);
        QVERIFY(info.reason == client::DisconnectInfo::Reason::ServerClosed);
        QVERIFY(info.protocolCode == protocol::ErrorCode::ServerShuttingDown);
    }

    void fullServerTurnsClientsAway()
    {
        startServer([](server::ServerConfig &c) { c.maxSessions = 1; });
        client::Connection first;
        QSignalSpy firstConnected(&first, &client::Connection::connected);
        first.connectToServer(options(pinnedTrust()));
        QVERIFY(firstConnected.wait(kWaitMs));

        client::Connection second;
        QSignalSpy secondDisconnected(&second, &client::Connection::disconnected);
        second.connectToServer(options(pinnedTrust()));
        QVERIFY(secondDisconnected.wait(kWaitMs));
        QVERIFY(disconnectInfo(secondDisconnected).protocolCode == protocol::ErrorCode::ServerFull);
        QVERIFY(first.state() == client::ConnectionState::Connected);
    }

    void connectionsPerAddressAreCapped()
    {
        startServer([](server::ServerConfig &c) { c.maxConnectionsPerAddress = 3; });
        std::vector<std::unique_ptr<QSslSocket>> open;
        for (int i = 0; i < 3; ++i) {
            auto socket = rawTlsSocket();
            QVERIFY(socket);
            open.push_back(std::move(socket));
        }
        // A fourth from the same address is dropped before it gets anywhere.
        QSslSocket fourth;
        QSslConfiguration config = security::clientTlsConfiguration();
        config.setPeerVerifyMode(QSslSocket::VerifyNone);
        fourth.setSslConfiguration(config);
        QSignalSpy dropped(&fourth, &QSslSocket::disconnected);
        QSignalSpy encrypted(&fourth, &QSslSocket::encrypted);
        fourth.connectToHostEncrypted(QStringLiteral("127.0.0.1"), m_server->serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(!dropped.isEmpty() || fourth.state() == QAbstractSocket::UnconnectedState, kWaitMs);
        QVERIFY(encrypted.isEmpty());
        for (const auto &socket : open) {
            QCOMPARE(socket->state(), QAbstractSocket::ConnectedState); // the first three are unaffected
        }
        // Once one closes, there is room again.
        QSignalSpy closed(open.front().get(), &QSslSocket::disconnected);
        open.front()->disconnectFromHost();
        QTRY_VERIFY_WITH_TIMEOUT(!closed.isEmpty(), kWaitMs);
        QTRY_COMPARE_WITH_TIMEOUT(m_server->sessionCount(), 2, kWaitMs);
        QVERIFY(rawTlsSocket() != nullptr);
    }

    void manyClientsAtOnce()
    {
        // A busy evening: 150 people connect at the same time, chat and leave.
        constexpr int kClients = 150;
        startServer([](server::ServerConfig &c) {
            c.maxSessions = 300;
            c.maxConnectionsPerAddress = 300; // all of them come from 127.0.0.1 here
        });
        QElapsedTimer timer;
        timer.start();
        std::vector<std::unique_ptr<client::Connection>> clients;
        int connected = 0;
        int received = 0;
        for (int i = 0; i < kClients; ++i) {
            auto c = std::make_unique<client::Connection>();
            QObject::connect(c.get(), &client::Connection::connected, c.get(), [&connected] { ++connected; });
            QObject::connect(c.get(), &client::Connection::textReceived, c.get(),
                             [&received](const protocol::TextMessage &) { ++received; });
            c->connectToServer(options(pinnedTrust()));
            clients.push_back(std::move(c));
        }
        QTRY_COMPARE_WITH_TIMEOUT(connected, kClients, 60'000);
        QCOMPARE(m_server->onlineUserCount(), kClients);
        const qint64 connectMs = timer.restart();

        clients.front()->sendText(protocol::TextTarget::Channel, protocol::kRootChannelId, QStringLiteral("hello all"));
        QTRY_COMPARE_WITH_TIMEOUT(received, kClients, 30'000); // everyone, the sender included
        const qint64 broadcastMs = timer.restart();

        for (const auto &c : clients) {
            c->disconnectFromServer();
        }
        QTRY_COMPARE_WITH_TIMEOUT(m_server->sessionCount(), 0, 30'000);
        QCOMPARE(m_server->onlineUserCount(), 0);
        qInfo("%d clients: all connected in %lld ms, a message reached everyone in %lld ms, all gone in %lld ms",
              kClients, connectMs, broadcastMs, timer.elapsed());
    }

    void incompatibleVersionIsReported()
    {
        startServer();
        client::ConnectOptions o = options(pinnedTrust());
        o.session.versions = {5, 6};
        client::Connection connection;
        QSignalSpy disconnected(&connection, &client::Connection::disconnected);
        connection.connectToServer(o);
        QVERIFY(disconnected.wait(kWaitMs));
        QVERIFY(disconnectInfo(disconnected).reason == client::DisconnectInfo::Reason::VersionMismatch);
    }

    void plaintextClientIsDropped()
    {
        startServer();
        QTcpSocket socket;
        QSignalSpy connected(&socket, &QTcpSocket::connected);
        QSignalSpy disconnected(&socket, &QTcpSocket::disconnected);
        socket.connectToHost(QHostAddress::LocalHost, m_server->serverPort());
        QVERIFY(connected.wait(kWaitMs));
        socket.write(protocol::encodeMessage(protocol::Hello{{1, 1}, QStringLiteral("x"), QStringLiteral("y")}));
        QTRY_COMPARE_WITH_TIMEOUT(disconnected.count(), 1, kWaitMs);
        QCOMPARE(m_server->sessionCount(), 0);
    }

    void garbageAfterTlsClosesSession()
    {
        startServer();
        QSignalSpy closedOnServer(m_server.get(), &server::Server::sessionClosed);
        auto socket = rawTlsSocket();
        QVERIFY(socket);
        QSignalSpy disconnected(socket.get(), &QSslSocket::disconnected);
        socket->write(QByteArray("GET / HTTP/1.1\r\nHost: x\r\n\r\n"));
        QVERIFY(closedOnServer.wait(kWaitMs));
        const auto info = closedOnServer.first().at(1).value<protocol::CloseInfo>();
        QVERIFY(info.code == protocol::ErrorCode::ProtocolViolation);
        QTRY_COMPARE_WITH_TIMEOUT(disconnected.count(), 1, kWaitMs);
    }

    void oversizedFrameAfterTlsClosesSession()
    {
        startServer();
        QSignalSpy closedOnServer(m_server.get(), &server::Server::sessionClosed);
        auto socket = rawTlsSocket();
        QVERIFY(socket);
        socket->write(QByteArray("\x7f\xff\xff\xff\x00\x01\x00\x00", 8)); // claims 2 GiB
        QVERIFY(closedOnServer.wait(kWaitMs));
        QVERIFY(closedOnServer.first().at(1).value<protocol::CloseInfo>().code ==
                protocol::ErrorCode::ProtocolViolation);
    }

    void tls12OnlyClientIsRejected()
    {
        startServer();
        QSslSocket socket;
        QSslConfiguration config = QSslConfiguration::defaultConfiguration();
        config.setProtocol(QSsl::TlsV1_2);
        config.setPeerVerifyMode(QSslSocket::VerifyNone);
        socket.setSslConfiguration(config);
        QSignalSpy encrypted(&socket, &QSslSocket::encrypted);
        QSignalSpy failed(&socket, &QSslSocket::errorOccurred);
        socket.connectToHostEncrypted(QStringLiteral("127.0.0.1"), m_server->serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(failed.count() > 0, kWaitMs);
        QCOMPARE(encrypted.count(), 0);
        QCOMPARE(m_server->sessionCount(), 0);
    }

    void silentConnectionIsDroppedAfterHandshakeTimeout()
    {
        startServer([](server::ServerConfig &c) { c.tlsHandshakeTimeoutMs = 500; });
        QTcpSocket socket;
        QSignalSpy connected(&socket, &QTcpSocket::connected);
        QSignalSpy disconnected(&socket, &QTcpSocket::disconnected);
        socket.connectToHost(QHostAddress::LocalHost, m_server->serverPort());
        QVERIFY(connected.wait(kWaitMs));
        // Send nothing: the server must not wait forever for a TLS ClientHello.
        QTRY_COMPARE_WITH_TIMEOUT(disconnected.count(), 1, 5'000);
    }

    void connectionRefusedIsReported()
    {
        startServer();
        const quint16 port = m_server->serverPort();
        cleanup(); // nothing listens on the port any more
        client::ConnectOptions o;
        o.host = QStringLiteral("127.0.0.1");
        o.port = port;
        client::Connection connection;
        QSignalSpy disconnected(&connection, &client::Connection::disconnected);
        connection.connectToServer(o);
        QVERIFY(disconnected.wait(kWaitMs));
        QVERIFY(disconnectInfo(disconnected).reason == client::DisconnectInfo::Reason::NetworkError);
        QCOMPARE(disconnected.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TestTlsSession)
#include "tst_tls_session.moc"
