// SPDX-License-Identifier: GPL-3.0-only
// "Remember my password" in the real connect dialog and main window, against a real
// server: the password is kept only after the server accepted it, is filled in next
// time, and is forgotten when the box is unticked or the saved server is deleted. The
// credential store is an in-memory stand-in for Windows Credential Manager.

#include "UiTestUtil.h"

#include "opentalk/client/Connection.h"
#include "opentalk/security/PasswordHash.h"
#include "opentalk/security/Tls.h"
#include "opentalk/server/Server.h"
#include "opentalk/server/ServerDatabase.h"
#include "opentalk/storage/AppPaths.h"
#include "opentalk/storage/ClientSettings.h"
#include "opentalk/storage/CredentialStore.h"
#include "opentalk/storage/ServerProfiles.h"
#include "opentalk/storage/SettingsStore.h"
#include "opentalk/ui/ConnectDialog.h"
#include "opentalk/ui/MainWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>

using namespace opentalk;
using namespace opentalk::test;

namespace {

const QString kDataDir = QStringLiteral(OPENTALK_TEST_DATA_DIR);
constexpr int kWaitMs = 10'000;

QCheckBox *rememberBox(QWidget *dialog)
{
    for (QCheckBox *box : dialog->findChildren<QCheckBox *>()) {
        if (box->text() == QStringLiteral("&Remember my password")) {
            return box;
        }
    }
    return nullptr;
}

} // namespace

class TestRememberPassword : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    std::unique_ptr<server::ServerDatabase> m_db;
    std::unique_ptr<server::Server> m_server;
    std::unique_ptr<storage::AppPaths> m_paths;
    std::unique_ptr<storage::SettingsStore> m_settings;
    std::unique_ptr<storage::ServerProfileStore> m_profiles;
    std::unique_ptr<ui::MainWindow> m_window;
    storage::MemoryCredentialStore *m_store = nullptr;
    QStringList m_errors;

    /// Answers the connect dialog: a new server, or the saved one, logging in as alice.
    void answerConnectDialog(const QString &password, bool remember, bool useSaved = false)
    {
        const quint16 port = m_server->serverPort();
        whenDialogOpens<ui::ConnectDialog>([=](ui::ConnectDialog *dialog) {
            if (useSaved) {
                dialog->savedServers()->setCurrentRow(1); // row 0 is "New server"
            } else {
                named<QLineEdit>(dialog, QStringLiteral("Address"))->setText(QStringLiteral("127.0.0.1"));
                named<QSpinBox>(dialog, QStringLiteral("Port"))->setValue(port);
                named<QComboBox>(dialog, QStringLiteral("Log in as"))->setCurrentIndex(1);
                named<QLineEdit>(dialog, QStringLiteral("Username"))->setText(QStringLiteral("alice"));
            }
            if (!password.isNull()) {
                named<QLineEdit>(dialog, QStringLiteral("Password"))->setText(password);
            }
            rememberBox(dialog)->setChecked(remember);
            dialog->accept();
        });
    }

    bool waitConnected()
    {
        return QTest::qWaitFor([this] { return m_window->connection().state() == client::ConnectionState::Connected; },
                               kWaitMs);
    }

