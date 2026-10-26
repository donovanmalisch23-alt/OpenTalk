// SPDX-License-Identifier: GPL-3.0-only
// Runs the real opentalk-server and opentalk-ping programs, the way an administrator
// and a user would, and checks the whole path: start-up output, TLS, pinning, the
// protocol handshake, and shutdown.

#include <QProcess>
#include <QRegularExpression>
#include <QTest>

namespace {

const QString kServer = QStringLiteral(OPENTALK_SERVER_EXECUTABLE);
const QString kPing = QStringLiteral(OPENTALK_PING_EXECUTABLE);
const QString kDataDir = QStringLiteral(OPENTALK_TEST_DATA_DIR);

} // namespace

class TestServerProcess : public QObject {
    Q_OBJECT

private:
    QProcess m_server;
    quint16 m_port = 0;
    QString m_fingerprint;

    /// Runs opentalk-ping and returns {exit code, stdout + stderr}.
    std::pair<int, QString> ping(const QStringList &extraArgs)
    {
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(kPing,
                      QStringList{QStringLiteral("127.0.0.1"), QStringLiteral("--port"), QString::number(m_port)} +
                          extraArgs);
        if (!process.waitForFinished(30'000)) {
            process.kill();
            return {-1, QStringLiteral("timed out")};
        }
        return {process.exitCode(), QString::fromUtf8(process.readAll())};
    }

private slots:
    void initTestCase()
    {
        m_server.start(kServer, {QStringLiteral("--listen"), QStringLiteral("127.0.0.1"), QStringLiteral("--port"),
                                 QStringLiteral("0"), QStringLiteral("--name"), QStringLiteral("Process Test"),
                                 QStringLiteral("--tls-cert"), kDataDir + QStringLiteral("/test-server.crt"),
                                 QStringLiteral("--tls-key"), kDataDir + QStringLiteral("/test-server.key")});
        QVERIFY2(m_server.waitForStarted(10'000), qPrintable(m_server.errorString()));

        // Wait until the server has printed its port and fingerprint.
        QString output;
        QTRY_VERIFY_WITH_TIMEOUT((output += QString::fromUtf8(m_server.readAllStandardOutput()),
                                  output.contains(QStringLiteral("Press Ctrl+C"))),
                                 15'000);
        const auto port = QRegularExpression(QStringLiteral("running on port (\\d+)")).match(output);
        const auto fingerprint = QRegularExpression(QStringLiteral("(SHA256:[0-9A-F:]+)")).match(output);
        QVERIFY2(port.hasMatch() && fingerprint.hasMatch(), qPrintable(output));
        m_port = static_cast<quint16>(port.captured(1).toUInt());
        m_fingerprint = fingerprint.captured(1);
    }

    void cleanupTestCase()
    {
        if (m_server.state() == QProcess::NotRunning) {
            return;
        }
#ifdef Q_OS_WIN
        // A console program cannot receive Ctrl+C from QProcess; graceful shutdown on
        // Windows is covered by manual testing (docs/server-administration/quick-start.md).
        m_server.kill();
        m_server.waitForFinished(10'000);
#else
        m_server.terminate(); // SIGTERM: must shut down gracefully
        QVERIFY(m_server.waitForFinished(10'000));
        QCOMPARE(m_server.exitStatus(), QProcess::NormalExit);
        QCOMPARE(m_server.exitCode(), 0);
        QVERIFY(QString::fromUtf8(m_server.readAllStandardError()).contains(QStringLiteral("Stopped")));
#endif
    }

    void pingWithPinnedFingerprintSucceeds()
    {
        const auto [code, output] = ping({QStringLiteral("--fingerprint"), m_fingerprint});
        QVERIFY2(code == 0, qPrintable(output));
        QVERIFY(output.contains(QStringLiteral("Connected to \"Process Test\"")));
        QVERIFY(output.contains(QStringLiteral("Round trip:")));
        QVERIFY2(!output.contains(QStringLiteral("opentalk.")), "log lines leaked without --verbose");
    }

    void pingWithoutPinShowsFingerprint()
    {
        const auto [code, output] = ping({});
        QCOMPARE(code, 1);
        QVERIFY(output.contains(m_fingerprint));
    }

    void pingWithWrongPinFails()
    {
        const auto [code, output] = ping({QStringLiteral("--fingerprint"), QString(64, QLatin1Char('0'))});
        QCOMPARE(code, 1);
        QVERIFY(output.contains(QStringLiteral("does not match")));
    }

    void serverRejectsBadArguments()
    {
        QProcess process;
        process.start(kServer, {QStringLiteral("--port"), QStringLiteral("70000")});
        QVERIFY(process.waitForFinished(10'000));
        QCOMPARE(process.exitCode(), 1);
    }
};

QTEST_GUILESS_MAIN(TestServerProcess)
#include "tst_server_process.moc"
