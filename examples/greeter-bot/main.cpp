// SPDX-License-Identifier: GPL-3.0-only
// opentalk-greeter-bot: an example bot built with the OpenTalk SDK. It logs in to an
// account (ideally one created with --bot), welcomes people who arrive in its channel
// and answers a few commands typed in the channel:
//
//   !help          what it can do
//   !time          the server's time
//   !roll [sides]  rolls a dice (6 sides unless you say otherwise)
//   !who           who is in the channel
//   !beep          plays a short tone, to show how a bot can talk
//
// With --media-folder it is also a small music player for the channel:
//
//   !files         the WAV, FLAC and MP3 files it can play
//   !play <name>   plays one of them into the channel (labelled as a file, not speech)
//   !stop          stops playing
//
// Run it with the account's password on standard input:
//
//   opentalk-greeter-bot example.org --user greeter --fingerprint SHA256:AB:... < password.txt
//
// It never answers other bots, so two bots cannot keep each other talking.

#include <opentalk/sdk/Client.h>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QTextStream>
#include <QTimer>

#include <cmath>
#include <iostream>
#include <numbers>
#include <string>

using namespace opentalk::sdk;

namespace {

class GreeterBot : public QObject {
public:
    GreeterBot(Client &client, QString channelName, QString mediaFolder)
        : m_client(client)
        , m_channelName(std::move(channelName))
        , m_mediaFolder(std::move(mediaFolder))
    {
        connect(&client, &Client::connected, this, [this](const ServerInfo &server) {
            log(QStringLiteral("Connected to %1 as %2.").arg(server.name, m_client.self().name));
            joinConfiguredChannel();
        });
        connect(&client, &Client::userJoined, this, [this](const User &user) { maybeGreet(user); });
        connect(&client, &Client::userMoved, this, [this](const User &user) { maybeGreet(user); });
        connect(&client, &Client::textReceived, this, &GreeterBot::onText);
        m_toneTimer.setInterval(20);
        connect(&m_toneTimer, &QTimer::timeout, this, &GreeterBot::sendToneFrame);
        m_playTimer.setTimerType(Qt::PreciseTimer);
        m_playTimer.setInterval(AudioEncoder::kFrameSamples * 1000 / AudioEncoder::kSampleRate); // 20 ms
        connect(&m_playTimer, &QTimer::timeout, this, &GreeterBot::sendFileFrame);
    }

private:
    static void log(const QString &text) { std::cout << text.toStdString() << std::endl; }

    void joinConfiguredChannel()
    {
        if (m_channelName.isEmpty()) {
            return;
        }
        for (const Channel &channel : m_client.channels()) {
            if (channel.name.compare(m_channelName, Qt::CaseInsensitive) == 0) {
                m_client.joinChannel(channel.id);
                return;
            }
        }
        log(QStringLiteral("There is no channel named %1; staying where I am.").arg(m_channelName));
    }

    void maybeGreet(const User &user)
    {
        const User self = m_client.self();
        if (user.id == self.id || user.bot || user.channelId != self.channelId) {
            return;
        }
        m_client.sendChannelMessage(QStringLiteral("Welcome, %1! Type !help to see what I can do.").arg(user.name));
    }

