// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/security/PasswordHash.h"

#include <QTest>

using namespace opentalk::security;

class TestPasswordHash : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QVERIFY(initializePasswordHashing()); }

    void hashAndVerify()
    {
        const auto hash = hashPassword(QStringLiteral("correct horse"), PasswordHashCost::minimumForTests());
        QVERIFY(hash.has_value());
        QVERIFY(hash->startsWith("$argon2id$"));
        QVERIFY(verifyPassword(*hash, QStringLiteral("correct horse")));
        QVERIFY(!verifyPassword(*hash, QStringLiteral("correct horsE")));
        QVERIFY(!verifyPassword(*hash, QString()));
    }

    void saltsDiffer()
    {
        const auto a = hashPassword(QStringLiteral("same"), PasswordHashCost::minimumForTests());
        const auto b = hashPassword(QStringLiteral("same"), PasswordHashCost::minimumForTests());
        QVERIFY(a && b);
        QVERIFY(*a != *b);
    }

    void unicodeIsNormalized()
    {
        // "é" typed as one code point and as "e" + combining accent must match.
        const QString composed = QStringLiteral("caf") + QChar(0x00E9);
        const QString decomposed = QStringLiteral("cafe") + QChar(0x0301);
        const auto hash = hashPassword(composed, PasswordHashCost::minimumForTests());
        QVERIFY(hash && verifyPassword(*hash, decomposed));
    }

    void rejectsEmptyAndOversized()
    {
        QVERIFY(!hashPassword(QString(), PasswordHashCost::minimumForTests()));
        QVERIFY(!hashPassword(QString(kMaxPasswordLength + 1, QLatin1Char('x')), PasswordHashCost::minimumForTests()));
        QVERIFY(hashPassword(QString(kMaxPasswordLength, QLatin1Char('x')), PasswordHashCost::minimumForTests()));
    }

    void garbageHashesNeverVerify()
    {
        QVERIFY(!verifyPassword(QByteArray(), QStringLiteral("x")));
        QVERIFY(!verifyPassword("not a hash", QStringLiteral("x")));
        QVERIFY(!verifyPassword(QByteArray(500, 'a'), QStringLiteral("x")));
    }

    void weakHashesNeedUpgrade()
    {
        const auto weak = hashPassword(QStringLiteral("pw"), PasswordHashCost::minimumForTests());
        QVERIFY(weak);
        QVERIFY(hashNeedsUpgrade(*weak, PasswordHashCost::interactive()));
        QVERIFY(!hashNeedsUpgrade(*weak, PasswordHashCost::minimumForTests()));
    }
};

QTEST_GUILESS_MAIN(TestPasswordHash)
#include "tst_passwordhash.moc"
