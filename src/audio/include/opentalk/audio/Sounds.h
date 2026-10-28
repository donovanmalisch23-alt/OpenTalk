// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <array>

namespace opentalk::audio {

/// Events that can play a sound. Every one also has a text equivalent in the app
/// (history line, status or announcement), so sound is never the only signal.
enum class EventSound {
    Connected,
    Disconnected,
    UserJoinedServer,
    UserLeftServer,
    UserJoinedChannel,
    UserLeftChannel,
    ChannelMessage,
    PrivateMessage,
    ServerMessage,
    TransmitOn,
    TransmitOff,
    Error,
    Count
};

/// File name (without extension) for each event in a soundpack folder, for example
/// "user-joined-server.wav".
QString soundFileName(EventSound sound);
QStringList allSoundFileNames();

/// OpenTalk's own built-in sounds, generated as short tones when the program starts,
/// so no third-party recordings are needed. 48 kHz mono.
QVector<float> synthesizeSound(EventSound sound);

/// A complete set of event sounds; missing entries are empty (silent).
using SoundSet = std::array<QVector<float>, static_cast<std::size_t>(EventSound::Count)>;
SoundSet builtInSounds();

} // namespace opentalk::audio
