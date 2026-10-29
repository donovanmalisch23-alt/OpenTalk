// SPDX-License-Identifier: GPL-3.0-only
// Fuzzes reading audio files (miniaudio's WAV, FLAC and MP3 decoders, through MediaFile):
// files people stream into a channel, and soundpack files, may be crafted.

#include "opentalk/audio/AudioFormat.h"
#include "opentalk/audio/MediaFile.h"

#include <QByteArray>

#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    opentalk::audio::MediaFile file;
    if (!file.openData(QByteArray(reinterpret_cast<const char *>(data), static_cast<qsizetype>(size)),
                       QStringLiteral("fuzz"), nullptr)) {
        return 0;
    }
    std::vector<float> frame(opentalk::audio::kFrameSamples);
    // At most 10 seconds, so a file claiming to be hours long cannot stall the fuzzer.
    for (int i = 0; i < 500 && file.read(frame) == frame.size(); ++i) {
    }
    file.seekMs(file.durationMs() > 0 ? file.durationMs() / 2 : 1000);
    file.read(frame);
    return 0;
}
