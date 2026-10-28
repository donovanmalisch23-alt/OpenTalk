// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/protocol/VoicePacket.h"
#include "opentalk/security/PacketCipher.h"
#include "opentalk/server/VoiceRouter.h"

#include <QSignalSpy>
#include <QTest>

using namespace opentalk;
using protocol::AudioData;
using protocol::AudioStream;
using protocol::VoicePacketKind;
using security::PacketDirection;
using server::VoiceRouter;

namespace {

/// The client end of one session's keys.
struct FakeClient {
    FakeClient() = default;
    explicit FakeClient(const protocol::AudioSetup &setup)
        : toServer(setup.clientToServerKey, PacketDirection::ClientToServer, setup.udpSessionId)
        , fromServer(setup.serverToClientKey, PacketDirection::ServerToClient, setup.udpSessionId)
    {
    }
    QByteArray audio(quint16 sequence, quint32 claimedUserId = 0)
    {
        return toServer.seal(static_cast<quint8>(VoicePacketKind::Audio),
                             protocol::encodeVoiceAudio({AudioStream::Voice, sequence, claimedUserId, "opus"}));
    }
    QByteArray ping(bool receivingUdp)
    {
        return toServer.seal(
            static_cast<quint8>(VoicePacketKind::Ping),
            protocol::encodeVoicePing({1, receivingUdp ? protocol::kVoicePingReceivingUdp : quint8{0}}));
    }
    security::PacketSealer toServer;
    security::PacketOpener fromServer;
};

const QHostAddress kAddressA(QStringLiteral("192.0.2.1"));
const QHostAddress kAddressB(QStringLiteral("192.0.2.2"));

} // namespace

class TestVoiceRouter : public QObject {
    Q_OBJECT

private:
    qint64 m_now = 1000;
    QHash<quint32, QList<quint32>> m_channelOf; // sender -> recipients

    std::unique_ptr<VoiceRouter> makeRouter(VoiceRouter::Config config = {})
    {
        return std::make_unique<VoiceRouter>(
            config, [this](quint32 id) { return m_channelOf.value(id); }, [this] { return m_now; });
    }

private slots:
    void init()
    {
        m_now = 1000;
        m_channelOf.clear();
    }

    void setupHasFreshKeys()
    {
        auto router = makeRouter();
        const protocol::AudioSetup a = router->addSession(1, 10350);
        const protocol::AudioSetup b = router->addSession(2, 10350);
        QCOMPARE(a.udpPort, quint16{10350});
        QCOMPARE(a.clientToServerKey.size(), security::kPacketKeyBytes);
        QVERIFY(a.clientToServerKey != a.serverToClientKey);
        QVERIFY(a.clientToServerKey != b.clientToServerKey);
        QVERIFY(a.udpSessionId != b.udpSessionId);
        QVERIFY(a.udpSessionId != 0);
    }

    void pingIsAnsweredAtTheSenderAddress()
    {
        auto router = makeRouter();
        FakeClient client(router->addSession(1, 10350));
        QSignalSpy sent(router.get(), &VoiceRouter::sendDatagram);
        router->handleDatagram(client.ping(false), kAddressA, 5000);
        QCOMPARE(sent.size(), 1);
        QCOMPARE(sent.first().at(1).value<QHostAddress>(), kAddressA);
        QCOMPARE(sent.first().at(2).value<quint16>(), quint16{5000});
        security::PacketHeader header;
        const auto pong = client.fromServer.open(sent.first().at(0).toByteArray(), &header);
        QVERIFY(pong.has_value());
        QCOMPARE(header.kind, static_cast<quint8>(VoicePacketKind::Pong));
        QCOMPARE(protocol::decodeVoicePong(*pong).value_or(0), quint64{1});
    }

    void forwardsToChannelOverTheRightPath()
    {
        auto router = makeRouter();
        FakeClient alice(router->addSession(1, 10350));
        FakeClient bob(router->addSession(2, 10350));
        router->addSession(3, 10350); // carol: never used UDP
        m_channelOf[1] = {2, 3};
        // Bob has a working UDP path both ways; Carol does not.
        router->handleDatagram(bob.ping(true), kAddressB, 6000);
        QVERIFY(router->isUsingUdp(2));
        QVERIFY(!router->isUsingUdp(3));

        QSignalSpy udp(router.get(), &VoiceRouter::sendDatagram);
        QSignalSpy tunnel(router.get(), &VoiceRouter::sendTunnel);
        // Alice claims to be user 3; the server must stamp her real ID.
        router->handleDatagram(alice.audio(5, 3), kAddressA, 5000);
        QCOMPARE(udp.size(), 1);
        QCOMPARE(udp.first().at(1).value<QHostAddress>(), kAddressB);
        const auto plain = bob.fromServer.open(udp.first().at(0).toByteArray());
        QVERIFY(plain.has_value());
        const auto audio = protocol::decodeVoiceAudio(*plain);
        QVERIFY(audio.has_value());
        QCOMPARE(audio->userId, quint32{1});
        QCOMPARE(audio->sequence, quint16{5});
        QCOMPARE(audio->payload, QByteArray("opus"));

        QCOMPARE(tunnel.size(), 1);
        QCOMPARE(tunnel.first().at(0).value<quint32>(), quint32{3});
        QCOMPARE(tunnel.first().at(1).value<AudioData>().userId, quint32{1});
        QCOMPARE(router->stats().receivedUdp, quint64{1});
        QCOMPARE(router->stats().sentUdp, quint64{1});
        QCOMPARE(router->stats().sentTunnel, quint64{1});
    }

