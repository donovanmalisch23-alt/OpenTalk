// SPDX-License-Identifier: GPL-3.0-only
// Client and server session state machines connected through an in-memory pipe with a
// fake clock, so every protocol rule is tested deterministically and instantly.

#include "opentalk/protocol/Frame.h"
#include "opentalk/protocol/Session.h"

#include <QSignalSpy>
#include <QTest>

using namespace opentalk::protocol;

namespace {

/// Connects a client and a server session. Bytes are queued and only delivered by
/// pump(), which mirrors how a real socket delivers data on a later event-loop turn.
struct Pipe {
    ServerSession server;
    ClientSession client;
    QByteArray toServer;
    QByteArray toClient;
    qint64 now = 0;
    bool serverCloseRequested = false;
    bool clientCloseRequested = false;
    QList<CloseInfo> serverClosed;
    QList<CloseInfo> clientClosed;

    explicit Pipe(ServerSession::Config serverConfig = {}, ClientSession::Config clientConfig = {})
        : server(withName(std::move(serverConfig)), 0)
        , client(std::move(clientConfig))
    {
        QObject::connect(&client, &Session::sendRequested, [this](const QByteArray &b) { toServer += b; });
        QObject::connect(&server, &Session::sendRequested, [this](const QByteArray &b) { toClient += b; });
        QObject::connect(&server, &Session::closeRequested, [this] { serverCloseRequested = true; });
        QObject::connect(&client, &Session::closeRequested, [this] { clientCloseRequested = true; });
        QObject::connect(&server, &Session::closed, [this](const CloseInfo &i) { serverClosed << i; });
        QObject::connect(&client, &Session::closed, [this](const CloseInfo &i) { clientClosed << i; });
    }

    static ServerSession::Config withName(ServerSession::Config config)
    {
        config.serverName = QStringLiteral("Test server");
        config.serverVersion = QStringLiteral("9.9");
        return config;
    }

    /// Delivers queued bytes in both directions until nothing is left. A side that asked
    /// to close stops receiving, like a closed socket.
    void pump()
    {
        for (int guard = 0; guard < 100 && (!toServer.isEmpty() || !toClient.isEmpty()); ++guard) {
            const QByteArray s = std::exchange(toServer, {});
            const QByteArray c = std::exchange(toClient, {});
            if (!s.isEmpty() && !serverCloseRequested) {
                server.receive(s, now);
            }
            if (!c.isEmpty() && !clientCloseRequested) {
                client.receive(c, now);
            }
        }
    }

    void connect()
    {
        client.start(now);
        pump();
    }

    void tick(qint64 at)
    {
        now = at;
        server.tick(now);
        client.tick(now);
        pump();
    }
};

QByteArray rawFrame(MessageType type, const QByteArray &payload)
{
    return *encodeFrame(static_cast<quint16>(type), payload);
}

} // namespace

class TestSession : public QObject {
    Q_OBJECT

private slots:
    void handshakeSucceeds()
    {
        Pipe p;
        QSignalSpy established(&p.client, &ClientSession::established);
        p.connect();
        QVERIFY(p.client.state() == SessionState::Established);
        QVERIFY(p.server.state() == SessionState::Established);
        QCOMPARE(established.count(), 1);
        QCOMPARE(p.client.welcome().serverName, QStringLiteral("Test server"));
        QCOMPARE(p.client.welcome().version, quint16(1));
        QCOMPARE(p.server.clientHello().clientName, QStringLiteral("OpenTalk"));
        QCOMPARE(p.server.negotiatedVersion(), quint16(1));
    }

    void versionMismatchClosesBothSides()
    {
        ClientSession::Config clientConfig;
        clientConfig.versions = {2, 3};
        Pipe p({}, clientConfig);
        p.connect();
        QVERIFY(p.client.state() == SessionState::Closed);
        QCOMPARE(p.clientClosed.size(), 1);
        QVERIFY(p.clientClosed.first().initiator == CloseInfo::Initiator::Peer);
        QVERIFY(p.clientClosed.first().code == ErrorCode::UnsupportedVersion);
        QVERIFY(p.server.state() == SessionState::Closed);
    }

