// SPDX-License-Identifier: GPL-3.0-only
// Drives a ServerSession (input from a hostile client) or a ClientSession (input from a
// hostile server) with arbitrary bytes, chunk sizes and clock jumps. Checks that it
// never crashes, never sends after closing, and reports its closure exactly once.
// The seed corpus contains valid conversations so the fuzzer reaches every state.

#include "opentalk/protocol/Session.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>

using namespace opentalk::protocol;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1) {
        return 0;
    }
    const bool fuzzClient = (data[0] & 0x80u) != 0;
    const size_t chunk = static_cast<size_t>(data[0] % 64u) + 1u;
    data += 1;
    size -= 1;

    std::unique_ptr<Session> session;
    if (fuzzClient) {
        auto client = std::make_unique<ClientSession>(ClientSession::Config{});
        client->start(0);
        session = std::move(client);
    } else {
        ServerSession::Config config;
        config.serverName = QStringLiteral("fuzz");
        session = std::make_unique<ServerSession>(config, 0);
    }
    int closedCount = 0;
    bool sentAfterClose = false;
    QObject::connect(session.get(), &Session::closed, [&](const CloseInfo &) { ++closedCount; });
    QObject::connect(session.get(), &Session::sendRequested, [&](const QByteArray &) {
        if (closedCount > 0) {
            sentAfterClose = true;
        }
    });

    qint64 now = 0;
    for (size_t offset = 0; offset < size; offset += chunk) {
        const size_t n = (size - offset < chunk) ? size - offset : chunk;
        // The first byte of each chunk also advances the clock by 0-255 ms.
        now += data[offset];
        session->receive(QByteArrayView(reinterpret_cast<const char *>(data + offset), static_cast<qsizetype>(n)), now);
        session->tick(now);
    }
    session->tick(now + 3'600'000); // must time out cleanly whatever state it is in
    if (closedCount != 1 || sentAfterClose || session->state() != SessionState::Closed) {
        std::abort();
    }
    return 0;
}
