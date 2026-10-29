// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/sdk/Client.h"

#include "opentalk/audio/AudioFormat.h"
#include "opentalk/audio/MediaFile.h"
#include "opentalk/audio/OpusCodec.h"
#include "opentalk/client/Connection.h"
#include "opentalk/security/Tls.h"

#include <QTimeZone>

namespace opentalk::sdk {

SdkVersion runtimeVersion()
{
    return {OPENTALK_SDK_VERSION_MAJOR, OPENTALK_SDK_VERSION_MINOR, OPENTALK_SDK_VERSION_PATCH};
}

class Client::Private {
public:
    client::Connection connection;
    QString applicationName = QStringLiteral("OpenTalk SDK client");
    QString applicationVersion = QStringLiteral("unspecified");
    quint16 sequence[3] = {0, 0, 0};
};

static_assert(AudioEncoder::kFrameSamples == audio::kFrameSamples && AudioEncoder::kSampleRate == audio::kSampleRate);

class AudioEncoder::Private {
public:
    Private(Content content, int bitrate)
        : encoder(content == Content::Voice ? audio::OpusEncoderWrapper::Content::Voice
                                            : audio::OpusEncoderWrapper::Content::Music,
                  bitrate)
    {
    }
    audio::OpusEncoderWrapper encoder;
};

AudioEncoder::AudioEncoder(Content content, int bitrate)
    : d(std::make_unique<Private>(content, bitrate))
{
}

AudioEncoder::~AudioEncoder() = default;

bool AudioEncoder::isValid() const
{
    return d->encoder.isValid();
}

QByteArray AudioEncoder::encode(const QList<float> &frame)
{
    if (frame.size() != kFrameSamples) {
        return {};
    }
    return d->encoder.encode(std::span<const float>(frame.constData(), static_cast<std::size_t>(frame.size())));
}

class AudioFileReader::Private {
public:
    audio::MediaFile file;
};

AudioFileReader::AudioFileReader()
    : d(std::make_unique<Private>())
{
}

AudioFileReader::~AudioFileReader() = default;

bool AudioFileReader::open(const QString &path, QString *errorMessage)
{
    return d->file.open(path, errorMessage);
}

void AudioFileReader::close()
{
    d->file.close();
}

bool AudioFileReader::isOpen() const
{
    return d->file.isOpen();
}

QList<float> AudioFileReader::readFrame()
{
    QList<float> frame(AudioEncoder::kFrameSamples, 0.0f);
    const std::size_t got = d->file.read(std::span<float>(frame.data(), static_cast<std::size_t>(frame.size())));
    if (got == 0) {
        return {};
    }
    return frame;
}

qint64 AudioFileReader::durationMs() const
{
    return d->file.durationMs();
}

qint64 AudioFileReader::positionMs() const
{
    return d->file.positionMs();
}

bool AudioFileReader::seekMs(qint64 ms)
{
    return d->file.seekMs(ms);
}

class AudioDecoder::Private {
public:
    audio::OpusDecoderWrapper decoder;
};

AudioDecoder::AudioDecoder()
    : d(std::make_unique<Private>())
{
}

AudioDecoder::~AudioDecoder() = default;

bool AudioDecoder::isValid() const
{
    return d->decoder.isValid();
}

QList<float> AudioDecoder::decode(const QByteArray &packet)
{
    QList<float> out(AudioEncoder::kFrameSamples, 0.0f);
    d->decoder.decode(packet, std::span<float>(out.data(), static_cast<std::size_t>(out.size())));
    return out;
}

namespace {

ClientState toSdk(client::ConnectionState state)
{
    switch (state) {
    case client::ConnectionState::Disconnected:
        return ClientState::Disconnected;
    case client::ConnectionState::Connecting:
    case client::ConnectionState::Handshaking:
        return ClientState::Connecting;
    case client::ConnectionState::Connected:
        return ClientState::Connected;
    case client::ConnectionState::Reconnecting:
        return ClientState::Reconnecting;
    }
    return ClientState::Disconnected;
}

DisconnectReason::Kind toSdk(client::DisconnectInfo::Reason reason)
{
    using R = client::DisconnectInfo::Reason;
    using K = DisconnectReason::Kind;
    switch (reason) {
    case R::UserRequested:
        return K::UserRequested;
    case R::ServerClosed:
        return K::ServerClosed;
    case R::UntrustedCertificate:
        return K::UntrustedCertificate;
    case R::VersionMismatch:
        return K::VersionMismatch;
    case R::LoginFailed:
        return K::LoginFailed;
    case R::ProtocolError:
        return K::ProtocolError;
    case R::Timeout:
        return K::Timeout;
    case R::NetworkError:
        return K::NetworkError;
    }
    return K::NetworkError;
}

Role toSdk(protocol::Role role)
{
    switch (role) {
    case protocol::Role::Guest:
        return Role::Guest;
    case protocol::Role::User:
        return Role::User;
    case protocol::Role::Admin:
        return Role::Admin;
    case protocol::Role::Owner:
        return Role::Owner;
    case protocol::Role::Moderator:
        return Role::Moderator;
    case protocol::Role::Restricted:
        return Role::Restricted;
    }
    return Role::Custom;
}

User toSdk(const protocol::UserInfo &u, const client::ClientState &state)
{
    User user;
    user.id = u.id;
    user.name = u.name;
    user.role = toSdk(u.role);
    user.channelId = u.channelId;
    user.roleName = state.roleName(static_cast<quint32>(u.role));
    user.bot = (u.flags & protocol::BotFlag) != 0;
    user.voiceMuted = (u.flags & protocol::VoiceMutedFlag) != 0;
    user.textMuted = (u.flags & protocol::TextMutedFlag) != 0;
    user.status = u.status;
    return user;
}

AudioStream toSdk(protocol::AudioStream stream)
{
    switch (stream) {
    case protocol::AudioStream::Voice:
        return AudioStream::Voice;
    case protocol::AudioStream::Media:
        return AudioStream::Media;
    case protocol::AudioStream::SystemAudio:
        return AudioStream::SystemAudio;
    }
    return AudioStream::Voice;
}

protocol::AudioStream toProtocol(AudioStream stream)
{
    switch (stream) {
    case AudioStream::Voice:
        return protocol::AudioStream::Voice;
    case AudioStream::Media:
        return protocol::AudioStream::Media;
    case AudioStream::SystemAudio:
        return protocol::AudioStream::SystemAudio;
    }
    return protocol::AudioStream::Voice;
}

Channel toSdk(const protocol::ChannelInfo &c)
{
    return {c.id, c.parentId, c.name, c.topic, c.persistent, c.hasPassword, c.maxUsers};
}

} // namespace

Client::Client(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    client::Connection &c = d->connection;
    connect(&c, &client::Connection::stateChanged, this,
            [this](client::ConnectionState state) { emit stateChanged(toSdk(state)); });
    connect(&c, &client::Connection::connected, this, [this] {
        emit connected(serverInfo());
        emit channelsChanged();
    });
    connect(&c, &client::Connection::disconnected, this, [this](const client::DisconnectInfo &info) {
        emit disconnected({toSdk(info.reason), info.detail, info.presentedFingerprint});
    });
    connect(&c, &client::Connection::connectionLost, this,
            [this](const client::DisconnectInfo &, int attempt, int delayMs) { emit reconnecting(attempt, delayMs); });
    connect(&c, &client::Connection::roundTripMeasured, this, &Client::pingReplied);
    connect(&c, &client::Connection::requestFinished, this,
            [this](quint64 id, const std::optional<protocol::Error> &error, std::optional<quint32>) {
                emit requestFinished(id, !error.has_value(), error ? error->detail : QString());
            });

    const client::ClientState &state = c.serverState();
    connect(&state, &client::ClientState::channelAdded, this, &Client::channelsChanged);
    connect(&state, &client::ClientState::channelRemoved, this, &Client::channelsChanged);
    connect(&state, &client::ClientState::channelUpdated, this, &Client::channelsChanged);
    connect(&state, &client::ClientState::userJoined, this,
            [this](const protocol::UserInfo &u) { emit userJoined(toSdk(u, d->connection.serverState())); });
    connect(&state, &client::ClientState::userLeft, this,
            [this](const protocol::UserInfo &u) { emit userLeft(toSdk(u, d->connection.serverState())); });
    connect(&state, &client::ClientState::userMoved, this,
            [this](const protocol::UserInfo &u, quint32) { emit userMoved(toSdk(u, d->connection.serverState())); });
    connect(&state, &client::ClientState::userUpdated, this,
            [this](const protocol::UserInfo &u, const protocol::UserInfo &) {
                emit userUpdated(toSdk(u, d->connection.serverState()));
            });
    connect(&c, &client::Connection::voiceReady, this, &Client::audioReady);
    connect(&c, &client::Connection::voicePathChanged, this, &Client::audioPathChanged);
    connect(&c, &client::Connection::audioReceived, this,
            [this](const protocol::AudioData &a) { emit audioReceived(a.userId, toSdk(a.stream), a.payload); });
    connect(&state, &client::ClientState::textReceived, this, [this](const protocol::TextMessage &m) {
        TextMessage out;
        switch (m.target) {
        case protocol::TextTarget::Channel:
            out.kind = TextMessage::Kind::Channel;
            out.channelId = m.targetId;
            break;
        case protocol::TextTarget::User:
            out.kind = TextMessage::Kind::Direct;
            break;
        case protocol::TextTarget::Server:
            out.kind = TextMessage::Kind::ServerWide;
            break;
        }
        out.fromUserId = m.fromUserId;
        const auto sender = d->connection.serverState().user(m.fromUserId);
        out.fromName = sender ? sender->name : QString();
        out.text = m.text;
        out.time = QDateTime::fromMSecsSinceEpoch(m.timestampMs, QTimeZone::utc());
        emit textReceived(out);
    });
}

Client::~Client() = default;

bool Client::initialize(QString *errorMessage)
{
    return security::initializeTls(errorMessage);
}

void Client::setApplication(const QString &name, const QString &version)
{
    d->applicationName = name;
    d->applicationVersion = version;
}

void Client::connectToServer(const ServerAddress &address, const ServerIdentity &identity, const Login &login)
{
    client::ConnectOptions options;
    options.host = address.host;
    options.port = address.port;
    options.trust = identity.pinnedFingerprint.isEmpty() ? security::ServerTrust::systemAuthorities()
                                                         : security::ServerTrust::pinned(identity.pinnedFingerprint);
    options.credentials.mode =
        login.mode == Login::Mode::Guest ? client::Credentials::Mode::Guest : client::Credentials::Mode::Account;
    options.credentials.name = login.name;
    options.credentials.password = login.password;
    options.session.clientName = d->applicationName;
    options.session.clientVersion = d->applicationVersion;
    d->connection.connectToServer(options);
}

void Client::disconnectFromServer()
{
    d->connection.disconnectFromServer();
}

ClientState Client::state() const
{
    return toSdk(d->connection.state());
}

ServerInfo Client::serverInfo() const
{
    const protocol::Welcome welcome = d->connection.serverInfo();
    return {welcome.serverName, welcome.serverVersion, welcome.version, d->connection.serverFingerprint()};
}

User Client::self() const
{
    const client::ClientState &state = d->connection.serverState();
    return toSdk(state.user(state.selfId()).value_or(protocol::UserInfo{}), state);
}

QList<Channel> Client::channels() const
{
    QList<Channel> result;
    for (const protocol::ChannelInfo &c : d->connection.serverState().channels()) {
        result.append(toSdk(c));
    }
    return result;
}

QList<User> Client::users() const
{
    QList<User> result;
    for (const protocol::UserInfo &u : d->connection.serverState().users()) {
        result.append(toSdk(u, d->connection.serverState()));
    }
    return result;
}

quint64 Client::joinChannel(quint32 channelId, const QString &password)
{
    return d->connection.joinChannel(channelId, password);
}

quint64 Client::sendChannelMessage(const QString &text)
{
    return d->connection.sendText(protocol::TextTarget::Channel, d->connection.serverState().currentChannelId(), text);
}

quint64 Client::sendDirectMessage(quint32 userId, const QString &text)
{
    return d->connection.sendText(protocol::TextTarget::User, userId, text);
}

quint64 Client::sendServerMessage(const QString &text)
{
    return d->connection.sendText(protocol::TextTarget::Server, 0, text);
}

quint64 Client::setStatus(const QString &status)
{
    if (status.size() > protocol::kMaxStatusLength) {
        return 0;
    }
    return d->connection.sendRequest(protocol::SetStatus{0, status});
}

bool Client::isAudioReady() const
{
    return d->connection.isVoiceReady();
}

bool Client::isAudioUsingUdp() const
{
    return d->connection.isVoiceUsingUdp();
}

bool Client::sendAudio(const QByteArray &opusPacket, AudioStream stream)
{
    quint16 &sequence = d->sequence[static_cast<int>(stream)];
    if (!d->connection.sendAudio(toProtocol(stream), sequence, opusPacket)) {
        return false;
    }
    ++sequence;
    return true;
}

bool Client::ping()
{
    return d->connection.measureRoundTrip();
}

} // namespace opentalk::sdk
