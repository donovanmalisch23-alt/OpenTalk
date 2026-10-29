// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/audio/MediaFile.h"

#include "opentalk/audio/AudioFormat.h"

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <string>

#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_GENERATION
#include "miniaudio.h"

namespace opentalk::audio {

struct MediaFile::Impl {
    ma_decoder decoder{};
    bool open = false;
    QByteArray data; ///< For openData(); must outlive the decoder.
};

MediaFile::MediaFile()
    : d(std::make_unique<Impl>())
{
}

MediaFile::~MediaFile()
{
    close();
}

QString MediaFile::fileFilter()
{
    return QStringLiteral("Audio files (*.wav *.flac *.mp3);;All files (*)");
}

bool MediaFile::open(const QString &path, QString *errorMessage)
{
    close();
    auto fail = [errorMessage](const QString &message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    if (!QFileInfo(path).isFile()) {
        return fail(QStringLiteral("the file does not exist"));
    }
    // miniaudio converts to OpenTalk's format (resampling and mixing to mono) as it reads.
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, kSampleRate);
#ifdef Q_OS_WIN
    const std::wstring wide = path.toStdWString();
    const ma_result opened = ma_decoder_init_file_w(wide.c_str(), &config, &d->decoder);
#else
    const QByteArray nativePath = QFile::encodeName(path);
    const ma_result opened = ma_decoder_init_file(nativePath.constData(), &config, &d->decoder);
#endif
    if (opened != MA_SUCCESS) {
        return fail(QStringLiteral("it is not a WAV, FLAC or MP3 file OpenTalk can read"));
    }
    d->open = true;
    m_path = path;
    m_title = QFileInfo(path).fileName();
    m_positionSamples = 0;
    ma_uint64 frames = 0;
    m_durationMs = ma_decoder_get_length_in_pcm_frames(&d->decoder, &frames) == MA_SUCCESS && frames > 0
                       ? static_cast<qint64>(frames * 1000 / kSampleRate)
                       : -1;
    return true;
}

bool MediaFile::openData(const QByteArray &data, const QString &title, QString *errorMessage)
{
    close();
    d->data = data;
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, kSampleRate);
    if (ma_decoder_init_memory(d->data.constData(), static_cast<size_t>(d->data.size()), &config, &d->decoder) !=
        MA_SUCCESS) {
        d->data.clear();
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("it is not a WAV, FLAC or MP3 file OpenTalk can read");
        }
        return false;
    }
    d->open = true;
    m_path.clear();
    m_title = title;
    m_positionSamples = 0;
    ma_uint64 frames = 0;
    m_durationMs = ma_decoder_get_length_in_pcm_frames(&d->decoder, &frames) == MA_SUCCESS && frames > 0
                       ? static_cast<qint64>(frames * 1000 / kSampleRate)
                       : -1;
    return true;
}

void MediaFile::close()
{
    if (d->open) {
        ma_decoder_uninit(&d->decoder);
        d->open = false;
    }
    d->data.clear();
    m_durationMs = -1;
    m_positionSamples = 0;
}

bool MediaFile::isOpen() const
{
    return d->open;
}

std::size_t MediaFile::read(std::span<float> out)
{
    if (!d->open || out.empty()) {
        return 0;
    }
    std::size_t total = 0;
    // A decoder can return less than asked before the end (for example at an MP3 frame
    // boundary), so keep reading until it gives nothing.
    while (total < out.size()) {
        ma_uint64 read = 0;
        const ma_result result = ma_decoder_read_pcm_frames(&d->decoder, out.data() + total, out.size() - total, &read);
        total += static_cast<std::size_t>(read);
        if (read == 0 || (result != MA_SUCCESS && result != MA_AT_END)) {
            break;
        }
    }
    m_positionSamples += static_cast<qint64>(total);
    return total;
}

qint64 MediaFile::positionMs() const
{
    return m_positionSamples * 1000 / kSampleRate;
}

bool MediaFile::seekMs(qint64 ms)
{
    if (!d->open) {
        return false;
    }
    ms = std::max<qint64>(0, ms);
    if (m_durationMs >= 0) {
        ms = std::min(ms, m_durationMs);
    }
    const auto frame = static_cast<ma_uint64>(ms * kSampleRate / 1000);
    if (ma_decoder_seek_to_pcm_frame(&d->decoder, frame) != MA_SUCCESS) {
        return false;
    }
    m_positionSamples = static_cast<qint64>(frame);
    return true;
}

} // namespace opentalk::audio
