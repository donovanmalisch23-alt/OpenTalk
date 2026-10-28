// SPDX-License-Identifier: GPL-3.0-only
// opentalk-audio-bench: measurements behind docs/audio/benchmarks.md.
//
//   opentalk-audio-bench codec            Opus CPU cost per 20 ms frame
//   opentalk-audio-bench jitter           jitter buffer delay and loss recovery (simulated network)
//   opentalk-audio-bench network          latency through a real server on this computer
//   opentalk-audio-bench router           server CPU cost to forward one packet
//   opentalk-audio-bench device-loopback  round trip through real sound hardware (needs a
//                                         cable or microphone near the speaker)
//   opentalk-audio-bench all              everything except device-loopback
//
// Results are printed as Markdown tables.

#include "opentalk/audio/AudioBackend.h"
#include "opentalk/audio/AudioFormat.h"
#include "opentalk/audio/JitterBuffer.h"
#include "opentalk/audio/OpusCodec.h"
#include "opentalk/audio/RingBuffer.h"
#include "opentalk/client/Connection.h"
#include "opentalk/protocol/VoicePacket.h"
#include "opentalk/security/CertificateGenerator.h"
#include "opentalk/security/PacketCipher.h"
#include "opentalk/security/PasswordHash.h"
#include "opentalk/security/Tls.h"
#include "opentalk/server/Server.h"
#include "opentalk/server/VoiceRouter.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QRandomGenerator>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtEndian>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

using namespace opentalk;
using audio::kFrameSamples;