    void onText(const TextMessage &message)
    {
        const User self = m_client.self();
        if (message.kind != TextMessage::Kind::Channel || message.fromUserId == self.id ||
            message.channelId != self.channelId) {
            return;
        }
        const QList<User> users = m_client.users();
        const auto sender =
            std::find_if(users.cbegin(), users.cend(), [&](const User &u) { return u.id == message.fromUserId; });
        if (sender == users.cend() || sender->bot) {
            return;
        }
        const QStringList words = message.text.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (words.isEmpty()) {
            return;
        }
        const QString command = words.first().toLower();
        if (command == QLatin1String("!help")) {
            m_client.sendChannelMessage(m_mediaFolder.isEmpty()
                                            ? QStringLiteral("I know: !help, !time, !roll [sides], !who and !beep.")
                                            : QStringLiteral("I know: !help, !time, !roll [sides], !who, !beep, "
                                                             "!files, !play <name> and !stop."));
        } else if (command == QLatin1String("!time")) {
            m_client.sendChannelMessage(
                QStringLiteral("It is %1 UTC on the server.").arg(message.time.toString(QStringLiteral("HH:mm"))));
        } else if (command == QLatin1String("!roll")) {
            int sides = 6;
            if (words.size() > 1) {
                bool ok = false;
                sides = words.at(1).toInt(&ok);
                if (!ok || sides < 2 || sides > 1000) {
                    m_client.sendChannelMessage(QStringLiteral("A dice needs 2 to 1000 sides."));
                    return;
                }
            }
            const int roll = QRandomGenerator::global()->bounded(1, sides + 1);
            m_client.sendChannelMessage(
                QStringLiteral("%1 rolled %2 (1 to %3).").arg(sender->name).arg(roll).arg(sides));
        } else if (command == QLatin1String("!who")) {
            QStringList names;
            for (const User &u : users) {
                if (u.channelId == self.channelId) {
                    names << (u.bot ? u.name + QStringLiteral(" (bot)") : u.name);
                }
            }
            m_client.sendChannelMessage(QStringLiteral("Here: %1.").arg(names.join(QStringLiteral(", "))));
        } else if (command == QLatin1String("!beep")) {
            if (!m_client.isAudioReady()) {
                m_client.sendChannelMessage(QStringLiteral("I cannot talk on this server."));
                return;
            }
            m_toneFrames = 50; // one second
            m_phase = 0;
            m_toneTimer.start();
        } else if (command == QLatin1String("!files") && !m_mediaFolder.isEmpty()) {
            const QStringList files = playableFiles();
            m_client.sendChannelMessage(
                files.isEmpty() ? QStringLiteral("I have nothing to play.")
                                : QStringLiteral("I can play: %1.").arg(files.mid(0, 20).join(QStringLiteral(", "))));
        } else if (command == QLatin1String("!play") && !m_mediaFolder.isEmpty()) {
            play(message.text.trimmed().mid(command.size()).trimmed());
        } else if (command == QLatin1String("!stop") && !m_mediaFolder.isEmpty()) {
            if (m_file.isOpen()) {
                m_playTimer.stop();
                m_file.close();
                m_client.sendChannelMessage(QStringLiteral("Stopped."));
            }
        }
    }

    QStringList playableFiles() const
    {
        return QDir(m_mediaFolder)
            .entryList({QStringLiteral("*.wav"), QStringLiteral("*.flac"), QStringLiteral("*.mp3")}, QDir::Files,
                       QDir::Name | QDir::IgnoreCase);
    }

    void play(const QString &name)
    {
        // Only a plain file name from the folder: never a path someone typed.
        if (!playableFiles().contains(name)) {
            m_client.sendChannelMessage(
                QStringLiteral("I have no file called \"%1\". Type !files to see what I have.").arg(name.left(100)));
            return;
        }
        if (!m_client.isAudioReady()) {
            m_client.sendChannelMessage(QStringLiteral("I cannot play anything on this server."));
            return;
        }
        QString error;
        if (!m_file.open(QDir(m_mediaFolder).filePath(name), &error)) {
            m_client.sendChannelMessage(QStringLiteral("I cannot play %1: %2.").arg(name, error));
            return;
        }
        m_playing = name;
        const qint64 seconds = m_file.durationMs() / 1000;
        m_client.sendChannelMessage(m_file.durationMs() >= 0 ? QStringLiteral("Playing %1 (%2:%3).")
                                                                   .arg(name)
                                                                   .arg(seconds / 60)
                                                                   .arg(seconds % 60, 2, 10, QLatin1Char('0'))
                                                             : QStringLiteral("Playing %1.").arg(name));
        m_playTimer.start();
    }

    /// One 20 ms frame of the file, sent as media so listeners know it is not speech.
    /// A real player would pace itself by the clock rather than a timer, which drifts.
    void sendFileFrame()
    {
        const QList<float> frame = m_file.readFrame();
        if (frame.isEmpty()) {
            m_playTimer.stop();
            m_file.close();
            m_client.sendChannelMessage(QStringLiteral("Finished %1.").arg(m_playing));
            return;
        }
        m_client.sendAudio(m_musicEncoder.encode(frame), AudioStream::Media);
    }

