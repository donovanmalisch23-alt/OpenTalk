// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/sdk/Version.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

#include <memory>

namespace opentalk::sdk {

struct ServerAddress {
    QString host;
    quint16 port = 10350;
};

/// How to recognise the genuine server.
struct ServerIdentity {
    /// SHA-256 fingerprint the server must present (from the server administrator or a
    /// previous connection). Leave empty to require a certificate from a public authority.
    QString pinnedFingerprint;
};

/// How to log in. Guests need the server to allow them.
struct Login {
    enum class Mode { Guest, Account };
    Mode mode = Mode::Guest;
    QString name; ///< Nickname (guest) or username (account).
    QString password;
};

struct ServerInfo {
    QString name;
    QString softwareVersion;
    int protocolVersion = 0;
    QString certificateFingerprint;
};

enum class ClientState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting, ///< The connection was lost; the SDK is trying again.
};

/// Built-in roles; `Custom` covers roles a server defines itself (see User::roleId).
enum class Role { Guest, User, Admin, Owner, Moderator, Restricted, Custom };

struct Channel {
    quint32 id = 0;
    quint32 parentId = 0; ///< 0 for the root channel.
    QString name;         ///< Empty for the root channel.
    QString topic;
    bool persistent = false;
    bool hasPassword = false;
    int maxUsers = 0; ///< 0 = unlimited.
};

struct User {
    quint32 id = 0; ///< Valid while the user stays connected.
    QString name;
    Role role = Role::Guest;
    quint32 channelId = 0;
    QString roleName; ///< The role's name on this server, for example "Moderator".
    bool bot = false; ///< The account is a bot (since SDK 0.3).
    bool voiceMuted = false;
    bool textMuted = false;
    QString status; ///< The user's status message, if any (since SDK 0.4).
};

/// Kinds of audio a client can send (since SDK 0.3).
enum class AudioStream {
    Voice,       ///< Speech: what push-to-talk sends.
    Media,       ///< Music or other media played into the channel.
    SystemAudio, ///< Sound the computer is playing.
};

/// Turns 48 kHz mono audio into the Opus packets that sendAudio() takes, 20 ms at a
/// time (since SDK 0.3).
class AudioEncoder {
public:
    static constexpr int kSampleRate = 48000;
    static constexpr int kFrameSamples = 960; ///< 20 ms

    enum class Content { Voice, Music };
    explicit AudioEncoder(Content content = Content::Voice, int bitrate = 32000);
    ~AudioEncoder();
    AudioEncoder(const AudioEncoder &) = delete;
    AudioEncoder &operator=(const AudioEncoder &) = delete;

    bool isValid() const;
    /// Encodes exactly kFrameSamples samples between -1 and 1. Empty on failure.
    QByteArray encode(const QList<float> &frame);

private:
    class Private;
    std::unique_ptr<Private> d;
};

/// Turns received Opus packets back into 48 kHz mono samples. Use one per speaker.
class AudioDecoder {
public:
    AudioDecoder();
    ~AudioDecoder();
    AudioDecoder(const AudioDecoder &) = delete;
    AudioDecoder &operator=(const AudioDecoder &) = delete;

    bool isValid() const;
    /// kFrameSamples samples, or silence for a damaged packet.
    QList<float> decode(const QByteArray &packet);

private:
    class Private;
    std::unique_ptr<Private> d;
};

/// Reads a WAV, FLAC or MP3 file as 48 kHz mono, 20 ms at a time, for bots that play
/// music or recordings into a channel (since SDK 0.4). Encode each frame with an
/// AudioEncoder (Content::Music) and send it with sendAudio(packet, AudioStream::Media),
/// one frame every 20 ms, so listeners hear it labelled as a file, not as speech. The
/// file is read a little at a time, so long recordings are fine.
class AudioFileReader {
public:
    AudioFileReader();
    ~AudioFileReader();
    AudioFileReader(const AudioFileReader &) = delete;
    AudioFileReader &operator=(const AudioFileReader &) = delete;

    bool open(const QString &path, QString *errorMessage = nullptr);
    void close();
    bool isOpen() const;
    /// The next AudioEncoder::kFrameSamples samples, the last one padded with silence;
    /// empty once the file has ended.
    QList<float> readFrame();
    /// Length in milliseconds, or -1 if the file does not say.
    qint64 durationMs() const;
    qint64 positionMs() const;
    bool seekMs(qint64 ms);

private:
    class Private;
    std::unique_ptr<Private> d;
};