    void tunnelAudioIsForwardedAndStampedToo()
    {
        auto router = makeRouter();
        router->addSession(1, 0);
        router->addSession(2, 0);
        m_channelOf[1] = {2};
        QSignalSpy tunnel(router.get(), &VoiceRouter::sendTunnel);
        router->handleTunnel(1, AudioData{AudioStream::Voice, 9, 77, "opus"});
        QCOMPARE(tunnel.size(), 1);
        QCOMPARE(tunnel.first().at(1).value<AudioData>().userId, quint32{1});
        router->handleTunnel(99, AudioData{AudioStream::Voice, 9, 0, "opus"}); // unknown session
        QCOMPARE(tunnel.size(), 1);
        QCOMPARE(router->stats().rejected, quint64{1});
    }

    void rejectsForgeriesReplaysAndStrangers()
    {
        auto router = makeRouter();
        FakeClient alice(router->addSession(1, 10350));
        router->addSession(2, 10350);
        m_channelOf[1] = {2};
        QSignalSpy tunnel(router.get(), &VoiceRouter::sendTunnel);
        const QByteArray packet = alice.audio(1);
        router->handleDatagram(packet, kAddressA, 5000);
        router->handleDatagram(packet, kAddressA, 5000); // replay
        QByteArray forged = alice.audio(2);
        forged[forged.size() - 1] = static_cast<char>(forged.back() ^ 1);
        router->handleDatagram(forged, kAddressA, 5000);
        router->handleDatagram(QByteArray(40, 'x'), kAddressA, 5000);
        router->handleDatagram(QByteArray(3, 'x'), kAddressA, 5000);
        QCOMPARE(tunnel.size(), 1);
        QCOMPARE(router->stats().rejected, quint64{4});
    }

    void removedSessionsAreForgotten()
    {
        auto router = makeRouter();
        FakeClient alice(router->addSession(1, 10350));
        router->removeSession(1);
        QSignalSpy sent(router.get(), &VoiceRouter::sendDatagram);
        router->handleDatagram(alice.ping(true), kAddressA, 5000);
        QCOMPARE(sent.size(), 0);
    }

    void floodIsCut()
    {
        VoiceRouter::Config config;
        config.packetsPerSecond = 50;
        config.packetBurst = 10;
        auto router = makeRouter(config);
        FakeClient alice(router->addSession(1, 10350));
        router->addSession(2, 0);
        m_channelOf[1] = {2};
        QSignalSpy tunnel(router.get(), &VoiceRouter::sendTunnel);
        for (int i = 0; i < 30; ++i) {
            router->handleDatagram(alice.audio(static_cast<quint16>(i)), kAddressA, 5000);
        }
        QCOMPARE(tunnel.size(), 10);
        m_now += 1000; // a second later the budget is back
        for (int i = 30; i < 40; ++i) {
            router->handleDatagram(alice.audio(static_cast<quint16>(i)), kAddressA, 5000);
        }
        QCOMPARE(tunnel.size(), 20);
    }

    void udpPathExpiresAndFallsBack()
    {
        auto router = makeRouter();
        FakeClient alice(router->addSession(1, 10350));
        FakeClient bob(router->addSession(2, 10350));
        m_channelOf[2] = {1};
        router->handleDatagram(alice.ping(true), kAddressA, 5000);
        QVERIFY(router->isUsingUdp(1));
        m_now += 25'000; // no pings for longer than the timeout
        QVERIFY(!router->isUsingUdp(1));
        router->handleDatagram(alice.ping(true), kAddressA, 5001); // address changed (NAT)
        QVERIFY(router->isUsingUdp(1));
        QSignalSpy udp(router.get(), &VoiceRouter::sendDatagram);
        router->handleTunnel(2, AudioData{AudioStream::Voice, 1, 0, "x"});
        QCOMPARE(udp.size(), 1);
        QCOMPARE(udp.first().at(2).value<quint16>(), quint16{5001});
        // A client that falls back to the tunnel itself also gets audio over the tunnel.
        router->handleTunnel(1, AudioData{AudioStream::Voice, 1, 0, "x"});
        QVERIFY(!router->isUsingUdp(1));
    }
};

QTEST_GUILESS_MAIN(TestVoiceRouter)
#include "tst_voicerouter.moc"