private slots:
    void initTestCase()
    {
        qRegisterMetaType<client::DisconnectInfo>();
        QString error;
        QVERIFY2(security::initializeTls(&error), qPrintable(error));
        QVERIFY(security::initializePasswordHashing());
        const auto tls = security::serverTlsConfiguration(kDataDir + QStringLiteral("/test-server.crt"),
                                                          kDataDir + QStringLiteral("/test-server.key"), &error);
        QVERIFY2(tls.has_value(), qPrintable(error));
        m_db = std::make_unique<server::ServerDatabase>();
        QVERIFY(m_db->open(m_dir.filePath(QStringLiteral("server.db"))));
        const auto hash =
            security::hashPassword(QStringLiteral("right-password"), security::PasswordHashCost::minimumForTests());
        QVERIFY(hash && m_db->createAccount(QStringLiteral("alice"), *hash, protocol::Role::User));
        server::ServerConfig config;
        config.address = QHostAddress::LocalHost;
        config.port = 0;
        config.tls = *tls;
        config.session.serverName = QStringLiteral("Password test");
        config.database = m_db.get();
        config.passwordCost = security::PasswordHashCost::minimumForTests();
        config.shutdownGraceMs = 1'000;
        m_server = std::make_unique<server::Server>(config);
        QVERIFY2(m_server->start(&error), qPrintable(error));

        m_paths =
            std::make_unique<storage::AppPaths>(storage::AppPaths::resolve({}, m_dir.filePath(QStringLiteral("app"))));
        QVERIFY(m_paths->ensureDirectories());
        m_settings = std::make_unique<storage::SettingsStore>(storage::client::schema(), m_paths->clientSettingsFile());
        m_settings->load();
        m_profiles = std::make_unique<storage::ServerProfileStore>(m_paths->serverProfilesFile());
        m_window = std::make_unique<ui::MainWindow>(*m_settings, *m_paths, *m_profiles,
                                                    std::make_unique<audio::SyntheticAudioBackend>());
        auto store = std::make_unique<storage::MemoryCredentialStore>();
        m_store = store.get();
        m_window->setCredentialStore(std::move(store));
        m_window->setTrustPrompt([](const ui::MainWindow::TrustQuestion &) { return true; });
        m_window->setErrorReporter([this](const QString &, const QString &text) { m_errors.append(text); });
        m_window->show();
    }

    void cleanupTestCase()
    {
        m_window.reset();
        QSignalSpy stopped(m_server.get(), &server::Server::stopped);
        m_server->stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, kWaitMs);
    }

    void aWrongPasswordIsNeverRemembered()
    {
        answerConnectDialog(QStringLiteral("wrong-password"), true);
        m_window->openConnectDialog();
        QTRY_VERIFY_WITH_TIMEOUT(!m_errors.isEmpty(), kWaitMs); // "Could not log in"
        QCOMPARE(m_store->size(), 0);
        m_errors.clear();
    }

    void rememberedAfterLoggingIn()
    {
        answerConnectDialog(QStringLiteral("right-password"), true, true);
        m_window->openConnectDialog();
        QVERIFY(waitConnected());
        QCOMPARE(m_profiles->profiles().size(), 1);
        const storage::ServerProfile profile = m_profiles->profiles().first();
        QVERIFY(profile.rememberPassword);
        QCOMPARE(m_store->readPassword(profile.id).value_or(QString()), QStringLiteral("right-password"));
        m_window->disconnectFromServer();
        QTRY_VERIFY_WITH_TIMEOUT(m_window->connection().state() == client::ConnectionState::Disconnected, kWaitMs);
    }

    void filledInNextTime()
    {
        ui::ConnectDialog dialog(*m_profiles, m_store);
        dialog.savedServers()->setCurrentRow(1);
        QCOMPARE(named<QLineEdit>(&dialog, QStringLiteral("Password"))->text(), QStringLiteral("right-password"));
        QCheckBox *remember = rememberBox(&dialog);
        QVERIFY(remember->isVisible() || !dialog.isVisible());
        QVERIFY(remember->isChecked());
        QVERIFY(remember->accessibleDescription().contains(QStringLiteral("never in OpenTalk's own files"),
                                                           Qt::CaseInsensitive));
        // Guests have no password to remember.
        named<QComboBox>(&dialog, QStringLiteral("Log in as"))->setCurrentIndex(0);
        QVERIFY(!remember->isEnabled());
    }

    void forgottenWhenUnticked()
    {
        answerConnectDialog(QString(), false, true); // keep the filled-in password, untick the box
        m_window->openConnectDialog();
        QVERIFY(waitConnected());
        QCOMPARE(m_store->size(), 0);
        QVERIFY(!m_profiles->profiles().first().rememberPassword);
        m_window->disconnectFromServer();
        QTRY_VERIFY_WITH_TIMEOUT(m_window->connection().state() == client::ConnectionState::Disconnected, kWaitMs);
    }

    void forgottenWhenTheServerIsDeleted()
    {
        const QString id = m_profiles->profiles().first().id;
        QVERIFY(m_store->writePassword(id, QStringLiteral("alice"), QStringLiteral("right-password"), nullptr));
        ui::ConnectDialog dialog(*m_profiles, m_store);
        dialog.show();
        dialog.savedServers()->setCurrentRow(1);
        whenDialogOpens<QMessageBox>([](QMessageBox *box) { box->button(QMessageBox::Yes)->click(); });
        QPushButton *remove = buttonWithText(&dialog, QStringLiteral("&Delete saved server"));
        QVERIFY(remove);
        remove->click();
        QTRY_VERIFY_WITH_TIMEOUT(m_profiles->profiles().isEmpty(), kWaitMs);
        QCOMPARE(m_store->size(), 0);
    }
};

QTEST_MAIN(TestRememberPassword)
#include "tst_remember_password.moc"
