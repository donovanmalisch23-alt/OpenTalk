// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/security/CertificateGenerator.h"
#include "opentalk/security/Tls.h"

#include <QDateTime>
#include <QFile>
#include <QSslCertificate>
#include <QTemporaryDir>
#include <QTest>

using namespace opentalk::security;

class TestCertificate : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QString error;
        QVERIFY2(initializeTls(&error), qPrintable(error));
    }

    void generatedCertificateLoadsAsServerConfiguration()
    {
        QTemporaryDir dir;
        const QString cert = dir.filePath(QStringLiteral("tls/server.crt"));
        const QString key = dir.filePath(QStringLiteral("tls/server.key"));
        SelfSignedCertificateRequest request;
        request.commonName = QStringLiteral("Unit Test Server");
        request.alternativeNames = {QStringLiteral("localhost"), QStringLiteral("127.0.0.1")};
        QString error;
        QVERIFY2(generateSelfSignedCertificate(request, cert, key, &error), qPrintable(error));

        const auto config = serverTlsConfiguration(cert, key, &error);
        QVERIFY2(config.has_value(), qPrintable(error));
        const QSslCertificate c = config->localCertificate();
        QCOMPARE(c.subjectInfo(QSslCertificate::CommonName).value(0), QStringLiteral("Unit Test Server"));
        QVERIFY(c.isSelfSigned());
        QVERIFY(c.expiryDate() > QDateTime::currentDateTimeUtc().addDays(3640));
        QVERIFY(c.effectiveDate() <= QDateTime::currentDateTimeUtc());
        QCOMPARE(c.subjectAlternativeNames().values(QSsl::DnsEntry), QStringList{QStringLiteral("localhost")});
        QVERIFY(certificateFingerprint(c).startsWith(QStringLiteral("SHA256:")));
#ifndef Q_OS_WIN
        QCOMPARE(QFile(key).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
                 QFileDevice::Permissions());
#endif
    }

    void eachCertificateIsUnique()
    {
        QTemporaryDir dir;
        QVERIFY(generateSelfSignedCertificate({}, dir.filePath(QStringLiteral("a.crt")),
                                              dir.filePath(QStringLiteral("a.key"))));
        QVERIFY(generateSelfSignedCertificate({}, dir.filePath(QStringLiteral("b.crt")),
                                              dir.filePath(QStringLiteral("b.key"))));
        QFile a(dir.filePath(QStringLiteral("a.crt")));
        QFile b(dir.filePath(QStringLiteral("b.crt")));
        QVERIFY(a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly));
        QVERIFY(a.readAll() != b.readAll());
    }

    void neverOverwrites()
    {
        QTemporaryDir dir;
        const QString cert = dir.filePath(QStringLiteral("s.crt"));
        const QString key = dir.filePath(QStringLiteral("s.key"));
        QVERIFY(generateSelfSignedCertificate({}, cert, key));
        QFile file(key);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray original = file.readAll();
        file.close();
        QString error;
        QVERIFY(!generateSelfSignedCertificate({}, cert, key, &error));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
    }
};

QTEST_GUILESS_MAIN(TestCertificate)
#include "tst_certificate.moc"
