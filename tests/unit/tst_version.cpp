// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/protocol/Version.h"

#include <QTest>

using namespace opentalk::protocol;

class TestVersion : public QObject {
    Q_OBJECT

private slots:
    void picksHighestCommonVersion()
    {
        QCOMPARE(negotiateVersion({1, 3}, {2, 5}), std::optional<quint16>(3));
        QCOMPARE(negotiateVersion({2, 5}, {1, 3}), std::optional<quint16>(3));
        QCOMPARE(negotiateVersion({1, 1}, {1, 1}), std::optional<quint16>(1));
    }

    void noOverlapFails() { QCOMPARE(negotiateVersion({1, 2}, {3, 4}), std::nullopt); }

    void malformedRangesFail()
    {
        QCOMPARE(negotiateVersion({0, 1}, {1, 1}), std::nullopt);
        QCOMPARE(negotiateVersion({1, 1}, {3, 2}), std::nullopt);
    }

    void thisBuildSupportsVersionOne()
    {
        QVERIFY(kSupportedVersions.isValid());
        QCOMPARE(negotiateVersion(kSupportedVersions, {1, 1}), std::optional<quint16>(1));
    }
};

QTEST_GUILESS_MAIN(TestVersion)
#include "tst_version.moc"
