// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/protocol/Frame.h"

#include <QTest>

using namespace opentalk::protocol;

class TestFrame : public QObject {
    Q_OBJECT

private slots:
    void headerLayoutIsBigEndian()
    {
        const auto bytes = encodeFrame(0x0102, QByteArrayView("abc"));
        QVERIFY(bytes);
        QCOMPARE(*bytes, QByteArray("\x00\x00\x00\x03\x01\x02\x00\x00"
                                    "abc",
                                    11));
    }

    void roundTripSingleFrame()
    {
        const QByteArray payload("hello, server");
        FrameDecoder decoder;
        QList<Frame> frames;
        QVERIFY(decoder.feed(*encodeFrame(7, payload), frames) == DecodeError::None);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames.first(), (Frame{7, payload}));
        QCOMPARE(decoder.bufferedBytes(), 0);
    }

    void emptyPayloadIsAllowed()
    {
        FrameDecoder decoder;
        QList<Frame> frames;
        QVERIFY(decoder.feed(*encodeFrame(42, {}), frames) == DecodeError::None);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames.first().type, quint16(42));
        QVERIFY(frames.first().payload.isEmpty());
    }

    void byteByByteDelivery()
    {
        const QByteArray stream = *encodeFrame(1, "first") + *encodeFrame(2, "second");
        FrameDecoder decoder;
        QList<Frame> frames;
        for (char byte : stream) {
            QVERIFY(decoder.feed(QByteArrayView(&byte, 1), frames) == DecodeError::None);
        }
        QCOMPARE(frames.size(), 2);
        QCOMPARE(frames[0], (Frame{1, "first"}));
        QCOMPARE(frames[1], (Frame{2, "second"}));
        QCOMPARE(decoder.bufferedBytes(), 0);
    }

    void severalFramesInOneChunkWithTrailingPartial()
    {
        const QByteArray third = *encodeFrame(3, "third");
        const QByteArray stream = *encodeFrame(1, "a") + *encodeFrame(2, "b") + third.left(5);
        FrameDecoder decoder;
        QList<Frame> frames;
        QVERIFY(decoder.feed(stream, frames) == DecodeError::None);
        QCOMPARE(frames.size(), 2);
        QCOMPARE(decoder.bufferedBytes(), 5);
        QVERIFY(decoder.feed(QByteArrayView(third).sliced(5), frames) == DecodeError::None);
        QCOMPARE(frames.size(), 3);
        QCOMPARE(frames[2], (Frame{3, "third"}));
    }

    void encoderRejectsOversizedPayload()
    {
        const QByteArray big(17, 'x');
        QVERIFY(!encodeFrame(1, big, 16));
        QVERIFY(encodeFrame(1, big.left(16), 16));
    }

    void oversizedAnnouncementFailsBeforePayloadArrives()
    {
        // Header claims 4 GiB - 1. Only the 8 header bytes are sent.
        const QByteArray header("\xff\xff\xff\xff\x00\x01\x00\x00", 8);
        FrameDecoder decoder;
        QList<Frame> frames;
        QVERIFY(decoder.feed(header, frames) == DecodeError::PayloadTooLarge);
        QVERIFY(frames.isEmpty());
        QCOMPARE(decoder.bufferedBytes(), 0);
    }

    void limitIsInclusive()
    {
        FrameDecoder decoder(4);
        QList<Frame> frames;
        QVERIFY(decoder.feed(*encodeFrame(1, "abcd", 4), frames) == DecodeError::None);
        QCOMPARE(frames.size(), 1);
        QVERIFY(decoder.feed(*encodeFrame(1, "abcde", 5), frames) == DecodeError::PayloadTooLarge);
    }

    void reservedFlagsAreRejected()
    {
        QByteArray bytes = *encodeFrame(1, "x");
        bytes[7] = 0x01;
        FrameDecoder decoder;
        QList<Frame> frames;
        QVERIFY(decoder.feed(bytes, frames) == DecodeError::ReservedFlagsSet);
        QVERIFY(frames.isEmpty());
    }

    void framesBeforeAnErrorAreStillDelivered()
    {
        QByteArray bad = *encodeFrame(2, "y");
        bad[6] = 0x7f;
        FrameDecoder decoder;
        QList<Frame> frames;
        QVERIFY(decoder.feed(*encodeFrame(1, "ok") + bad, frames) == DecodeError::ReservedFlagsSet);
        QCOMPARE(frames.size(), 1);
    }

    void failureIsPermanent()
    {
        FrameDecoder decoder(1);
        QList<Frame> frames;
        QVERIFY(decoder.feed(*encodeFrame(1, "too big"), frames) == DecodeError::PayloadTooLarge);
        QVERIFY(decoder.feed(*encodeFrame(1, "a", 1), frames) == DecodeError::PayloadTooLarge);
        QVERIFY(frames.isEmpty());
        QVERIFY(decoder.hasFailed());
    }
};

QTEST_GUILESS_MAIN(TestFrame)
#include "tst_frame.moc"
