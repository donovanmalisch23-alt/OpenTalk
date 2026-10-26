// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QSslCertificate>
#include <QSslConfiguration>
#include <QString>

#include <optional>

namespace opentalk::security {

/// Selects and checks the TLS implementation. Call once at start-up, before any socket
/// is created. OpenTalk requires TLS 1.3, and Windows 10's built-in TLS (Schannel) does
/// not provide it, so Qt's OpenSSL backend is used on every platform (ADR 0007).
/// Returns false with a user-readable message if TLS 1.3 is unavailable, for example
/// because the OpenSSL 3 libraries were not found.
bool initializeTls(QString *errorMessage = nullptr);

/// Human-readable SHA-256 fingerprint, for example "SHA256:3A:F1:...". This is the value
/// users compare and pin when trusting a self-signed server.
QString certificateFingerprint(const QSslCertificate &certificate);

/// Normalises user-entered fingerprints (spaces, case, optional "SHA256:" prefix).
/// Returns nullopt if the text is not a SHA-256 fingerprint.
std::optional<QString> normalizeFingerprint(const QString &text);

/// TLS settings for a client connection: TLS 1.3 only, peer certificate required.
QSslConfiguration clientTlsConfiguration();

/// Loads a PEM certificate (optionally followed by intermediate certificates) and a PEM
/// private key (EC or RSA, unencrypted) and builds a TLS 1.3-only server configuration.
std::optional<QSslConfiguration> serverTlsConfiguration(const QString &certificatePath, const QString &keyPath,
                                                        QString *errorMessage = nullptr);

} // namespace opentalk::security