namespace {

void out(const QString &line)
{
    std::fputs(qPrintable(line + QLatin1Char('\n')), stdout);
    std::fflush(stdout);
}

/// A speech-like test signal: a gliding voice fundamental with harmonics shaped by two
/// formants, syllables at about 4 per second, and a little noise. Deterministic.
std::vector<float> speechLike(int frames, quint32 seed = 1)
{
    QRandomGenerator rng(seed);
    std::vector<float> signal(static_cast<std::size_t>(frames) * kFrameSamples);
    double phase = 0.0;
    for (std::size_t i = 0; i < signal.size(); ++i) {
        const double t = static_cast<double>(i) / audio::kSampleRate;
        const double f0 = 150.0 + 40.0 * std::sin(2.0 * std::numbers::pi * 0.7 * t);
        phase += 2.0 * std::numbers::pi * f0 / audio::kSampleRate;
        double v = 0.0;
        for (int h = 1; h <= 20; ++h) {
            const double f = f0 * h;
            const double formants =
                std::exp(-std::pow((f - 700.0) / 300.0, 2)) + 0.6 * std::exp(-std::pow((f - 1800.0) / 500.0, 2)) + 0.05;
            v += formants * std::sin(phase * h) / h;
        }
        const double syllable = std::pow(std::max(0.0, std::sin(2.0 * std::numbers::pi * 4.0 * t)), 0.5);
        const double noise = (rng.generateDouble() - 0.5) * 0.01;
        signal[i] = static_cast<float>(0.4 * syllable * v + noise);
    }
    return signal;
}

double percentile(std::vector<double> values, double p)
{
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(std::clamp(p, 0.0, 1.0) * static_cast<double>(values.size() - 1));
    return values[index];
}

QString fixed(double value, int decimals = 1)
{
    return QString::number(value, 'f', decimals);
}

// ------------------------------------------------------------------------- codec

void benchCodec()
{
    out(QStringLiteral("## Opus cost per 20 ms frame (one CPU core)\n"));
    out(QStringLiteral(
        "| Bit rate | Encode (µs) | Decode (µs) | Concealment (µs) | Share of one core for one stream |"));
    out(QStringLiteral("|---|---|---|---|---|"));
    const int frames = 2500; // 50 seconds of audio
    const std::vector<float> signal = speechLike(frames);
    for (int kbps : {16, 32, 64}) {
        audio::OpusEncoderWrapper encoder(audio::OpusEncoderWrapper::Content::Voice, kbps * 1000);
        audio::OpusDecoderWrapper decoder;
        std::vector<QByteArray> packets;
        packets.reserve(frames);
        QElapsedTimer timer;
        timer.start();
        for (int f = 0; f < frames; ++f) {
            packets.push_back(encoder.encode(
                std::span<const float>(signal.data() + static_cast<std::size_t>(f) * kFrameSamples, kFrameSamples)));
        }
        const double encodeUs = static_cast<double>(timer.nsecsElapsed()) / 1000.0 / frames;
        std::vector<float> pcm(kFrameSamples);
        timer.restart();
        for (const QByteArray &packet : packets) {
            decoder.decode(packet, pcm);
        }
        const double decodeUs = static_cast<double>(timer.nsecsElapsed()) / 1000.0 / frames;
        timer.restart();
        for (int f = 0; f < 500; ++f) {
            decoder.conceal(QByteArray(), pcm);
        }
        const double concealUs = static_cast<double>(timer.nsecsElapsed()) / 1000.0 / 500;
        const double share = (encodeUs + decodeUs) / (audio::kFrameMs * 1000.0) * 100.0;
        out(QStringLiteral("| %1 kbit/s | %2 | %3 | %4 | %5 % |")
                .arg(kbps)
                .arg(fixed(encodeUs))
                .arg(fixed(decodeUs))
                .arg(fixed(concealUs))
                .arg(fixed(share, 2)));
    }
    out(QString());
}

// ------------------------------------------------------------------------ jitter

struct JitterScenario {
    QString name;
    int jitterMs;     ///< Extra delay per packet, uniformly 0..jitterMs.
    double lossRate;  ///< Chance a packet is lost.
    double burstStay; ///< Chance that the packet after a lost one is lost too.
};

void benchJitter()
{
    out(QStringLiteral("## Jitter buffer and loss recovery (simulated network, 60 s of speech each)\n"));
    out(QStringLiteral("Network delay is 20 ms plus a random extra delay. \"Added delay\" is the time a packet waits "
                       "in the buffer before playing (median and 95th percentile). Frames are 20 ms.\n"));
    out(QStringLiteral("| Network | Added delay median / p95 (ms) | Played normally | Recovered by FEC | "
                       "Concealed | Late (dropped) |"));
    out(QStringLiteral("|---|---|---|---|---|---|"));
    const QList<JitterScenario> scenarios = {
        {QStringLiteral("perfect"), 0, 0.0, 0.0},           {QStringLiteral("10 ms jitter"), 10, 0.0, 0.0},
        {QStringLiteral("30 ms jitter"), 30, 0.0, 0.0},     {QStringLiteral("60 ms jitter"), 60, 0.0, 0.0},
        {QStringLiteral("2% random loss"), 10, 0.02, 0.0},  {QStringLiteral("5% random loss"), 10, 0.05, 0.0},
        {QStringLiteral("10% random loss"), 10, 0.10, 0.0}, {QStringLiteral("5% loss in bursts"), 10, 0.05, 0.5},
    };
    const int frames = 3000;
    const std::vector<float> signal = speechLike(frames, 7);
    audio::OpusEncoderWrapper encoder(audio::OpusEncoderWrapper::Content::Voice, 32000);
    std::vector<QByteArray> packets;
    for (int f = 0; f < frames; ++f) {
        packets.push_back(encoder.encode(
            std::span<const float>(signal.data() + static_cast<std::size_t>(f) * kFrameSamples, kFrameSamples)));
    }
    for (const JitterScenario &scenario : scenarios) {
        QRandomGenerator rng(42);
        struct Arrival {
            qint64 atMs;
            int frame;
        };
        std::vector<Arrival> arrivals;
        bool previousLost = false;
        for (int f = 0; f < frames; ++f) {
            const double lossChance = previousLost && scenario.burstStay > 0 ? scenario.burstStay : scenario.lossRate;
            previousLost = rng.generateDouble() < lossChance;
            if (previousLost) {
                continue;
            }
            const qint64 delay = 20 + (scenario.jitterMs > 0 ? rng.bounded(scenario.jitterMs + 1) : 0);
            arrivals.push_back({static_cast<qint64>(f) * audio::kFrameMs + delay, f});
        }
        std::sort(arrivals.begin(), arrivals.end(), [](const Arrival &a, const Arrival &b) { return a.atMs < b.atMs; });

        QHash<QByteArray, int> frameOf;
        for (int f = 0; f < frames; ++f) {
            frameOf.insert(packets[static_cast<std::size_t>(f)], f);
        }
        audio::JitterBuffer buffer;
        audio::OpusDecoderWrapper decoder;
        std::vector<float> pcm(kFrameSamples);
        std::vector<double> waits;
        std::vector<qint64> arrivalOf(static_cast<std::size_t>(frames), -1);
        int normal = 0;
        int recovered = 0;
        int concealed = 0;
        std::size_t next = 0;
        const qint64 endMs = static_cast<qint64>(frames) * audio::kFrameMs + 400;
        // The playback clock ticks every 20 ms, offset from the sender's clock.
        for (qint64 now = 7; now < endMs; now += audio::kFrameMs) {
            while (next < arrivals.size() && arrivals[next].atMs <= now) {
                const Arrival &a = arrivals[next++];
                arrivalOf[static_cast<std::size_t>(a.frame)] = a.atMs;
                buffer.insert(static_cast<quint16>(a.frame), packets[static_cast<std::size_t>(a.frame)], a.atMs);
            }
            QByteArray payload;
            QByteArray fec;
            switch (buffer.pop(&payload, &fec)) {
            case audio::JitterBuffer::Result::Packet: {
                decoder.decode(payload, pcm);
                ++normal;
                const int frame = frameOf.value(payload, -1);
                if (frame >= 0 && arrivalOf[static_cast<std::size_t>(frame)] >= 0) {
                    waits.push_back(static_cast<double>(now - arrivalOf[static_cast<std::size_t>(frame)]));
                }
                break;
            }
            case audio::JitterBuffer::Result::Lost:
                decoder.conceal(fec, pcm);
                (fec.isEmpty() ? concealed : recovered)++;
                break;
            case audio::JitterBuffer::Result::Empty:
                break;
            }
        }
        const int delivered = static_cast<int>(arrivals.size());
        const int late = std::max(0, delivered - normal);
        const auto pct = [total = static_cast<double>(frames)](int n) {
            return fixed(100.0 * n / total) + QStringLiteral(" %");
        };
        out(QStringLiteral("| %1 | %2 / %3 | %4 | %5 | %6 | %7 |")
                .arg(scenario.name, fixed(percentile(waits, 0.5), 0), fixed(percentile(waits, 0.95), 0), pct(normal),
                     pct(recovered), pct(concealed), pct(late)));
    }
    out(QString());
}

// ------------------------------------------------------------------------ router

void benchRouter()
{
    out(QStringLiteral("## Server cost to forward voice (one CPU core)\n"));
    out(QStringLiteral("Decrypting one incoming UDP packet and encrypting a copy for each listener, as the server "
                       "does. Listeners all use UDP.\n"));
    out(QStringLiteral("| Listeners in the channel | Time per incoming packet (µs) | Per copy sent (µs) | "
                       "Speakers one core could carry |"));
    out(QStringLiteral("|---|---|---|---|"));
    for (int listeners : {1, 10, 50}) {
        QList<quint32> recipients;
        for (int i = 0; i < listeners; ++i) {
            recipients.append(static_cast<quint32>(i + 2));
        }
        server::VoiceRouter::Config config;
        config.packetsPerSecond = 1e9; // measure the forwarding path, not the flood limit
        config.packetBurst = 1e9;
        server::VoiceRouter router(config, [recipients](quint32) { return recipients; }, [] { return qint64(0); });
        security::PacketSealer speaker;
        for (quint32 id = 1; id <= static_cast<quint32>(listeners + 1); ++id) {
            const protocol::AudioSetup setup = router.addSession(id, 10350);
            security::PacketSealer client(setup.clientToServerKey, security::PacketDirection::ClientToServer,
                                          setup.udpSessionId);
            router.handleDatagram(client.seal(static_cast<quint8>(protocol::VoicePacketKind::Ping),
                                              protocol::encodeVoicePing({1, protocol::kVoicePingReceivingUdp})),
                                  QHostAddress::LocalHost, static_cast<quint16>(20000 + id));
            if (id == 1) {
                speaker = client;
            }
        }
        const int count = 5000;
        std::vector<QByteArray> datagrams;
        datagrams.reserve(count);
        for (int i = 0; i < count; ++i) {
            datagrams.push_back(
                speaker.seal(static_cast<quint8>(protocol::VoicePacketKind::Audio),
                             protocol::encodeVoiceAudio(
                                 {protocol::AudioStream::Voice, static_cast<quint16>(i), 0, QByteArray(80, 'x')})));
        }
        quint64 sent = 0;
        QObject::connect(&router, &server::VoiceRouter::sendDatagram, &router, [&sent] { ++sent; });
        QElapsedTimer timer;
        timer.start();
        for (const QByteArray &datagram : datagrams) {
            router.handleDatagram(datagram, QHostAddress::LocalHost, 20001);
        }
        const double elapsedUs = static_cast<double>(timer.nsecsElapsed()) / 1000.0;
        const double perPacket = elapsedUs / count;
        const double perCopy = sent > 0 ? elapsedUs / static_cast<double>(sent) : 0.0;
        // Each speaker sends 50 packets per second.
        const double speakers = perPacket > 0 ? 1e6 / (perPacket * 50.0) : 0.0;
        out(QStringLiteral("| %1 | %2 | %3 | %4 |")
                .arg(listeners)
                .arg(fixed(perPacket, 1))
                .arg(fixed(perCopy, 2))
                .arg(fixed(speakers, 0)));
    }
    out(QString());
}

// ----------------------------------------------------------------------- network

bool waitFor(const std::function<bool()> &condition, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::usleep(200);
    }
    return true;
}