    void clientRejectsVersionItDidNotOffer()
    {
        ClientSession client({});
        QList<CloseInfo> closed;
        connect(&client, &Session::closed, this, [&](const CloseInfo &i) { closed << i; });
        client.start(0);
        client.receive(encodeMessage(Welcome{7, QStringLiteral("Evil"), QStringLiteral("1"), 65536, 15}), 1);
        QVERIFY(client.state() == SessionState::Closed);
        QCOMPARE(closed.size(), 1);
        QVERIFY(closed.first().code == ErrorCode::UnsupportedVersion);
    }

    void serverRequiresHelloFirst()
    {
        Pipe p;
        p.server.receive(encodeMessage(Ping{1}), 0);
        QVERIFY(p.server.state() == SessionState::Closed);
        QVERIFY(p.serverCloseRequested);
        QVERIFY(p.serverClosed.first().code == ErrorCode::ProtocolViolation);
        // The server explained why before closing.
        FrameDecoder decoder;
        QList<Frame> frames;
        decoder.feed(p.toClient, frames);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames.first().type, static_cast<quint16>(MessageType::Error));
    }

    void secondHelloIsAViolation()
    {
        Pipe p;
        p.connect();
        p.server.receive(encodeMessage(Hello{{1, 1}, QStringLiteral("x"), QStringLiteral("y")}), 1);
        QVERIFY(p.server.state() == SessionState::Closed);
        QVERIFY(p.serverClosed.first().code == ErrorCode::ProtocolViolation);
    }

    void heartbeatMeasuresRoundTrip()
    {
        Pipe p;
        p.connect();
        QSignalSpy rtt(&p.client, &ClientSession::roundTripMeasured);
        p.tick(14'000);
        QCOMPARE(rtt.count(), 0); // interval not reached yet
        p.now = 15'000;
        p.client.tick(p.now);
        p.now = 15'040; // the pong arrives 40 ms later
        p.pump();
        QCOMPARE(rtt.count(), 1);
        QCOMPARE(p.client.lastRoundTripMs(), qint64(40));
        QVERIFY(p.server.state() == SessionState::Established);
    }

    void regularTrafficKeepsSessionAlive()
    {
        Pipe p;
        p.connect();
        for (qint64 t = 1'000; t <= 300'000; t += 1'000) {
            p.tick(t);
        }
        QVERIFY(p.client.state() == SessionState::Established);
        QVERIFY(p.server.state() == SessionState::Established);
    }

    void clientTimesOutWhenServerGoesSilent()
    {
        Pipe p;
        p.connect();
        p.serverCloseRequested = true; // server stops answering
        p.tick(15'000);
        p.tick(30'000);
        p.tick(45'000);
        QVERIFY(p.client.state() == SessionState::Established);
        p.tick(45'001);
        QVERIFY(p.client.state() == SessionState::Closed);
        QVERIFY(p.clientClosed.first().code == ErrorCode::Timeout);
        QVERIFY(p.clientClosed.first().initiator == CloseInfo::Initiator::Local);
    }

    void serverDropsSilentClient()
    {
        Pipe p;
        p.connect();
        p.server.tick(45'001);
        QVERIFY(p.server.state() == SessionState::Closed);
        QVERIFY(p.serverClosed.first().code == ErrorCode::Timeout);
        p.pump();
        QVERIFY(p.clientClosed.first().initiator == CloseInfo::Initiator::Peer);
        QVERIFY(p.clientClosed.first().code == ErrorCode::Timeout);
    }

    void serverDropsClientThatNeverSaysHello()
    {
        Pipe p;
        p.server.tick(10'000);
        QVERIFY(p.server.state() == SessionState::Handshaking);
        p.server.tick(10'001);
        QVERIFY(p.server.state() == SessionState::Closed);
        QVERIFY(p.serverClosed.first().code == ErrorCode::Timeout);
    }

    void clientGoodbyeIsOrderly()
    {
        Pipe p;
        p.connect();
        p.client.disconnectFromServer();
        p.pump();
        QVERIFY(p.clientClosed.first().initiator == CloseInfo::Initiator::Local);
        QVERIFY(p.serverClosed.first().initiator == CloseInfo::Initiator::Peer);
        QVERIFY(p.serverClosed.first().code == ErrorCode::None);
    }

    void serverShutdownReachesClient()
    {
        Pipe p;
        p.connect();
        p.server.shutdown(ErrorCode::ServerShuttingDown);
        p.pump();
        QVERIFY(p.clientClosed.first().initiator == CloseInfo::Initiator::Peer);
        QVERIFY(p.clientClosed.first().code == ErrorCode::ServerShuttingDown);
    }

    void oversizedFrameClosesSession()
    {
        Pipe p;
        p.connect();
        p.server.receive(QByteArray("\x00\x10\x00\x01\x00\x03\x00\x00", 8), 1); // claims 1 MiB
        QVERIFY(p.server.state() == SessionState::Closed);
        QVERIFY(p.serverClosed.first().code == ErrorCode::ProtocolViolation);
    }

    void malformedMessageClosesSession()
    {
        Pipe p;
        p.connect();
        p.server.receive(rawFrame(MessageType::Ping, QByteArray("\xa1\x01", 2)), 1); // truncated CBOR
        QVERIFY(p.server.state() == SessionState::Closed);
        QVERIFY(p.serverClosed.first().code == ErrorCode::MalformedMessage);
    }

    void unknownMessageIsToleratedAfterHandshake()
    {
        Pipe p;
        p.connect();
        p.server.receive(*encodeFrame(0x0777, QByteArray("\xa0", 1)), 1);
        QVERIFY(p.server.state() == SessionState::Established);
        QVERIFY(!p.toClient.isEmpty()); // an Error{UnknownMessage} was sent
        p.pump();
        QVERIFY(p.client.state() == SessionState::Established);
    }

    void unknownMessageBeforeHandshakeCloses()
    {
        Pipe p;
        p.server.receive(*encodeFrame(0x0777, QByteArray("\xa0", 1)), 0);
        QVERIFY(p.server.state() == SessionState::Closed);
    }

    void floodingIsRateLimited()
    {
        Pipe p;
        p.connect();
        QByteArray flood;
        for (int i = 0; i < 150; ++i) {
            flood += encodeMessage(Ping{static_cast<quint64>(i)});
        }
        p.server.receive(flood, 1);
        QVERIFY(p.server.state() == SessionState::Closed);
        QVERIFY(p.serverClosed.first().code == ErrorCode::RateLimited);
    }

    void steadyTrafficUnderTheLimitIsAccepted()
    {
        Pipe p;
        p.connect();
        // 40 messages per second for 10 seconds stays under the 50/s default.
        for (int i = 0; i < 400; ++i) {
            p.server.receive(encodeMessage(Ping{static_cast<quint64>(i)}), 1 + i * 25);
        }
        QVERIFY(p.server.state() == SessionState::Established);
    }

    void transportLossIsReportedOnce()
    {
        Pipe p;
        p.connect();
        p.client.transportClosed(QStringLiteral("connection reset"));
        p.client.transportClosed(QStringLiteral("again"));
        p.client.disconnectFromServer();
        QCOMPARE(p.clientClosed.size(), 1);
        QVERIFY(p.clientClosed.first().initiator == CloseInfo::Initiator::Transport);
    }

    void nothingIsProcessedAfterClose()
    {
        Pipe p;
        p.connect();
        p.client.disconnectFromServer();
        p.toServer.clear();
        QSignalSpy sent(&p.client, &Session::sendRequested);
        p.client.receive(encodeMessage(Ping{5}), 2);
        p.client.tick(100'000);
        QCOMPARE(sent.count(), 0);
    }
};

QTEST_GUILESS_MAIN(TestSession)
#include "tst_session.moc"
