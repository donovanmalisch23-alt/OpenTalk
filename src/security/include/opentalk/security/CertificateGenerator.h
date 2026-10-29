// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>
#include <QStringList>

namespace opentalk::security {

struct SelfSignedCertificateRequest {
    QString commonName = QStringLiteral("OpenTalk server");
    /// Host names and IP addresses for the subjectAltName extension. Optional, because
    /// OpenTalk clients identify self-signed servers by fingerprint, not by name.
    QStringList alternativeNames;
    int validDays = 3650;
};

/// Creates a new EC P-256 private key and a self-signed X.509 certificate for it, and
/// writes both as PEM files. The key file is written with owner-only permissions where
/// the platform supports it. Existing files are never overwritten.
bool generateSelfSignedCertificate(const SelfSignedCertificateRequest &request, const QString &certificatePath,
                                   const QString &keyPath, QString *errorMessage = nullptr);

} // namespace opentalk::security
