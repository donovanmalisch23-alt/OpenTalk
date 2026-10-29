// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QList>
#include <QSslCertificate>
#include <QSslError>
#include <QString>

namespace opentalk::security {

/// How a client decides whether a server's certificate proves the server's identity.
///
/// Most self-hosted servers use self-signed certificates, so OpenTalk uses trust on first
/// use: the first connection fails with the certificate's fingerprint, the user confirms
/// it, and later connections pin that exact fingerprint. Servers with a certificate from
/// a public authority can use ordinary validation instead.
struct ServerTrust {
    enum class Mode {
        /// Accept only a certificate that passes normal validation against the system's
        /// trusted authorities and matches the host name.
        SystemAuthorities,
        /// Accept only the certificate with this exact SHA-256 fingerprint, whoever
        /// issued it. Validation errors such as "self-signed" are expected and ignored.
        PinnedFingerprint,
    };

    Mode mode = Mode::SystemAuthorities;
    QString pinnedFingerprint; ///< Normalised, as returned by normalizeFingerprint().

    static ServerTrust systemAuthorities() { return {}; }
    static ServerTrust pinned(const QString &fingerprint);
};

struct TrustEvaluation {
    bool accepted = false;
    /// Fingerprint of the certificate the server presented, to show the user.
    QString presentedFingerprint;
    /// Why the certificate was rejected, in plain English; empty when accepted.
    QString reason;
};

/// Decides whether to continue a TLS connection. `errors` are the validation errors Qt
/// reported (empty if validation passed).
TrustEvaluation evaluateServerCertificate(const ServerTrust &trust, const QSslCertificate &presented,
                                          const QList<QSslError> &errors);

} // namespace opentalk::security