qint64 nowUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool benchNetwork()
{
    out(QStringLiteral("## Latency through a real server on this computer\n"));
    out(QStringLiteral("One client sends 80-byte packets every 20 ms to another in the same channel; each packet "
                       "carries its send time. Includes encryption, the server and decryption; excludes the "
                       "jitter buffer and sound devices.\n"));
    QString error;
    if (!security::initializeTls(&error)) {
        out(QStringLiteral("Cannot initialise TLS: %1").arg(error));
        return false;
    }
    QTemporaryDir dir;
    const QString certificate = dir.filePath(QStringLiteral("bench.crt"));
    const QString key = dir.filePath(QStringLiteral("bench.key"));
    if (!dir.isValid() || !security::generateSelfSignedCertificate({}, certificate, key, &error)) {
        out(QStringLiteral("Cannot create a certificate: %1").arg(error));
        return false;
    }
    const std::optional<QSslConfiguration> tls = security::serverTlsConfiguration(certificate, key, &error);
    if (!tls) {
        out(QStringLiteral("Cannot load the certificate: %1").arg(error));
        return false;
    }
    out(QStringLiteral("| Path | Median (ms) | 95th percentile (ms) | Maximum (ms) | Delivered |"));
    out(QStringLiteral("|---|---|---|---|---|"));
    for (const bool udp : {true, false}) {
        server::ServerConfig config;
        config.address = QHostAddress::LocalHost;
        config.port = 0;
        config.tls = *tls;
        config.allowGuests = true;
        config.passwordCost = security::PasswordHashCost::minimumForTests();
        server::Server server(config);
        if (!server.start(&error)) {
            out(QStringLiteral("Cannot start a server: %1").arg(error));
            return false;
        }
        client::Connection a;
        client::Connection b;
        for (client::Connection *c : {&a, &b}) {
            client::ConnectOptions o;
            o.host = QStringLiteral("127.0.0.1");
            o.port = server.serverPort();
            o.trust = security::ServerTrust::pinned(server.certificateFingerprint());
            o.credentials.name = c == &a ? QStringLiteral("Sender") : QStringLiteral("Listener");
            o.autoReconnect = false;
            o.allowUdp = udp;
            c->connectToServer(o);
        }
        if (!waitFor(
                [&] {
                    return a.isVoiceReady() && b.isVoiceReady() &&
                           (!udp || (a.isVoiceUsingUdp() && b.isVoiceUsingUdp()));
                },
                10000)) {
            out(QStringLiteral("| %1 | could not connect | | | |")
                    .arg(udp ? QStringLiteral("UDP") : QStringLiteral("TLS")));
            continue;
        }
        std::vector<double> latencies;
        QObject::connect(&b, &client::Connection::audioReceived, &b, [&](const protocol::AudioData &audio) {
            if (audio.payload.size() >= 8) {
                const qint64 sent = qFromBigEndian<qint64>(reinterpret_cast<const uchar *>(audio.payload.constData()));
                latencies.push_back(static_cast<double>(nowUs() - sent) / 1000.0);
            }
        });
        const int count = 500;
        QElapsedTimer pace;
        pace.start();
        for (int i = 0; i < count; ++i) {
            QByteArray payload(80, '\0');
            qToBigEndian<qint64>(nowUs(), reinterpret_cast<uchar *>(payload.data()));
            a.sendAudio(protocol::AudioStream::Voice, static_cast<quint16>(i), payload);
            waitFor([&] { return pace.elapsed() >= (i + 1) * 20; }, 1000);
        }
        waitFor([&] { return latencies.size() >= static_cast<std::size_t>(count); }, 2000);
        out(QStringLiteral("| %1 | %2 | %3 | %4 | %5 of %6 |")
                .arg(udp ? QStringLiteral("Encrypted UDP") : QStringLiteral("TLS connection (fallback)"))
                .arg(fixed(percentile(latencies, 0.5), 2))
                .arg(fixed(percentile(latencies, 0.95), 2))
                .arg(fixed(percentile(latencies, 1.0), 2))
                .arg(latencies.size())
                .arg(count));
        a.disconnectFromServer();
        b.disconnectFromServer();
        server.stop();
        waitFor([&] { return !server.isListening(); }, 3000);
    }
    out(QString());
    return true;
}

