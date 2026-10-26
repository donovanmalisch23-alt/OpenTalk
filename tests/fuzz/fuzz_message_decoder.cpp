// SPDX-License-Identifier: GPL-3.0-only
// Decodes arbitrary payloads as every message type. Any message that decodes must
// re-encode and decode to an equal message.

#include "opentalk/protocol/Frame.h"
#include "opentalk/protocol/Messages.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>

using namespace opentalk::protocol;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1) {
        return 0;
    }
    const quint16 type = static_cast<quint16>(data[0] % 8u); // 0..7 covers known and unknown types
    const QByteArrayView payload(reinterpret_cast<const char *>(data + 1), static_cast<qsizetype>(size - 1));
    const MessageDecodeResult result = decodeMessage(type, payload);
    if (const auto *message = std::get_if<Message>(&result)) {
        const QByteArray frame = encodeMessage(*message);
        const MessageDecodeResult again = decodeMessage(type, QByteArrayView(frame).sliced(kFrameHeaderSize));
        const auto *second = std::get_if<Message>(&again);
        if (second == nullptr || !(*second == *message)) {
            std::abort();
        }
    }
    return 0;
}
