// SPDX-License-Identifier: GPL-3.0-only
// MediaFile reads audio files in pieces, converted to OpenTalk's 48 kHz mono.

#include "AudioTestUtil.h"

#include "opentalk/audio/AudioFormat.h"
#include "opentalk/audio/MediaFile.h"

#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <numbers>
#include <vector>

using namespace opentalk::audio;
using opentalk::test::toneStrength;

namespace {

/// Writes a 16-bit PCM WAV file with the same tone on every channel.
void writeWav(const QString &path, int sampleRate, int channels, double seconds, double frequency)
{
    const auto frames = static_cast<quint32>(seconds * sampleRate);
    const quint32 dataBytes = frames * static_cast<quint32>(channels) * 2;
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + dataBytes);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << quint16(channels) << quint32(sampleRate) << quint32(sampleRate * channels * 2)
        << quint16(channels * 2) << quint16(16);
    out.writeRawData("data", 4);
    out << dataBytes;
    for (quint32 i = 0; i < frames; ++i) {
        const auto value =
            static_cast<qint16>(std::lround(16000.0 * std::sin(2 * std::numbers::pi * frequency * i / sampleRate)));
        for (int c = 0; c < channels; ++c) {
            out << value;
        }
    }
}

} // namespace

class TestMediaFile : public QObject {
    Q_OBJECT

private slots:
    void readsAndConvertsAFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("Song title.wav"));
        writeWav(path, 44100, 2, 2.5, 1000.0); // CD format: needs resampling and mixing to mono

        MediaFile file;
        QString error;
        QVERIFY2(file.open(path, &error), qPrintable(error));
        QCOMPARE(file.title(), QStringLiteral("Song title.wav"));
        QVERIFY(std::abs(file.durationMs() - 2500) <= 20);
        QCOMPARE(file.positionMs(), 0);

        std::vector<float> all;
        std::vector<float> frame(kFrameSamples);
        for (;;) {
            const std::size_t got = file.read(frame);
            all.insert(all.end(), frame.begin(), frame.begin() + static_cast<std::ptrdiff_t>(got));
            if (got < frame.size()) {
                break;
            }
        }
        QVERIFY(std::abs(static_cast<double>(all.size()) - 2.5 * kSampleRate) < kFrameSamples);
        QVERIFY(std::abs(file.positionMs() - 2500) <= 20);
        // 16000/32768 of full scale at 1 kHz, still 1 kHz after resampling.
        const double strength = toneStrength(all.data(), static_cast<qsizetype>(all.size()), 1000.0);
        QVERIFY2(std::abs(strength - 0.244) < 0.02, qPrintable(QString::number(strength)));
        QVERIFY(toneStrength(all.data(), static_cast<qsizetype>(all.size()), 1102.5) < 0.01);
        QCOMPARE(file.read(frame), std::size_t{0}); // stays at the end
    }

    void seeks()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("a.wav"));
        writeWav(path, 48000, 1, 3.0, 440.0);
        MediaFile file;
        QVERIFY(file.open(path, nullptr));
        QVERIFY(file.seekMs(2000));
        QCOMPARE(file.positionMs(), 2000);
        std::vector<float> rest(kSampleRate * 2);
        QVERIFY(std::abs(static_cast<double>(file.read(rest)) - kSampleRate) < 10);
        QVERIFY(file.seekMs(-500)); // limited to the start
        QCOMPARE(file.positionMs(), 0);
        QVERIFY(file.seekMs(10'000)); // and to the end
        QCOMPARE(file.positionMs(), 3000);
    }

    void refusesWhatItCannotRead()
    {
        QTemporaryDir dir;
        MediaFile file;
        QString error;
        QVERIFY(!file.open(dir.filePath(QStringLiteral("missing.mp3")), &error));
        QVERIFY(error.contains(QStringLiteral("does not exist")));
        const QString text = dir.filePath(QStringLiteral("notes.mp3"));
        QFile notes(text);
        QVERIFY(notes.open(QIODevice::WriteOnly));
        notes.write(QByteArray(4096, 'x'));
        notes.close();
        QVERIFY(!file.open(text, &error));
        QVERIFY(error.contains(QStringLiteral("WAV, FLAC or MP3")));
        QVERIFY(!file.isOpen());
        std::vector<float> frame(kFrameSamples);
        QCOMPARE(file.read(frame), std::size_t{0});
    }
};

QTEST_GUILESS_MAIN(TestMediaFile)
#include "tst_media_file.moc"
