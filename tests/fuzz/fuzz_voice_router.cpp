// SPDX-License-Identifier: GPL-3.0-only
// Feeds hostile UDP datagrams and tunnel audio into the server's VoiceRouter. Inputs are
// split into records; each is delivered raw (exercising header parsing and
// authentication) or first sealed with a real client key (so the fuzzer reaches the
// plaintext parsers and forwarding logic behind the encryption).

#include "opentalk/protocol/VoicePacket.h"
#include "opentalk/security/PacketCipher.h"
#include "opentalk/server/VoiceRouter.h"

#include <QCoreApplication>

#include <cstddef>
#include <cstdint>
#include <cstdlib>

using namespace opentalk;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static int argc = 1;
    static char name[] = "fuzz";
    static char *argv[] = {name, nullptr};
    static QCoreApplication app(argc, argv);

    qint64 now = 0;
    server::VoiceRouter router(
        {}, [](quint32 id, protocol::AudioStream) { return QList<quint32>{id == 1 ? 2u : 1u}; },
        [&now] { return now; });
    const protocol::AudioSetup setup = router.addSession(1, 10350);
    router.addSession(2, 10350);
    security::PacketSealer client(setup.clientToServerKey, security::PacketDirection::ClientToServer,
                                  setup.udpSessionId);
    QObject::connect(&router, &server::VoiceRouter::sendTunnel, [](quint32 id, const protocol::AudioData &audio) {
        if ((id != 1 && id != 2) || audio.userId == 0 || audio.userId == id || audio.payload.isEmpty() ||
            audio.payload.size() > protocol::kMaxAudioPayloadBytes) {
            std::abort();
        }
    });

    const QHostAddress from(QStringLiteral("192.0.2.7"));
    size_t offset = 0;
    while (offset + 2 <= size) {
        const uint8_t control = data[offset];
        const size_t length = std::min<size_t>(data[offset + 1] * 8u, size - offset - 2);
        const QByteArrayView record(reinterpret_cast<const char *>(data + offset + 2), static_cast<qsizetype>(length));
        offset += 2 + length;
        now += control & 0x0f;
        switch (control >> 6) {
        case 0:
            router.handleDatagram(record, from, 5000);
            break;
        case 1:
            router.handleDatagram(client.seal(static_cast<quint8>((control >> 4) & 0x03), record), from, 5000);
            break;
        case 2:
            if (const auto audio = protocol::decodeVoiceAudio(record)) {
                router.handleTunnel(1, *audio);
            }
            break;
        default:
            router.removeSession(2);
            break;
        }
    }
    return 0;
}
