// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/audio/Sounds.h"

#include "opentalk/audio/AudioFormat.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <numbers>

namespace opentalk::audio {
namespace {

struct Note {
    double frequency; ///< Hz; 0 for a pause.
    int durationMs;
};

/// Soft sine notes with short fades and a little second harmonic, at a gentle level.
QVector<float> render(std::initializer_list<Note> notes, double level = 0.25)
{
    QVector<float> out;
    for (const Note &note : notes) {
        const int samples = kSampleRate * note.durationMs / 1000;
        const int fade = std::min(samples / 4, kSampleRate * 8 / 1000);
        for (int i = 0; i < samples; ++i) {
            double value = 0.0;
            if (note.frequency > 0) {
                const double t = static_cast<double>(i) / kSampleRate;
                value = std::sin(2.0 * std::numbers::pi * note.frequency * t) +
                        0.25 * std::sin(4.0 * std::numbers::pi * note.frequency * t);
                double envelope = 1.0;
                if (i < fade) {
                    envelope = static_cast<double>(i) / fade;
                } else if (i > samples - fade) {
                    envelope = static_cast<double>(samples - i) / fade;
                }
                value *= envelope * level / 1.25;
            }
            out.append(static_cast<float>(value));
        }
    }
    return out;
}

} // namespace

QString soundFileName(EventSound sound)
{
    switch (sound) {
    case EventSound::Connected:
        return QStringLiteral("connected");
    case EventSound::Disconnected:
        return QStringLiteral("disconnected");
    case EventSound::UserJoinedServer:
        return QStringLiteral("user-joined-server");
    case EventSound::UserLeftServer:
        return QStringLiteral("user-left-server");
    case EventSound::UserJoinedChannel:
        return QStringLiteral("user-joined-channel");
    case EventSound::UserLeftChannel:
        return QStringLiteral("user-left-channel");
    case EventSound::ChannelMessage:
        return QStringLiteral("channel-message");
    case EventSound::PrivateMessage:
        return QStringLiteral("private-message");
    case EventSound::ServerMessage:
        return QStringLiteral("server-message");
    case EventSound::TransmitOn:
        return QStringLiteral("transmit-on");
    case EventSound::TransmitOff:
        return QStringLiteral("transmit-off");
    case EventSound::Error:
        return QStringLiteral("error");
    case EventSound::Count:
        break;
    }
    return QString();
}

QStringList allSoundFileNames()
{
    QStringList names;
    for (int i = 0; i < static_cast<int>(EventSound::Count); ++i) {
        names << soundFileName(static_cast<EventSound>(i));
    }
    return names;
}

QVector<float> synthesizeSound(EventSound sound)
{
    // Rising patterns mean "arrived / started", falling ones "left / stopped".
    switch (sound) {
    case EventSound::Connected:
        return render({{523.25, 70}, {659.25, 70}, {783.99, 120}});
    case EventSound::Disconnected:
        return render({{783.99, 70}, {659.25, 70}, {523.25, 120}});
    case EventSound::UserJoinedServer:
        return render({{587.33, 60}, {880.0, 90}}, 0.18);
    case EventSound::UserLeftServer:
        return render({{880.0, 60}, {587.33, 90}}, 0.18);
    case EventSound::UserJoinedChannel:
        return render({{659.25, 50}, {987.77, 80}}, 0.2);
    case EventSound::UserLeftChannel:
        return render({{987.77, 50}, {659.25, 80}}, 0.2);
    case EventSound::ChannelMessage:
        return render({{1046.5, 90}}, 0.18);
    case EventSound::PrivateMessage:
        return render({{1046.5, 70}, {0, 40}, {1318.5, 90}}, 0.2);
    case EventSound::ServerMessage:
        return render({{784.0, 80}, {1046.5, 80}, {784.0, 80}}, 0.22);
    case EventSound::TransmitOn:
        return render({{1500.0, 25}}, 0.12);
    case EventSound::TransmitOff:
        return render({{1000.0, 25}}, 0.12);
    case EventSound::Error:
        return render({{220.0, 120}, {0, 30}, {196.0, 160}}, 0.22);
    case EventSound::Count:
        break;
    }
    return {};
}

SoundSet builtInSounds()
{
    SoundSet set;
    for (int i = 0; i < static_cast<int>(EventSound::Count); ++i) {
        set[static_cast<std::size_t>(i)] = synthesizeSound(static_cast<EventSound>(i));
    }
    return set;
}

} // namespace opentalk::audio