    /// One 20 ms frame of a quiet 440 Hz tone; a real bot would send speech or music.
    void sendToneFrame()
    {
        if (m_toneFrames-- <= 0) {
            m_toneTimer.stop();
            return;
        }
        QList<float> frame(AudioEncoder::kFrameSamples);
        for (float &sample : frame) {
            sample = 0.2f * static_cast<float>(std::sin(m_phase));
            m_phase += 2.0 * std::numbers::pi * 440.0 / AudioEncoder::kSampleRate;
        }
        m_client.sendAudio(m_encoder.encode(frame));
    }

    Client &m_client;
    QString m_channelName;
    QString m_mediaFolder;
    AudioEncoder m_encoder{AudioEncoder::Content::Music, 48000};
    AudioEncoder m_musicEncoder{AudioEncoder::Content::Music, 64000};
    AudioFileReader m_file;
    QTimer m_playTimer;
    QString m_playing;
    QTimer m_toneTimer;
    int m_toneFrames = 0;
    double m_phase = 0;
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("opentalk-greeter-bot"));
    QTextStream err(stderr);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("An example OpenTalk bot that greets people and answers "
                                                    "simple commands."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("host"), QStringLiteral("Server host name or IP address."));
    const QCommandLineOption portOption(QStringLiteral("port"), QStringLiteral("Server port (default 10350)."),
                                        QStringLiteral("port"), QStringLiteral("10350"));
    const QCommandLineOption fingerprintOption(QStringLiteral("fingerprint"),
                                               QStringLiteral("Expected SHA-256 certificate fingerprint."),
                                               QStringLiteral("fingerprint"));
    const QCommandLineOption userOption(QStringLiteral("user"),
                                        QStringLiteral("The bot's account; its password is read from the first "
                                                       "line of standard input."),
                                        QStringLiteral("username"));
    const QCommandLineOption channelOption(
        QStringLiteral("channel"), QStringLiteral("Join this channel after logging in."), QStringLiteral("name"));
    const QCommandLineOption mediaOption(QStringLiteral("media-folder"),
                                         QStringLiteral("Offer the WAV, FLAC and MP3 files in this folder with !play."),
                                         QStringLiteral("folder"));
    const QCommandLineOption verboseOption(QStringLiteral("verbose"), QStringLiteral("Show diagnostic messages."));
    parser.addOptions({portOption, fingerprintOption, userOption, channelOption, mediaOption, verboseOption});
    parser.process(app);
    if (!parser.isSet(verboseOption)) {
        QLoggingCategory::setFilterRules(QStringLiteral("opentalk.*=false"));
    }
    if (parser.positionalArguments().size() != 1 || !parser.isSet(userOption)) {
        parser.showHelp(2);
    }
    bool portOk = false;
    const uint port = parser.value(portOption).toUInt(&portOk);
    if (!portOk || port == 0 || port > 65535) {
        err << "--port must be between 1 and 65535\n";
        return 2;
    }
    std::string password;
    std::getline(std::cin, password);
    if (!password.empty() && password.back() == '\r') {
        password.pop_back();
    }

    QString error;
    if (!Client::initialize(&error)) {
        err << error << '\n';
        return 1;
    }
    Client client;
    client.setApplication(QStringLiteral("opentalk-greeter-bot"), QStringLiteral("1.0"));
    const QString mediaFolder = parser.value(mediaOption);
    if (parser.isSet(mediaOption) && !QFileInfo(mediaFolder).isDir()) {
        err << "--media-folder must be a folder\n";
        return 2;
    }
    GreeterBot bot(client, parser.value(channelOption), mediaFolder);
    QObject::connect(&client, &Client::disconnected, &app, [&err](const DisconnectReason &reason) {
        err << "Disconnected: " << reason.message << '\n';
        err.flush();
        QCoreApplication::exit(1);
    });
    client.connectToServer({parser.positionalArguments().first(), static_cast<quint16>(port)},
                           {parser.value(fingerprintOption)},
                           {Login::Mode::Account, parser.value(userOption), QString::fromStdString(password)});
    return QCoreApplication::exec();
}
