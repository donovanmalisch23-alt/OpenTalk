// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/sdk/Client.h"

#include "opentalk/client/Connection.h"
#include "opentalk/security/Tls.h"

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
};

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
    case R::ProtocolError:
        return K::ProtocolError;
    case R::Timeout:
        return K::Timeout;
    case R::NetworkError:
        return K::NetworkError;
    }
    return K::NetworkError;
}

} // namespace

Client::Client(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    connect(&d->connection, &client::Connection::stateChanged, this,
            [this](client::ConnectionState state) { emit stateChanged(toSdk(state)); });
    connect(&d->connection, &client::Connection::connected, this, [this] { emit connected(serverInfo()); });
    connect(&d->connection, &client::Connection::disconnected, this, [this](const client::DisconnectInfo &info) {
        emit disconnected({toSdk(info.reason), info.detail, info.presentedFingerprint});
    });
    connect(&d->connection, &client::Connection::roundTripMeasured, this, &Client::pingReplied);
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

void Client::connectToServer(const ServerAddress &address, const ServerIdentity &identity)
{
    client::ConnectOptions options;
    options.host = address.host;
    options.port = address.port;
    options.trust = identity.pinnedFingerprint.isEmpty() ? security::ServerTrust::systemAuthorities()
                                                         : security::ServerTrust::pinned(identity.pinnedFingerprint);
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

bool Client::ping()
{
    return d->connection.measureRoundTrip();
}

} // namespace opentalk::sdk
