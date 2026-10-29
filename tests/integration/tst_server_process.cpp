// SPDX-License-Identifier: GPL-3.0-only
// Runs the real opentalk-server and opentalk-ping programs, the way an administrator
// and a user would, and checks the whole path: start-up output, TLS, pinning, the
// protocol handshake, and shutdown.

#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

namespace {

const QString kServer = QStringLiteral(OPENTALK_SERVER_EXECUTABLE);
const QString kPing = QStringLiteral(OPENTALK_PING_EXECUTABLE);
const QString kDataDir = QStringLiteral(OPENTALK_TEST_DATA_DIR);

} // namespace

class TestServerProcess : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dataDir;
    QProcess m_server;
    quint16 m_port = 0;
    QString m_fingerprint;

    /// Runs a one-off opentalk-server command against `dataDir`, feeding `input`;
    /// returns {exit code, output}.
    static std::pair<int, QString> runServer(const QString &dataDir, const QStringList &args,
                                             const QByteArray &input = {})
    {
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(kServer, QStringList{QStringLiteral("--data-dir"), dataDir} + args);
        if (!process.waitForStarted(10'000)) {
            return {-1, process.errorString()};
        }
        process.write(input);
        process.closeWriteChannel();
        if (!process.waitForFinished(30'000)) {
            process.kill();
            return {-1, QStringLiteral("timed out")};
        }
        return {process.exitCode(), QString::fromUtf8(process.readAll())};
    }

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
        QVERIFY(m_dataDir.isValid());
        m_server.start(kServer,
                       {QStringLiteral("--data-dir"), m_dataDir.path(), QStringLiteral("--listen"),
                        QStringLiteral("127.0.0.1"), QStringLiteral("--port"), QStringLiteral("0"),
                        QStringLiteral("--name"), QStringLiteral("Process Test"), QStringLiteral("--allow-guests"),
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

    void pingChecksTheVoicePath()
    {
        auto [code, output] = ping({QStringLiteral("--fingerprint"), m_fingerprint, QStringLiteral("--voice")});
        QVERIFY2(code == 0 && output.contains(QStringLiteral("Voice: UDP works")), qPrintable(output));

        // A server without UDP: reachable, but voice needs the fallback (exit code 3).
        QTemporaryDir dir;
        QProcess noUdp;
        noUdp.start(kServer,
                    {QStringLiteral("--data-dir"), dir.path(), QStringLiteral("--listen"), QStringLiteral("127.0.0.1"),
                     QStringLiteral("--port"), QStringLiteral("0"), QStringLiteral("--allow-guests"),
                     QStringLiteral("--no-udp"), QStringLiteral("--backup-every-hours"), QStringLiteral("0")});
        QVERIFY(noUdp.waitForStarted(10'000));
        QString startup;
        QTRY_VERIFY_WITH_TIMEOUT((startup += QString::fromUtf8(noUdp.readAllStandardOutput()),
                                  startup.contains(QStringLiteral("Press Ctrl+C"))),
                                 15'000);
        QVERIFY(startup.contains(QStringLiteral("Automatic backups are off.")));
        const quint16 port = static_cast<quint16>(
            QRegularExpression(QStringLiteral("running on port (\\d+)")).match(startup).captured(1).toUInt());
        const QString fingerprint =
            QRegularExpression(QStringLiteral("(SHA256:[0-9A-F:]+)")).match(startup).captured(1);
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(kPing, {QStringLiteral("127.0.0.1"), QStringLiteral("--port"), QString::number(port),
                              QStringLiteral("--fingerprint"), fingerprint, QStringLiteral("--voice")});
        QVERIFY(process.waitForFinished(30'000));
        output = QString::fromUtf8(process.readAll());
        noUdp.kill();
        noUdp.waitForFinished(10'000);
        QVERIFY2(process.exitCode() == 3 && output.contains(QStringLiteral("UDP does not get through")),
                 qPrintable(output));
        QVERIFY(output.contains(QStringLiteral("Round trip:")));
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

    void ownerRecoveryAndAuditCommands()
    {
        QTemporaryDir dir;
        // Runs an offline command against `dir`, feeding `input`; returns {exit code, output}.
        const auto run = [&dir](const QStringList &args, const QByteArray &input = {}) -> std::pair<int, QString> {
            QProcess process;
            process.setProcessChannelMode(QProcess::MergedChannels);
            process.start(kServer, QStringList{QStringLiteral("--data-dir"), dir.path()} + args);
            if (!process.waitForStarted(10'000)) {
                return {-1, process.errorString()};
            }
            process.write(input);
            process.closeWriteChannel();
            if (!process.waitForFinished(30'000)) {
                process.kill();
                return {-1, QStringLiteral("timed out")};
            }
            return {process.exitCode(), QString::fromUtf8(process.readAll())};
        };
        auto [code, output] = run({QStringLiteral("--create-account"), QStringLiteral("first"),
                                   QStringLiteral("--role"), QStringLiteral("owner")},
                                  "first password\n");
        QVERIFY2(code == 0, qPrintable(output));
        std::tie(code, output) = run({QStringLiteral("--create-account"), QStringLiteral("second"),
                                      QStringLiteral("--role"), QStringLiteral("owner")},
                                     "second password\n");
        QCOMPARE(code, 1); // one owner per server
        std::tie(code, output) =
            run({QStringLiteral("--create-account"), QStringLiteral("second")}, "second password\n");
        QVERIFY2(code == 0, qPrintable(output));

        // The owner is locked out: "second" takes over, and the change is audited.
        std::tie(code, output) = run({QStringLiteral("--recover-owner"), QStringLiteral("second")});
        QVERIFY2(code == 0, qPrintable(output));
        std::tie(code, output) = run({QStringLiteral("--list-accounts")});
        QVERIFY2(output.contains(QStringLiteral("second (owner)")), qPrintable(output));
        QVERIFY2(output.contains(QStringLiteral("first (admin)")), qPrintable(output));

        std::tie(code, output) = run({QStringLiteral("--verify-audit")});
        QVERIFY2(code == 0 && output.contains(QStringLiteral("intact")), qPrintable(output));
        const QString exported = dir.filePath(QStringLiteral("audit.txt"));
        std::tie(code, output) = run({QStringLiteral("--export-audit"), exported});
        QVERIFY2(code == 0, qPrintable(output));
        QFile file(exported);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(file.readAll());
        QVERIFY2(text.contains(QStringLiteral("owner.recovered")) && text.contains(QStringLiteral("chain intact")),
                 qPrintable(text));
    }

    void backupsWhileRunning()
    {
        // The running server made its first automatic backup at start-up.
        QTRY_VERIFY_WITH_TIMEOUT(runServer(m_dataDir.path(), {QStringLiteral("--list-backups")})
                                     .second.contains(QStringLiteral("opentalk-")),
                                 10'000);
        // A backup can be taken while it runs...
        QTemporaryDir out;
        const QString file = out.filePath(QStringLiteral("now.db"));
        auto [code, output] = runServer(m_dataDir.path(), {QStringLiteral("--backup"), file});
        QVERIFY2(code == 0 && QFile::exists(file), qPrintable(output));
        // ...but not restored, and no second server can use the same folder.
        std::tie(code, output) = runServer(m_dataDir.path(), {QStringLiteral("--restore"), file});
        QVERIFY2(code == 1 && output.contains(QStringLiteral("Stop it first")), qPrintable(output));
        std::tie(code, output) = runServer(m_dataDir.path(), {QStringLiteral("--listen"), QStringLiteral("127.0.0.1"),
                                                              QStringLiteral("--port"), QStringLiteral("0")});
        QVERIFY2(code == 1 && output.contains(QStringLiteral("is using")), qPrintable(output));
        std::tie(code, output) = runServer(m_dataDir.path(), {QStringLiteral("--recover-owner"), QStringLiteral("x")});
        QVERIFY2(code == 1 && output.contains(QStringLiteral("Stop it first")), qPrintable(output));
    }

    void restoreReplacesTheDatabaseAndKeepsACopy()
    {
        QTemporaryDir dir;
        auto [code, output] =
            runServer(dir.path(), {QStringLiteral("--create-account"), QStringLiteral("first")}, "first password\n");
        QVERIFY2(code == 0, qPrintable(output));
        const QString backup = dir.filePath(QStringLiteral("saved.db"));
        std::tie(code, output) = runServer(dir.path(), {QStringLiteral("--backup"), backup});
        QVERIFY2(code == 0, qPrintable(output));
        std::tie(code, output) =
            runServer(dir.path(), {QStringLiteral("--create-account"), QStringLiteral("second")}, "second password\n");
        QVERIFY2(code == 0, qPrintable(output));

        std::tie(code, output) = runServer(dir.path(), {QStringLiteral("--restore"), backup});
        QVERIFY2(code == 0 && output.contains(QStringLiteral("Restored")), qPrintable(output));
        std::tie(code, output) = runServer(dir.path(), {QStringLiteral("--list-accounts")});
        QVERIFY2(output.contains(QStringLiteral("first")) && !output.contains(QStringLiteral("second")),
                 qPrintable(output));
        // What was there before is kept, and is itself a usable backup.
        const QStringList kept =
            QDir(dir.filePath(QStringLiteral("backups"))).entryList({QStringLiteral("before-restore-*.db")});
        QCOMPARE(kept.size(), 1);
        std::tie(code, output) = runServer(
            dir.path(), {QStringLiteral("--restore"), dir.filePath(QStringLiteral("backups/") + kept.first())});
        QVERIFY2(code == 0, qPrintable(output));
        std::tie(code, output) = runServer(dir.path(), {QStringLiteral("--list-accounts")});
        QVERIFY2(output.contains(QStringLiteral("second")), qPrintable(output));

        // Something that is not a backup is refused and changes nothing.
        QFile junk(dir.filePath(QStringLiteral("junk.db")));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("junk");
        junk.close();
        std::tie(code, output) = runServer(dir.path(), {QStringLiteral("--restore"), junk.fileName()});
        QVERIFY2(code == 1 && output.contains(QStringLiteral("not an OpenTalk database")), qPrintable(output));
        std::tie(code, output) = runServer(dir.path(), {QStringLiteral("--list-accounts")});
        QVERIFY2(output.contains(QStringLiteral("second")), qPrintable(output));
    }

    void serverRejectsBadArguments()
    {
        QProcess process;
        process.start(kServer, {QStringLiteral("--data-dir"), m_dataDir.path(), QStringLiteral("--port"),
                                QStringLiteral("70000")});
        QVERIFY(process.waitForFinished(10'000));
        QCOMPARE(process.exitCode(), 1);
    }
};

QTEST_GUILESS_MAIN(TestServerProcess)
#include "tst_server_process.moc"