struct TextMessage {
    enum class Kind { Channel, Direct, ServerWide };
    Kind kind = Kind::Channel;
    quint32 fromUserId = 0;
    QString fromName;      ///< Empty if the sender already left.
    quint32 channelId = 0; ///< For Kind::Channel.
    QString text;
    QDateTime time; ///< Server time, UTC.
};

struct DisconnectReason {
    enum class Kind {
        UserRequested,
        ServerClosed,
        UntrustedCertificate, ///< certificateFingerprint says what the server presented.
        VersionMismatch,
        LoginFailed,
        ProtocolError,
        Timeout,
        NetworkError,
    };
    Kind kind = Kind::NetworkError;
    /// Plain-English explanation, suitable for logs and simple user interfaces.
    QString message;
    QString certificateFingerprint;
};

/// A connection to an OpenTalk server.
///
/// Use it from a thread with a running Qt event loop; all signals are delivered on the
/// thread that created the object. Call Client::initialize() once before the first
/// connection.
///
/// Draft API (SDK 0.x): connection and login, channels, users, text and audio.
class Client : public QObject {
    Q_OBJECT

public:
    explicit Client(QObject *parent = nullptr);
    ~Client() override;

    /// Sets up TLS. Returns false with a message if the system cannot provide TLS 1.3.
    static bool initialize(QString *errorMessage = nullptr);

    /// Name and version of your program, shown to server administrators.
    void setApplication(const QString &name, const QString &version);

    void connectToServer(const ServerAddress &address, const ServerIdentity &identity, const Login &login);
    /// Ends the connection; also stops automatic reconnection.
    void disconnectFromServer();

    ClientState state() const;
    /// Valid while connected.
    ServerInfo serverInfo() const;
    User self() const;
    QList<Channel> channels() const;
    QList<User> users() const;

    /// Requests return an ID (0 if not connected); the outcome arrives as
    /// requestFinished() with that ID.
    quint64 joinChannel(quint32 channelId, const QString &password = {});
    quint64 sendChannelMessage(const QString &text);
    quint64 sendDirectMessage(quint32 userId, const QString &text);
    /// A message to everyone on the server; needs the broadcast.send permission.
    quint64 sendServerMessage(const QString &text);
    /// Sets this client's status message (up to 100 characters; empty clears it). Since SDK 0.4.
    quint64 setStatus(const QString &status);

    /// True once audio can be sent (after each login; see audioReady()).
    bool isAudioReady() const;
    /// True while audio travels over the faster UDP path; otherwise it goes through the
    /// main connection (for example when a firewall blocks UDP). Since SDK 0.4.
    bool isAudioUsingUdp() const;
    /// Sends one Opus packet (20 ms; see AudioEncoder) to the people in your channel.
    /// Send one every 20 ms while talking. Returns false if audio is not ready, the
    /// packet is empty or too large. The server may refuse audio you may not send.
    bool sendAudio(const QByteArray &opusPacket, AudioStream stream = AudioStream::Voice);

    /// Measures the round-trip time; the answer arrives as pingReplied().
    bool ping();

signals:
    void stateChanged(opentalk::sdk::ClientState state);
    /// Logged in. Also emitted after each automatic reconnection.
    void connected(const opentalk::sdk::ServerInfo &server);
    void disconnected(const opentalk::sdk::DisconnectReason &reason);
    void reconnecting(int attempt, int delayMs);
    void pingReplied(qint64 milliseconds);
    void requestFinished(quint64 requestId, bool succeeded, const QString &errorMessage);
    void channelsChanged();
    void userJoined(const opentalk::sdk::User &user);
    void userLeft(const opentalk::sdk::User &user);
    void userMoved(const opentalk::sdk::User &user);
    void textReceived(const opentalk::sdk::TextMessage &message);
    /// A user's role, mutes, bot status or status message changed.
    void userUpdated(const opentalk::sdk::User &user);
    /// Audio can be sent (after each login).
    void audioReady();
    /// Audio switched between UDP and the main connection (since SDK 0.4).
    void audioPathChanged(bool usingUdp);
    /// One Opus packet from someone in your channel (decode with AudioDecoder).
    void audioReceived(quint32 userId, opentalk::sdk::AudioStream stream, const QByteArray &opusPacket);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace opentalk::sdk
