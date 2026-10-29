// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/security/Tls.h"

#include "opentalk/logging/Logging.h"

#include <QCryptographicHash>
#include <QFile>
#include <QRegularExpression>
#include <QSslKey>
#include <QSslSocket>

namespace opentalk::security {
namespace {

void setError(QString *target, const QString &message)
{
    if (target != nullptr) {
        *target = message;
    }
}

QByteArray readSmallFile(const QString &path, QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage, QStringLiteral("Cannot open %1: %2").arg(path, file.errorString()));
        return {};
    }
    if (file.size() > 1024 * 1024) {
        setError(errorMessage, QStringLiteral("%1 is too large to be a certificate or key").arg(path));
        return {};
    }
    return file.readAll();
}

} // namespace

bool initializeTls(QString *errorMessage)
{
    const QString preferred = QStringLiteral("openssl");
    if (QSslSocket::activeBackend() != preferred && !QSslSocket::setActiveBackend(preferred)) {
        setError(errorMessage, QStringLiteral("The OpenSSL TLS backend is not available (available: %1). "
                                              "OpenTalk needs Qt's OpenSSL backend and the OpenSSL 3 libraries.")
                                   .arg(QSslSocket::availableBackends().join(QStringLiteral(", "))));
        return false;
    }
    if (!QSslSocket::supportsSsl()) {
        setError(errorMessage, QStringLiteral("The OpenSSL 3 libraries could not be loaded. On Windows, "
                                              "libssl-3-x64.dll and libcrypto-3-x64.dll must be next to "
                                              "the program."));
        return false;
    }
    if (!QSslSocket::isProtocolSupported(QSsl::TlsV1_3OrLater)) {
        setError(errorMessage, QStringLiteral("The TLS library does not support TLS 1.3."));
        return false;
    }
    qCInfo(lcSecurity).noquote() << "TLS backend:" << QSslSocket::activeBackend() << "library"
                                 << QSslSocket::sslLibraryVersionString();
    return true;
}

QString certificateFingerprint(const QSslCertificate &certificate)
{
    const QByteArray hex = certificate.digest(QCryptographicHash::Sha256).toHex().toUpper();
    QString result = QStringLiteral("SHA256:");
    for (qsizetype i = 0; i < hex.size(); i += 2) {
        if (i > 0) {
            result += QLatin1Char(':');
        }
        result += QLatin1String(hex.mid(i, 2));
    }
    return result;
}

std::optional<QString> normalizeFingerprint(const QString &text)
{
    QString hex = text.trimmed();
    if (hex.startsWith(QLatin1String("SHA256:"), Qt::CaseInsensitive)) {
        hex = hex.mid(7);
    }
    hex.remove(QRegularExpression(QStringLiteral("[\\s:]")));
    static const QRegularExpression valid(QStringLiteral("^[0-9A-Fa-f]{64}$"));
    if (!valid.match(hex).hasMatch()) {
        return std::nullopt;
    }
    hex = hex.toUpper();
    QString result = QStringLiteral("SHA256:");
    for (qsizetype i = 0; i < hex.size(); i += 2) {
        if (i > 0) {
            result += QLatin1Char(':');
        }
        result += hex.mid(i, 2);
    }
    return result;
}

QSslConfiguration clientTlsConfiguration()
{
    QSslConfiguration config = QSslConfiguration::defaultConfiguration();
    config.setProtocol(QSsl::TlsV1_3OrLater);
    config.setPeerVerifyMode(QSslSocket::VerifyPeer);
    return config;
}

std::optional<QSslConfiguration> serverTlsConfiguration(const QString &certificatePath, const QString &keyPath,
                                                        QString *errorMessage)
{
    const QByteArray certPem = readSmallFile(certificatePath, errorMessage);
    if (certPem.isEmpty()) {
        if (errorMessage != nullptr && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("%1 is empty").arg(certificatePath);
        }
        return std::nullopt;
    }
    const QList<QSslCertificate> chain = QSslCertificate::fromData(certPem, QSsl::Pem);
    if (chain.isEmpty() || chain.first().isNull()) {
        setError(errorMessage, QStringLiteral("%1 does not contain a PEM certificate").arg(certificatePath));
        return std::nullopt;
    }

    const QByteArray keyPem = readSmallFile(keyPath, errorMessage);
    if (keyPem.isEmpty()) {
        if (errorMessage != nullptr && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("%1 is empty").arg(keyPath);
        }
        return std::nullopt;
    }
    QSslKey key(keyPem, QSsl::Ec, QSsl::Pem, QSsl::PrivateKey);
    if (key.isNull()) {
        key = QSslKey(keyPem, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey);
    }
    if (key.isNull()) {
        setError(errorMessage,
                 QStringLiteral("%1 does not contain an unencrypted EC or RSA private key in PEM format").arg(keyPath));
        return std::nullopt;
    }

    QSslConfiguration config = QSslConfiguration::defaultConfiguration();
    config.setProtocol(QSsl::TlsV1_3OrLater);
    config.setPeerVerifyMode(QSslSocket::VerifyNone); // clients authenticate inside the protocol
    config.setLocalCertificateChain(chain);
    config.setPrivateKey(key);
    return config;
}

} // namespace opentalk::security