// ---------------------------------------------------------------- device loopback

int benchDeviceLoopback(const QString &inputId, const QString &outputId)
{
    out(QStringLiteral("## Round trip through the sound hardware\n"));
    std::unique_ptr<audio::AudioBackend> backend = audio::createSystemAudioBackend();
    audio::RingBuffer capture(audio::kSampleRate);
    audio::RingBuffer playback(audio::kSampleRate);
    QString error;
    if (!backend->start(inputId, outputId, capture, playback, &error)) {
        out(QStringLiteral("Cannot open the sound devices: %1").arg(error));
        return 1;
    }
    out(QStringLiteral("Backend: %1. Playing 10 short clicks; each must be heard by the microphone (use a cable from "
                       "the output to the input, or hold the microphone near the speaker in a quiet room).\n")
            .arg(backend->name()));
    std::vector<double> results;
    std::vector<float> chunk(audio::kSampleRate / 1000); // 1 ms
    std::vector<float> click(240);
    for (std::size_t i = 0; i < click.size(); ++i) {
        click[i] =
            static_cast<float>(0.8 * std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(i) / 48000.0));
    }
    for (int trial = 0; trial < 10; ++trial) {
        // Let the room go quiet, keeping only a few ms queued so the queue adds little.
        QElapsedTimer quiet;
        quiet.start();
        while (quiet.elapsed() < 700) {
            if (playback.available() < 480) {
                std::fill(chunk.begin(), chunk.end(), 0.0f);
                playback.write(chunk.data(), chunk.size());
            }
            std::vector<float> discard(capture.available());
            capture.read(discard.data(), discard.size());
            QThread::usleep(500);
        }
        while (playback.available() > 0) {
            QThread::usleep(200); // the device has taken everything queued
        }
        playback.write(click.data(), click.size());
        QElapsedTimer timer;
        timer.start();
        qint64 emittedUs = -1;
        bool heard = false;
        while (timer.elapsed() < 1000 && !heard) {
            if (emittedUs < 0 && playback.available() < click.size()) {
                emittedUs = timer.nsecsElapsed() / 1000; // the device started taking the click
            }
            std::vector<float> got(capture.available());
            capture.read(got.data(), got.size());
            for (float sample : got) {
                if (std::abs(sample) > 0.1f && emittedUs >= 0) {
                    heard = true;
                    results.push_back(static_cast<double>(timer.nsecsElapsed() / 1000 - emittedUs) / 1000.0);
                    break;
                }
            }
            if (playback.available() < 480) {
                std::fill(chunk.begin(), chunk.end(), 0.0f);
                playback.write(chunk.data(), chunk.size());
            }
            QThread::usleep(250);
        }
        out(heard ? QStringLiteral("Click %1: %2 ms").arg(trial + 1).arg(fixed(results.back(), 1))
                  : QStringLiteral("Click %1: not heard").arg(trial + 1));
    }
    backend->stop();
    if (results.empty()) {
        out(QStringLiteral("\nNo click was heard. Check the cable or volume and try again."));
        return 1;
    }
    out(QStringLiteral("\n| Measurement | Value |\n|---|---|\n| Clicks heard | %1 of 10 |\n"
                       "| Round trip, median | %2 ms |\n| Round trip, fastest | %3 ms |\n")
            .arg(results.size())
            .arg(fixed(percentile(results, 0.5)))
            .arg(fixed(percentile(results, 0.0))));
    out(QStringLiteral("The round trip includes the output and input device buffers, about 1 ms of measurement "
                       "polling, and the sound travelling through the air or cable."));
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("opentalk-audio-bench"));
    QCoreApplication::setApplicationVersion(QStringLiteral(OPENTALK_VERSION_STRING));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Measures OpenTalk's audio path."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("test"),
                                 QStringLiteral("codec, jitter, router, network, device-loopback or all"));
    const QCommandLineOption inputOption(QStringLiteral("input"), QStringLiteral("Input device name."),
                                         QStringLiteral("name"));
    const QCommandLineOption outputOption(QStringLiteral("output"), QStringLiteral("Output device name."),
                                          QStringLiteral("name"));
    parser.addOptions({inputOption, outputOption});
    parser.process(app);
    const QString test = parser.positionalArguments().value(0, QStringLiteral("all"));

    out(QStringLiteral("# OpenTalk %1 audio benchmark\n").arg(QCoreApplication::applicationVersion()));
    out(QStringLiteral("Machine: %1, %2, %3 logical CPUs. Build: %4.\n")
            .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture())
            .arg(QThread::idealThreadCount())
            .arg(QSysInfo::buildAbi()));
    if (test == QLatin1String("device-loopback")) {
        return benchDeviceLoopback(parser.value(inputOption), parser.value(outputOption));
    }
    bool ok = true;
    if (test == QLatin1String("codec") || test == QLatin1String("all")) {
        benchCodec();
    }
    if (test == QLatin1String("jitter") || test == QLatin1String("all")) {
        benchJitter();
    }
    if (test == QLatin1String("router") || test == QLatin1String("all")) {
        benchRouter();
    }
    if (test == QLatin1String("network") || test == QLatin1String("all")) {
        ok = benchNetwork() && ok;
    }
    return ok ? 0 : 1;
}
