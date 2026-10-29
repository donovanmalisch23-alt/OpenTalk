// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/security/CertificateGenerator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QSaveFile>

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <memory>

namespace opentalk::security {
namespace {

// RAII owners for OpenSSL objects.
struct PkeyDeleter {
    void operator()(EVP_PKEY *p) const { EVP_PKEY_free(p); }
};
struct X509Deleter {
    void operator()(X509 *p) const { X509_free(p); }
};
struct BioDeleter {
    void operator()(BIO *p) const { BIO_free(p); }
};
struct BnDeleter {
    void operator()(BIGNUM *p) const { BN_free(p); }
};
struct ExtDeleter {
    void operator()(X509_EXTENSION *p) const { X509_EXTENSION_free(p); }
};

bool fail(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    return false;
}

QByteArray bioContents(BIO *bio)
{
    char *data = nullptr;
    const long length = BIO_get_mem_data(bio, &data);
    return length > 0 ? QByteArray(data, static_cast<qsizetype>(length)) : QByteArray();
}

bool addExtension(X509 *certificate, int nid, const QByteArray &value)
{
    X509V3_CTX context;
    X509V3_set_ctx_nodb(&context);
    X509V3_set_ctx(&context, certificate, certificate, nullptr, nullptr, 0);
    std::unique_ptr<X509_EXTENSION, ExtDeleter> extension(
        X509V3_EXT_conf_nid(nullptr, &context, nid, value.constData()));
    return extension && X509_add_ext(certificate, extension.get(), -1) == 1;
}

bool writeFile(const QString &path, const QByteArray &contents, bool privateFile, QString *errorMessage)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return fail(errorMessage, QStringLiteral("Cannot write %1: %2").arg(path, file.errorString()));
    }
    if (privateFile) {
        // Restrict before the contents are written. On Windows this sets the read-only
        // attribute only; the data folder's own permissions protect the key there.
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    file.write(contents);
    if (!file.commit()) {
        return fail(errorMessage, QStringLiteral("Cannot save %1: %2").arg(path, file.errorString()));
    }
    return true;
}

} // namespace

bool generateSelfSignedCertificate(const SelfSignedCertificateRequest &request, const QString &certificatePath,
                                   const QString &keyPath, QString *errorMessage)
{
    if (QFileInfo::exists(certificatePath) || QFileInfo::exists(keyPath)) {
        return fail(errorMessage, QStringLiteral("refusing to overwrite an existing certificate or key"));
    }
    if (request.validDays < 1 || request.validDays > 36500 || request.commonName.trimmed().isEmpty()) {
        return fail(errorMessage, QStringLiteral("invalid certificate request"));
    }

    std::unique_ptr<EVP_PKEY, PkeyDeleter> key(EVP_EC_gen("P-256"));
    if (!key) {
        return fail(errorMessage, QStringLiteral("could not generate an EC key"));
    }
    std::unique_ptr<X509, X509Deleter> certificate(X509_new());
    if (!certificate) {
        return fail(errorMessage, QStringLiteral("out of memory"));
    }

    // Random positive 127-bit serial number, as recommended for certificate serials.
    unsigned char serialBytes[16];
    if (RAND_bytes(serialBytes, sizeof serialBytes) != 1) {
        return fail(errorMessage, QStringLiteral("random number generator failed"));
    }
    serialBytes[0] &= 0x7f;
    std::unique_ptr<BIGNUM, BnDeleter> serial(BN_bin2bn(serialBytes, sizeof serialBytes, nullptr));
    const QByteArray commonName = request.commonName.trimmed().left(64).toUtf8();

    bool ok = serial && X509_set_version(certificate.get(), 2) == 1 &&
              BN_to_ASN1_INTEGER(serial.get(), X509_get_serialNumber(certificate.get())) != nullptr &&
              X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -3600) != nullptr &&
              X509_gmtime_adj(X509_getm_notAfter(certificate.get()), static_cast<long>(request.validDays) * 86400) !=
                  nullptr &&
              X509_set_pubkey(certificate.get(), key.get()) == 1;
    X509_NAME *name = X509_get_subject_name(certificate.get());
    ok = ok &&
         X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_UTF8,
                                    reinterpret_cast<const unsigned char *>(commonName.constData()),
                                    static_cast<int>(commonName.size()), -1, 0) == 1 &&
         X509_set_issuer_name(certificate.get(), name) == 1;

    ok = ok && addExtension(certificate.get(), NID_basic_constraints, "critical,CA:FALSE") &&
         addExtension(certificate.get(), NID_key_usage, "critical,digitalSignature,keyCertSign") &&
         addExtension(certificate.get(), NID_ext_key_usage, "serverAuth") &&
         addExtension(certificate.get(), NID_subject_key_identifier, "hash");
    if (ok && !request.alternativeNames.isEmpty()) {
        QStringList entries;
        for (const QString &altName : request.alternativeNames) {
            const QString trimmed = altName.trimmed();
            if (trimmed.isEmpty() || trimmed.contains(QLatin1Char(','))) {
                return fail(errorMessage, QStringLiteral("invalid alternative name: %1").arg(altName));
            }
            entries << (QHostAddress(trimmed).isNull() ? QStringLiteral("DNS:") : QStringLiteral("IP:")) + trimmed;
        }
        ok = addExtension(certificate.get(), NID_subject_alt_name, entries.join(QLatin1Char(',')).toUtf8());
    }
    ok = ok && X509_sign(certificate.get(), key.get(), EVP_sha256()) > 0;
    if (!ok) {
        return fail(errorMessage, QStringLiteral("could not build the certificate"));
    }

    std::unique_ptr<BIO, BioDeleter> certBio(BIO_new(BIO_s_mem()));
    std::unique_ptr<BIO, BioDeleter> keyBio(BIO_new(BIO_s_mem()));
    if (!certBio || !keyBio || PEM_write_bio_X509(certBio.get(), certificate.get()) != 1 ||
        PEM_write_bio_PrivateKey(keyBio.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1) {
        return fail(errorMessage, QStringLiteral("could not encode the certificate"));
    }

    for (const QString &path : {certificatePath, keyPath}) {
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
            return fail(errorMessage, QStringLiteral("Cannot create the folder for %1").arg(path));
        }
    }
    // Key first: a certificate without its key is useless, so never leave only the cert.
    if (!writeFile(keyPath, bioContents(keyBio.get()), true, errorMessage)) {
        return false;
    }
    if (!writeFile(certificatePath, bioContents(certBio.get()), false, errorMessage)) {
        QFile::remove(keyPath);
        return false;
    }
    return true;
}

} // namespace opentalk::security
