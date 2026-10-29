// SPDX-License-Identifier: GPL-3.0-only
#include "AudioTestUtil.h"

#include "opentalk/audio/AudioFormat.h"
#include "opentalk/audio/Dsp.h"
#include "opentalk/audio/JitterBuffer.h"
#include "opentalk/audio/OpusCodec.h"
#include "opentalk/audio/RingBuffer.h"
#include "opentalk/audio/Sounds.h"

#include <QTest>

#include <thread>

using namespace opentalk::audio;
using opentalk::test::sine;
using opentalk::test::toneStrength;

namespace {

QByteArray packet(int n)
{
    return QByteArray(1, static_cast<char>(n & 0x7f)) + "x";
}

int id(const QByteArray &p)
{
    return p.isEmpty() ? -1 : static_cast<unsigned char>(p.at(0));
}

} // namespace

class TestAudioCore : public QObject {
    Q_OBJECT

private slots:
    void ringBufferWrapsAndRefusesOverflow()
    {
        RingBuffer ring(8);
        float in[6] = {1, 2, 3, 4, 5, 6};
        float out[8] = {};
        QCOMPARE(ring.write(in, 6), std::size_t(6));
        QCOMPARE(ring.read(out, 4), std::size_t(4));
        QCOMPARE(ring.write(in, 6), std::size_t(6)); // wraps around
        QCOMPARE(ring.write(in, 6), std::size_t(0)); // full: nothing overwritten
        QCOMPARE(ring.available(), std::size_t(8));
        QCOMPARE(ring.read(out, 8), std::size_t(8));
        QCOMPARE(out[0], 5.0f);
        QCOMPARE(out[2], 1.0f);
        QCOMPARE(out[7], 6.0f);
    }

    void ringBufferAcrossThreads()
    {
        RingBuffer ring(1000);
        constexpr int total = 200000;
        std::thread producer([&ring] {
            for (int i = 0; i < total;) {
                const float v = static_cast<float>(i);
                if (ring.write(&v, 1) == 1) {
                    ++i;
                }
            }
        });
        int expected = 0;
        bool ordered = true;
        while (expected < total) {
            float v = 0;
            if (ring.read(&v, 1) == 1) {
                ordered = ordered && v == static_cast<float>(expected);
                ++expected;
            }
        }
        producer.join();
        QVERIFY(ordered);
    }

    void levelsAndClipping()
    {
        std::vector<float> silence(960, 0.0f);
        QCOMPARE(levelDbfs(silence), -100.0f);
        std::vector<float> loud(960);
        for (int i = 0; i < 960; ++i) {
            loud[static_cast<std::size_t>(i)] = sine(i, 1000, 1.0);
        }
        QVERIFY(std::abs(levelDbfs(loud) - (-3.01f)) < 0.1f); // a full-scale sine is -3 dBFS RMS
        std::vector<float> hot = {2.0f, -3.0f, 0.5f};
        softClip(hot);
        QVERIFY(hot[0] <= 1.0f && hot[0] > 0.9f); // never beyond full scale
        QVERIFY(hot[1] >= -1.0f && hot[1] < -0.9f);
        QCOMPARE(hot[2], 0.5f);
    }

    void voiceActivityHasHangover()
    {
        VoiceActivityDetector vad(-40.0f, 3);
        QVERIFY(!vad.update(-60));
        QVERIFY(vad.update(-30));
        QVERIFY(vad.update(-60)); // the end of a word is kept
        QVERIFY(vad.update(-60));
        QVERIFY(vad.update(-60));
        QVERIFY(!vad.update(-60));
    }

    void opusRoundTripKeepsTheTone()
    {
        OpusEncoderWrapper encoder(OpusEncoderWrapper::Content::Voice, 32000);
        OpusDecoderWrapper decoder;
        QVERIFY(encoder.isValid() && decoder.isValid());
        std::vector<float> frame(kFrameSamples);
        std::vector<float> out(kFrameSamples);
        QVector<float> decoded;
        qint64 index = 0;
        for (int f = 0; f < 50; ++f) {
            for (float &s : frame) {
                s = sine(index++, 440.0);
            }
            const QByteArray packet = encoder.encode(frame);
            QVERIFY(!packet.isEmpty() && packet.size() < 400);
            QVERIFY(decoder.decode(packet, out));
            decoded += QVector<float>(out.begin(), out.end());
        }
        const QVector<float> tail = decoded.mid(decoded.size() / 2);
        QVERIFY2(toneStrength(tail, 440.0) > 0.15, "the tone survives the codec");
        QVERIFY(toneStrength(tail, 1000.0) < 0.02);
    }

