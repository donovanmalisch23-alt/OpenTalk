// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/sdk/Version.h"

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

struct ServerInfo {
    QString name;
    QString softwareVersion;
    int protocolVersion = 0;
    QString certificateFingerprint;
};

enum class ClientState { Disconnected, Connecting, Connected };

struct DisconnectReason {
    enum class Kind {
        UserRequested,
        ServerClosed,
        UntrustedCertificate, ///< certificateFingerprint says what the server presented.
        VersionMismatch,
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
/// Draft API (SDK 0.x): connection, server information and round-trip measurement.
/// Channels, users, text and audio are added as the server gains those features.
class Client : public QObject {
    Q_OBJECT

public:
    explicit Client(QObject *parent = nullptr);
    ~Client() override;

    /// Sets up TLS. Returns false with a message if the system cannot provide TLS 1.3.
    static bool initialize(QString *errorMessage = nullptr);

    /// Name and version of your program, shown to server administrators.
    void setApplication(const QString &name, const QString &version);

    void connectToServer(const ServerAddress &address, const ServerIdentity &identity = {});
    void disconnectFromServer();

    ClientState state() const;
    /// Valid while connected.
    ServerInfo serverInfo() const;

    /// Measures the round-trip time; the answer arrives as pingReplied().
    bool ping();

signals:
    void stateChanged(opentalk::sdk::ClientState state);
    void connected(const opentalk::sdk::ServerInfo &server);
    void disconnected(const opentalk::sdk::DisconnectReason &reason);
    void pingReplied(qint64 milliseconds);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace opentalk::sdk
