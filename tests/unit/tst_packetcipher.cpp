// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/protocol/VoicePacket.h"
#include "opentalk/security/PacketCipher.h"

#include <QTest>

using namespace opentalk::security;
using namespace opentalk::protocol;

class TestPacketCipher : public QObject {
    Q_OBJECT

private:
    QByteArray m_key = randomPacketKey();

    PacketSealer sealer(quint32 session = 7) { return {m_key, PacketDirection::ClientToServer, session}; }
    PacketOpener opener(quint32 session = 7) { return {m_key, PacketDirection::ClientToServer, session}; }

private slots:
    void keysAreRandom()
    {
        QCOMPARE(m_key.size(), kPacketKeyBytes);
        QVERIFY(randomPacketKey() != randomPacketKey());
    }

    void roundTrip()
    {
        PacketSealer s = sealer();
        PacketOpener o = opener();
        const QByteArray datagram = s.seal(1, "hello voice");
        QCOMPARE(datagram.size(), kPacketOverheadBytes + 11);
        QVERIFY(!datagram.contains("hello voice")); // encrypted
        PacketHeader header;
        const std::optional<QByteArray> plain = o.open(datagram, &header);
        QVERIFY(plain.has_value());
        QCOMPARE(*plain, QByteArray("hello voice"));
        QCOMPARE(header.kind, quint8{1});
        QCOMPARE(header.sessionId, quint32{7});
        QCOMPARE(header.counter, quint64{1});
        QVERIFY(o.open(s.seal(1, QByteArray())).has_value()); // empty plaintext is fine
    }

    void rejectsTamperingAnywhere()
    {
        PacketSealer s = sealer();
        const QByteArray datagram = s.seal(1, "payload bytes");
        for (qsizetype i = 0; i < datagram.size(); ++i) {
            PacketOpener o = opener();
            QByteArray damaged = datagram;
            damaged[i] = static_cast<char>(damaged[i] ^ 0x01);
            QVERIFY2(!o.open(damaged).has_value(), qPrintable(QStringLiteral("byte %1").arg(i)));
        }
        PacketOpener o = opener();
        QVERIFY(!o.open(datagram.chopped(1)).has_value());
        QVERIFY(!o.open(datagram.left(kPacketHeaderBytes)).has_value());
        QVERIFY(!o.open(QByteArray()).has_value());
        QVERIFY(o.open(datagram).has_value()); // the original is still accepted
    }

    void rejectsWrongKeySessionOrDirection()
    {
        PacketSealer s = sealer();
        const QByteArray datagram = s.seal(1, "x");
        PacketOpener otherKey(randomPacketKey(), PacketDirection::ClientToServer, 7);
        QVERIFY(!otherKey.open(datagram).has_value());
        PacketOpener otherSession = opener(8);
        QVERIFY(!otherSession.open(datagram).has_value());
        // A packet reflected back to its sender must not be accepted.
        PacketOpener otherDirection(m_key, PacketDirection::ServerToClient, 7);
        QVERIFY(!otherDirection.open(datagram).has_value());
    }

    void rejectsReplays()
    {
        PacketSealer s = sealer();
        PacketOpener o = opener();
        QList<QByteArray> packets;
        for (int i = 0; i < 100; ++i) {
            packets.append(s.seal(1, QByteArray::number(i)));
        }
        QVERIFY(o.open(packets[10]).has_value());
        QVERIFY(!o.open(packets[10]).has_value()); // exact replay
        // Reordering within the 64-packet window is accepted once.
        QVERIFY(o.open(packets[5]).has_value());
        QVERIFY(!o.open(packets[5]).has_value());
        QVERIFY(o.open(packets[90]).has_value());
        QVERIFY(!o.open(packets[10]).has_value()); // now too old
        QVERIFY(!o.open(packets[26]).has_value()); // counter 27 is 64 behind 91: outside
        QVERIFY(o.open(packets[27]).has_value());  // counter 28 is 63 behind: inside
        QVERIFY(o.open(packets[89]).has_value());
        QVERIFY(o.open(packets[99]).has_value());
        QVERIFY(!o.open(packets[90]).has_value());
    }

    void forgeriesDoNotMoveTheWindow()
    {
        PacketSealer s = sealer();
        PacketOpener o = opener();
        const QByteArray first = s.seal(1, "a");
        // A forged packet with a huge counter must not push genuine packets out.
        QByteArray forged = first;
        forged[5] = '\x7f';
        QVERIFY(!o.open(forged).has_value());
        QVERIFY(o.open(first).has_value());
    }

    void voiceAudioRoundTrip()
    {
        const AudioData audio{AudioStream::Media, 513, 42, QByteArray(300, 'o')};
        const std::optional<AudioData> decoded = decodeVoiceAudio(encodeVoiceAudio(audio));
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == audio);
        QVERIFY(!decodeVoiceAudio(QByteArray(7, '\0')).has_value()); // header only
        QByteArray badStream = encodeVoiceAudio(audio);
        badStream[0] = 3;
        QVERIFY(!decodeVoiceAudio(badStream).has_value());
        QVERIFY(!decodeVoiceAudio(encodeVoiceAudio({AudioStream::Voice, 1, 0, QByteArray(1276, 'x')})).has_value());
    }

    void voicePingRoundTrip()
    {
        const std::optional<VoicePing> ping = decodeVoicePing(encodeVoicePing({0x0102030405060708ULL, 1}));
        QVERIFY(ping.has_value());
        QCOMPARE(ping->token, 0x0102030405060708ULL);
        QCOMPARE(ping->flags, quint8{1});
        QCOMPARE(decodeVoicePong(encodeVoicePong(99)).value_or(0), quint64{99});
        QVERIFY(!decodeVoicePing(QByteArray(8, '\0')).has_value());
        QVERIFY(!decodeVoicePong(QByteArray(9, '\0')).has_value());
    }
};

QTEST_GUILESS_MAIN(TestPacketCipher)
#include "tst_packetcipher.moc"