    void opusConcealsLossAndRejectsGarbage()
    {
        OpusDecoderWrapper decoder;
        std::vector<float> out(kFrameSamples, 1.0f);
        QVERIFY(!decoder.decode(QByteArray(3000, 'x'), out));
        QCOMPARE(out[0], 0.0f);
        decoder.conceal({}, out); // must not crash without any history
        decoder.conceal(QByteArray("\xff\xff\xff", 3), out);
    }

    // ----------------------------------------------------------------- jitter buffer

    void jitterBufferWaitsForTargetThenPlaysInOrder()
    {
        JitterBuffer jb;
        QByteArray p;
        QByteArray fec;
        QCOMPARE(jb.targetFrames(), JitterBuffer::kMinTarget);
        jb.insert(10, packet(10), 0);
        QVERIFY(jb.pop(&p, &fec) == JitterBuffer::Result::Empty); // still buffering
        jb.insert(12, packet(12), 40);                            // out of order
        jb.insert(11, packet(11), 41);
        QVERIFY(jb.pop(&p, &fec) == JitterBuffer::Result::Packet);
        QCOMPARE(id(p), 10);
        QVERIFY(jb.pop(&p, &fec) == JitterBuffer::Result::Packet);
        QCOMPARE(id(p), 11);
        QVERIFY(jb.pop(&p, &fec) == JitterBuffer::Result::Packet);
        QCOMPARE(id(p), 12);
    }

    void lossUsesTheNextPacketForErrorCorrection()
    {
        JitterBuffer jb;
        QByteArray p;
        QByteArray fec;
        for (int s : {1, 2, 3, 5, 6}) {
            jb.insert(static_cast<quint16>(s), packet(s), s * 20);
        }
        for (int expected : {1, 2, 3}) {
            QVERIFY(jb.pop(&p, &fec) == JitterBuffer::Result::Packet);
            QCOMPARE(id(p), expected);
        }
        QVERIFY(jb.pop(&p, &fec) == JitterBuffer::Result::Lost);
        QCOMPARE(id(fec), 5); // packet 4 can be rebuilt from 5
        QVERIFY(jb.pop(&p, &fec) == JitterBuffer::Result::Packet);
        QCOMPARE(id(p), 5);
    }

    void latePacketsAreDropped()
    {
        JitterBuffer jb;
        QByteArray p;
        QByteArray fec;
        for (int s : {1, 2, 3}) {
            jb.insert(static_cast<quint16>(s), packet(s), s * 20);
        }
        QVERIFY(jb.pop(&p, &fec) == JitterBuffer::Result::Packet);
        QVERIFY(jb.pop(&p, &fec) == JitterBuffer::Result::Packet);
        jb.insert(1, packet(1), 100); // already played
        QVERIFY(jb.pop(&p, &fec) == JitterBuffer::Result::Packet);
        QCOMPARE(id(p), 3);
    }

    void spurtEndsAfterSilenceAndTheNextOneStartsFresh()
    {
        JitterBuffer jb;
        QByteArray p;
        QByteArray fec;
        for (int s : {1, 2, 3}) {
            jb.insert(static_cast<quint16>(s), packet(s), s * 20);
        }
        for (int i = 0; i < 3; ++i) {
            QVERIFY(jb.pop(&p, &fec) == JitterBuffer::Result::Packet);
        }
        int lost = 0;
        while (jb.pop(&p, &fec) == JitterBuffer::Result::Lost) {
            ++lost;
        }
        QCOMPARE(lost, JitterBuffer::kEndAfterMissing);
        QVERIFY(!jb.isPlaying());
        for (int s : {50, 51, 52}) {
            jb.insert(static_cast<quint16>(s), packet(s), 2000 + s * 20);
        }
        QVERIFY(jb.pop(&p, &fec) == JitterBuffer::Result::Packet);
        QCOMPARE(id(p), 50);
    }

