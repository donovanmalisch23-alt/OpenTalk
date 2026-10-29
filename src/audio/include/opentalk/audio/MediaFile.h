// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <memory>
#include <span>

namespace opentalk::audio {

/// Reads a WAV, FLAC or MP3 file a little at a time as 48 kHz mono float, for
/// streaming into a channel. Unlike decodeAudioFile() it never holds the whole file in
/// memory, so long recordings are fine. Not thread-safe: use from one thread at a time.
class MediaFile {
public:
    MediaFile();
    ~MediaFile();
    MediaFile(const MediaFile &) = delete;
    MediaFile &operator=(const MediaFile &) = delete;

    /// The file types that can be opened, as a file-dialog filter.
    static QString fileFilter();

    bool open(const QString &path, QString *errorMessage);
    /// Reads from memory instead of a file (`data` is copied), for tests and fuzzing.
    bool openData(const QByteArray &data, const QString &title, QString *errorMessage);
    void close();
    bool isOpen() const;

    /// Fills `out` from the current position; returns how many samples were written
    /// (fewer than asked only at the end of the file).
    std::size_t read(std::span<float> out);
    /// Length in milliseconds, or -1 when the file does not say (some MP3 files).
    qint64 durationMs() const { return m_durationMs; }
    qint64 positionMs() const;
    bool seekMs(qint64 ms);
    /// What to call it: the file name without its folder.
    QString title() const { return m_title; }
    QString path() const { return m_path; }

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    QString m_path;
    QString m_title;
    qint64 m_durationMs = -1;
    qint64 m_positionSamples = 0;
};

} // namespace opentalk::audio
