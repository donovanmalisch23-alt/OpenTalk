// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/security/ServerTrust.h"

#include "opentalk/security/Tls.h"

#include <QStringList>

namespace opentalk::security {

ServerTrust ServerTrust::pinned(const QString &fingerprint)
{
    return {Mode::PinnedFingerprint, normalizeFingerprint(fingerprint).value_or(QString())};
}

TrustEvaluation evaluateServerCertificate(const ServerTrust &trust, const QSslCertificate &presented,
                                          const QList<QSslError> &errors)
{
    TrustEvaluation result;
    if (presented.isNull()) {
        result.reason = QStringLiteral("the server did not present a certificate");
        return result;
    }
    result.presentedFingerprint = certificateFingerprint(presented);

    switch (trust.mode) {
    case ServerTrust::Mode::PinnedFingerprint:
        if (trust.pinnedFingerprint.isEmpty()) {
            result.reason = QStringLiteral("no valid fingerprint was configured for this server");
        } else if (result.presentedFingerprint != trust.pinnedFingerprint) {
            result.reason = QStringLiteral("the server's certificate does not match the one you trusted before. "
                                           "This can mean the server was reinstalled, or that someone is "
                                           "intercepting the connection");
        } else {
            result.accepted = true;
        }
        break;
    case ServerTrust::Mode::SystemAuthorities:
        if (errors.isEmpty()) {
            result.accepted = true;
        } else {
            QStringList problems;
            for (const QSslError &error : errors) {
                problems << error.errorString();
            }
            result.reason =
                QStringLiteral("the server's certificate is not trusted (%1)").arg(problems.join(QStringLiteral("; ")));
        }
        break;
    }
    return result;
}

} // namespace opentalk::security