    void catchesUpAfterAStall()
    {
        JitterBuffer jb;
        QByteArray p;
        QByteArray fec;
        for (int s = 0; s < 20; ++s) { // 20 packets arrive at once after a stall
            jb.insert(static_cast<quint16>(s), packet(s), 1000);
        }
        QVERIFY(jb.pop(&p, &fec) == JitterBuffer::Result::Packet);
        QVERIFY(jb.bufferedFrames() <= jb.targetFrames() + 4);
        QVERIFY(id(p) > 0); // the oldest frames were skipped
    }

    void growsWhenPacketsComeTooLateAndShrinksWhenSteady()
    {
        JitterBuffer jb;
        QByteArray p;
        QByteArray fec;
        jb.insert(0, packet(0), 0);
        jb.insert(1, packet(1), 20);
        QVERIFY(jb.pop(&p, &fec) == JitterBuffer::Result::Packet); // 0
        QVERIFY(jb.pop(&p, &fec) == JitterBuffer::Result::Packet); // 1
        QVERIFY(jb.pop(&p, &fec) == JitterBuffer::Result::Lost);   // 2 has not arrived
        const int before = jb.targetFrames();
        jb.insert(2, packet(2), 70); // arrives after it was concealed
        QCOMPARE(jb.latePackets(), 1);
        QCOMPARE(jb.targetFrames(), before + 1);
        jb.insert(3, packet(3), 71);
        // The next frame is a stretch (concealment without skipping), then playback goes on.
        QVERIFY(jb.pop(&p, &fec) == JitterBuffer::Result::Lost);
        QVERIFY(jb.pop(&p, &fec) == JitterBuffer::Result::Packet);
        QCOMPARE(id(p), 3);

        // A steady stream with extra packets queued: the buffer drops one frame per window.
        JitterBuffer steady;
        int sequence = 0;
        for (; sequence < 6; ++sequence) {
            steady.insert(static_cast<quint16>(sequence), packet(sequence & 0x7f), sequence * 20);
        }
        const int startBuffered = steady.bufferedFrames();
        for (int tick = 0; tick < 3 * JitterBuffer::kAdaptWindowFrames; ++tick, ++sequence) {
            steady.insert(static_cast<quint16>(sequence), packet(sequence & 0x7f), sequence * 20);
            QVERIFY(steady.pop(&p, &fec) == JitterBuffer::Result::Packet);
        }
        QVERIFY(steady.bufferedFrames() < startBuffered);
        QVERIFY(steady.bufferedFrames() >= JitterBuffer::kMinTarget - 1);
    }

    void sequenceNumbersWrapAround()
    {
        JitterBuffer jb;
        QByteArray p;
        QByteArray fec;
        for (int s : {65534, 65535, 0, 1}) {
            jb.insert(static_cast<quint16>(s), packet(s & 0x7f), 0);
        }
        QList<int> order;
        while (jb.pop(&p, &fec) == JitterBuffer::Result::Packet) {
            order << id(p);
        }
        QCOMPARE(order, (QList<int>{65534 & 0x7f, 65535 & 0x7f, 0, 1}));
    }

    void targetGrowsWithJitter()
    {
        JitterBuffer jb;
        QByteArray p;
        QByteArray fec;
        // Packets 20 ms apart in sequence but arriving 0 or 80 ms apart.
        for (int s = 0; s < 60; ++s) {
            jb.insert(static_cast<quint16>(s), packet(s), (s / 4) * 80);
            jb.pop(&p, &fec);
        }
        jb.reset();
        QVERIFY2(jb.targetFrames() > 3, qPrintable(QString::number(jb.jitterMs())));
    }

    void builtInSoundsExistAndAreGentle()
    {
        const SoundSet set = builtInSounds();
        for (int i = 0; i < static_cast<int>(EventSound::Count); ++i) {
            const QVector<float> &s = set[static_cast<std::size_t>(i)];
            QVERIFY2(!s.isEmpty(), qPrintable(soundFileName(static_cast<EventSound>(i))));
            QVERIFY(s.size() < kSampleRate); // under a second
            float peak = 0;
            for (float v : s) {
                peak = std::max(peak, std::abs(v));
            }
            QVERIFY(peak <= 0.3f && peak > 0.05f);
        }
        QCOMPARE(allSoundFileNames().size(), static_cast<int>(EventSound::Count));
    }
};

QTEST_GUILESS_MAIN(TestAudioCore)
#include "tst_audio_core.moc"
