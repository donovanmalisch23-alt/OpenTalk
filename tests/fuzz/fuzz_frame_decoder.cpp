// SPDX-License-Identifier: GPL-3.0-only
// Feeds arbitrary bytes to FrameDecoder in arbitrary chunk sizes and checks its
// invariants: bounded buffering, no frame above the limit, and sticky failure.

#include "opentalk/protocol/Frame.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>

using namespace opentalk::protocol;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2) {
        return 0;
    }
    // First byte picks a small payload limit so the size checks are exercised often.
    const quint32 limit = static_cast<quint32>(data[0]) * 16u;
    const size_t chunk = static_cast<size_t>(data[1] % 32u) + 1u;
    data += 2;
    size -= 2;

    FrameDecoder decoder(limit);
    QList<Frame> frames;
    bool failed = false;
    for (size_t offset = 0; offset < size; offset += chunk) {
        const size_t n = (size - offset < chunk) ? size - offset : chunk;
        const DecodeError error = decoder.feed(
            QByteArrayView(reinterpret_cast<const char *>(data + offset), static_cast<qsizetype>(n)), frames);
        if (failed && error == DecodeError::None) {
            std::abort(); // a failed decoder must never recover
        }
        failed = error != DecodeError::None;
        if (decoder.bufferedBytes() >
            kFrameHeaderSize + static_cast<qsizetype>(limit) + static_cast<qsizetype>(chunk)) {
            std::abort(); // buffering must stay bounded
        }
    }
    for (const Frame &frame : frames) {
        if (static_cast<quint64>(frame.payload.size()) > limit) {
            std::abort();
        }
        // Every decoded frame must re-encode to a valid frame of the same content.
        if (!encodeFrame(frame.type, frame.payload, limit)) {
            std::abort();
        }
    }
    return 0;
}
