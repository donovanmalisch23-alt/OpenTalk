// SPDX-License-Identifier: GPL-3.0-only
// Moderation in the real desktop windows against a real in-process server: which
// actions each role is offered, warning, muting and kicking from the window, reports,
// appeals, the administration dialog, and the accessibility of every moderation dialog.
// Modal dialogs are answered by a timer that finds them, as a person would.

#include "../unit/AudioTestUtil.h"
#include "UiTestUtil.h"

#include "opentalk/client/Connection.h"
#include "opentalk/moderation/Appeals.h"
#include "opentalk/security/PasswordHash.h"
#include "opentalk/security/Tls.h"
#include "opentalk/server/Server.h"
#include "opentalk/server/ServerDatabase.h"
#include "opentalk/storage/AppPaths.h"
#include "opentalk/storage/ClientSettings.h"
#include "opentalk/storage/ServerProfiles.h"
#include "opentalk/storage/SettingsStore.h"
#include "opentalk/ui/AdminDialog.h"
#include "opentalk/ui/ChannelTree.h"
#include "opentalk/ui/MainWindow.h"
#include "opentalk/ui/ModerationDialogs.h"

#include <QAbstractSpinBox>
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

using namespace opentalk;
using protocol::Role;
using Page = ui::AdminDialog::Page;
using namespace opentalk::test;

namespace {

const QString kDataDir = QStringLiteral(OPENTALK_TEST_DATA_DIR);
constexpr int kWaitMs = 10'000;

QStringList treeTexts(QTreeWidget *tree)
{
    QStringList texts;
    for (QTreeWidgetItemIterator it(tree); *it != nullptr; ++it) {
        texts << (*it)->text(0);
    }
    return texts;
}

QStringList listTexts(QTreeWidget *list)
{
    QStringList texts;
    for (int i = 0; i < list->topLevelItemCount(); ++i) {
        texts << list->topLevelItem(i)->data(0, Qt::AccessibleTextRole).toString();
    }
    return texts;
}

/// One person's desktop app, logged in to the test server.
struct App {
    QTemporaryDir dir;
    std::unique_ptr<storage::AppPaths> paths;
    std::unique_ptr<storage::SettingsStore> settings;
    std::unique_ptr<storage::ServerProfileStore> profiles;
    std::unique_ptr<ui::MainWindow> window;
    QStringList errors;

    ~App()
    {
        if (window) {
            window->connection().disconnectFromServer();
        }
    }

    QAction *action(const char *name) const { return window->findChild<QAction *>(QLatin1String(name)); }
    QString history() const { return window->historyText(); }
    const client::ClientState &state() const { return window->connection().serverState(); }

    /// Selects another user in the channel tree, as arrowing to them would.
    bool selectUser(const QString &name)
    {
        QTreeWidget *tree = window->channelTree();
        for (QTreeWidgetItemIterator it(tree); *it != nullptr; ++it) {
            const QString text = (*it)->text(0);
            if (text == name || text.startsWith(name + QLatin1Char(','))) {
                tree->setCurrentItem(*it);
                return window->channelTree()->currentUserId().has_value();
            }
        }
        return false;
    }
};

} // namespace

class TestModerationUi : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QSslConfiguration m_tls;
    std::unique_ptr<server::ServerDatabase> m_db;
    std::unique_ptr<server::Server> m_server;

    /// Opens a window and logs in, waiting until connected (null if that fails).
    std::unique_ptr<App> login(const QString &account)
    {
        auto app = open(account);
        App *raw = app.get();
        if (!app ||
            !QTest::qWaitFor([raw] { return raw->window->connection().state() == client::ConnectionState::Connected; },
                             kWaitMs)) {
            return nullptr;
        }
        return app;
    }

    /// Opens a window and starts logging in.
    std::unique_ptr<App> open(const QString &account)
    {
        auto app = std::make_unique<App>();
        app->paths = std::make_unique<storage::AppPaths>(storage::AppPaths::resolve({}, app->dir.path()));
        if (!app->paths->ensureDirectories()) {
            return nullptr;
        }
        app->settings =
            std::make_unique<storage::SettingsStore>(storage::client::schema(), app->paths->clientSettingsFile());
        app->settings->load();
        app->profiles = std::make_unique<storage::ServerProfileStore>(app->paths->serverProfilesFile());
        app->window = std::make_unique<ui::MainWindow>(*app->settings, *app->paths, *app->profiles,
                                                       std::make_unique<audio::SyntheticAudioBackend>());
        App *raw = app.get();
        app->window->setErrorReporter([raw](const QString &title, const QString &text) {
            raw->errors.append(title + QStringLiteral(": ") + text);
        });
        app->window->show();
        storage::ServerProfile profile;
        profile.name = QStringLiteral("Moderation test");
        profile.host = QStringLiteral("127.0.0.1");
        profile.port = m_server->serverPort();
        profile.userName = account;
        profile.guest = false;
        profile.pinnedFingerprint = m_server->certificateFingerprint();
        app->window->connectToServer(profile, account + QStringLiteral("-password"), false);
        return app;
    }

    static void waitIdle(const ui::AdminDialog &dialog)
    {
        QTRY_COMPARE_WITH_TIMEOUT(dialog.pendingRequests(), 0, kWaitMs);
    }

    static void giveSanction(App &moderator, const char *action, const QString &reason)
    {
        whenDialogOpens<ui::SanctionDialog>([reason](ui::SanctionDialog *dialog) {
            named<QLineEdit>(dialog, QStringLiteral("Reason"))->setText(reason);
            dialog->accept();
        });
        moderator.action(action)->trigger();
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
        m_tls = *tls;
    }

    void init()
    {
        m_db = std::make_unique<server::ServerDatabase>();
        QVERIFY(m_db->open(
            m_dir.filePath(QStringLiteral("mod-%1.db").arg(QString::fromLatin1(QTest::currentTestFunction())))));
        for (const auto &[name, role] :
             {std::pair{QStringLiteral("boss"), Role::Owner}, std::pair{QStringLiteral("mod"), Role::Moderator},
              std::pair{QStringLiteral("alice"), Role::User}}) {
            const auto hash = security::hashPassword(name + QStringLiteral("-password"),
                                                     security::PasswordHashCost::minimumForTests());
            QVERIFY(hash && m_db->createAccount(name, *hash, role));
        }
        server::ServerConfig config;
        config.address = QHostAddress::LocalHost;
        config.port = 0;
        config.tls = m_tls;
        config.session.serverName = QStringLiteral("Moderation test");
        config.allowGuests = true;
        config.database = m_db.get();
        config.passwordCost = security::PasswordHashCost::minimumForTests();
        config.shutdownGraceMs = 1'000;
        m_server = std::make_unique<server::Server>(config);
        QString error;
        QVERIFY2(m_server->start(&error), qPrintable(error));
    }

    void cleanup()
    {
        if (m_server) {
            QSignalSpy stopped(m_server.get(), &server::Server::stopped);
            m_server->stop();
            QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, kWaitMs);
        }
        m_server.reset();
        m_db.reset();
    }

    void actionsFollowPermissions()
    {
        auto boss = login(QStringLiteral("boss"));
        auto alice = login(QStringLiteral("alice"));
        QVERIFY(boss && alice);
        QTRY_VERIFY_WITH_TIMEOUT(boss->selectUser(QStringLiteral("alice")), kWaitMs);
        for (const char *name : {"warnAction", "muteUserAction", "kickAction", "banAction", "moveUserAction",
                                 "adminAction", "broadcastAction"}) {
            QVERIFY2(boss->action(name)->isVisible() && boss->action(name)->isEnabled(), name);
        }
        QVERIFY(boss->action("editChannelAction")->isVisible());
        // Owners are labelled; registered users are not.
        QVERIFY(treeTexts(boss->window->channelTree()).contains(QStringLiteral("boss (you), owner")));
        QVERIFY(treeTexts(boss->window->channelTree()).contains(QStringLiteral("alice")));

        // A registered user sees only what they can do: report, and their own sanctions.
        QVERIFY(alice->selectUser(QStringLiteral("boss")));
        for (const char *name : {"warnAction", "muteUserAction", "kickAction", "banAction", "moveUserAction",
                                 "adminAction", "broadcastAction"}) {
            QVERIFY2(!alice->action(name)->isVisible(), name);
        }
        QVERIFY(alice->action("reportAction")->isEnabled());
        QVERIFY(alice->action("mySanctionsAction")->isEnabled());
        QVERIFY(!alice->action("deleteChannelAction")->isEnabled());
    }

    void warnMuteAndKickFromTheWindow()
    {
        auto boss = login(QStringLiteral("boss"));
        auto alice = login(QStringLiteral("alice"));
        QVERIFY(boss && alice);
        QTRY_VERIFY_WITH_TIMEOUT(boss->selectUser(QStringLiteral("alice")), kWaitMs);

        giveSanction(*boss, "warnAction", QStringLiteral("be nice"));
        QTRY_VERIFY_WITH_TIMEOUT(boss->history().contains(QStringLiteral("You warned alice.")), kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(alice->history().contains(QStringLiteral("Warning, from boss. Reason: be nice.")),
                                 kWaitMs);

        giveSanction(*boss, "muteUserAction", QStringLiteral("shouting"));
        QTRY_VERIFY_WITH_TIMEOUT(alice->history().contains(QStringLiteral("Reason: shouting.")), kWaitMs);
        // Everyone sees the mute in the tree; alice sees it on herself.
        QTRY_VERIFY_WITH_TIMEOUT(treeTexts(boss->window->channelTree()).contains(QStringLiteral("alice, muted")),
                                 kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(treeTexts(alice->window->channelTree()).contains(QStringLiteral("alice (you), muted")),
                                 kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(boss->history().contains(QStringLiteral("alice was muted.")), kWaitMs);

        // A muted person's messages are refused, with a clear reason.
        auto *input = qobject_cast<QLineEdit *>(alice->window->paneWidget(ui::MainWindow::Pane::MessageInput));
        input->setText(QStringLiteral("let me talk"));
        QTest::keyClick(input, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(alice->window->statusText().startsWith(QStringLiteral("Message not sent")), kWaitMs);

        giveSanction(*boss, "kickAction", QStringLiteral("cool down"));
        QTRY_COMPARE_WITH_TIMEOUT(alice->errors.size(), 1, kWaitMs);
        QCOMPARE(alice->errors.first(),
                 QStringLiteral("Kicked: Disconnected from Moderation test. You were kicked: cool down."));
        QTest::qWait(300);
        QVERIFY(alice->window->connection().state() == client::ConnectionState::Disconnected); // no reconnect
    }

    void bansAreExplainedAndHold()
    {
        auto boss = login(QStringLiteral("boss"));
        auto alice = login(QStringLiteral("alice"));
        QVERIFY(boss && alice);
        QTRY_VERIFY_WITH_TIMEOUT(boss->selectUser(QStringLiteral("alice")), kWaitMs);
        giveSanction(*boss, "banAction", QStringLiteral("spam links"));
        QTRY_COMPARE_WITH_TIMEOUT(alice->errors.size(), 1, kWaitMs);
        QVERIFY2(alice->errors.first().startsWith(QStringLiteral(
                     "Banned: Disconnected from Moderation test. You are banned from this server until")),
                 qPrintable(alice->errors.first()));
        QVERIFY(alice->errors.first().contains(QStringLiteral("Reason: spam links.")));
        QTRY_VERIFY_WITH_TIMEOUT(boss->history().contains(QStringLiteral("You banned alice.")), kWaitMs);

        // Logging in again is refused with the same explanation, and the app does not retry.
        auto retry = open(QStringLiteral("alice"));
        QTRY_COMPARE_WITH_TIMEOUT(retry->errors.size(), 1, kWaitMs);
        QVERIFY2(retry->errors.first().contains(QStringLiteral("You are banned from this server until")),
                 qPrintable(retry->errors.first()));
        QTest::qWait(1500);
        QVERIFY(retry->window->connection().state() == client::ConnectionState::Disconnected);
        QCOMPARE(m_server->onlineUserCount(), 1);
    }

    void reportsReachModeratorsAndAreResolved()
    {
        auto boss = login(QStringLiteral("boss"));
        auto alice = login(QStringLiteral("alice"));
        QVERIFY(boss && alice);
        QTRY_VERIFY_WITH_TIMEOUT(alice->selectUser(QStringLiteral("boss")), kWaitMs);
        whenDialogOpens<ui::ReportDialog>([](ui::ReportDialog *dialog) {
            named<QLineEdit>(dialog, QStringLiteral("What happened"))->setText(QStringLiteral("rude"));
            dialog->accept();
        });
        alice->action("reportAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(
            alice->history().contains(QStringLiteral("Your report about boss was sent to the moderators.")), kWaitMs);

        ui::AdminDialog admin(boss->window->connection());
        admin.show();
        QVERIFY(admin.showPage(Page::Reports));
        waitIdle(admin);
        auto *reports = named<QTreeWidget>(admin.pageWidget(Page::Reports), QStringLiteral("Reports"));
        QVERIFY(reports != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(listTexts(reports).join(QLatin1Char('\n')).contains(QStringLiteral("rude")), kWaitMs);
        QVERIFY(listTexts(reports).first().contains(QStringLiteral("Reported by: alice")));
        reports->setCurrentItem(reports->topLevelItem(0));
        whenDialogOpens<QInputDialog>([](QInputDialog *dialog) {
            dialog->setTextValue(QStringLiteral("talked to them"));
            dialog->accept();
        });
        buttonWithText(admin.pageWidget(Page::Reports), QStringLiteral("Mark as &handled..."))->click();
        QTRY_COMPARE_WITH_TIMEOUT(admin.statusText(), QStringLiteral("The report was marked as handled."), kWaitMs);
        waitIdle(admin);
        QCOMPARE(listTexts(reports), QStringList{QStringLiteral("No open reports")});
    }

    void appealsAreDecidedBySomeoneElse()
    {
        auto boss = login(QStringLiteral("boss"));
        auto mod = login(QStringLiteral("mod"));
        auto alice = login(QStringLiteral("alice"));
        QVERIFY(boss && mod && alice);
        QTRY_VERIFY_WITH_TIMEOUT(mod->selectUser(QStringLiteral("alice")), kWaitMs);
        giveSanction(*mod, "warnAction", QStringLiteral("spam"));
        QTRY_VERIFY_WITH_TIMEOUT(alice->history().contains(QStringLiteral("Reason: spam.")), kWaitMs);

        // Alice appeals from her own list.
        ui::MySanctionsDialog mine(alice->window->connection());
        mine.show();
        auto *sanctions = named<QTreeWidget>(&mine, QStringLiteral("Sanctions on you"));
        QTRY_VERIFY_WITH_TIMEOUT(sanctions->topLevelItemCount() == 1 &&
                                     sanctions->topLevelItem(0)->text(1) == QStringLiteral("spam"),
                                 kWaitMs);
        QVERIFY(unnamedFocusableWidgets(&mine).isEmpty());
        whenDialogOpens<QInputDialog>([](QInputDialog *dialog) {
            dialog->setTextValue(QStringLiteral("it was a joke"));
            dialog->accept();
        });
        buttonWithText(&mine, QStringLiteral("&Appeal the selected sanction..."))->click();
        auto *appeals = named<QTreeWidget>(&mine, QStringLiteral("Your appeals"));
        QTRY_VERIFY_WITH_TIMEOUT(appeals->topLevelItem(0)->text(0) == QStringLiteral("it was a joke"), kWaitMs);

        // The owner, not the moderator who warned, decides it.
        ui::AdminDialog admin(boss->window->connection());
        admin.show();
        QVERIFY(admin.showPage(Page::Appeals));
        waitIdle(admin);
        auto *list = named<QTreeWidget>(admin.pageWidget(Page::Appeals), QStringLiteral("Appeals"));
        QTRY_COMPARE_WITH_TIMEOUT(list->topLevelItem(0)->text(0), QStringLiteral("alice"), kWaitMs);
        QVERIFY(listTexts(list).first().contains(QStringLiteral("Warning by mod, reason: spam")));
        list->setCurrentItem(list->topLevelItem(0));
        whenDialogOpens<ui::AppealDecisionDialog>([](ui::AppealDecisionDialog *dialog) {
            auto *decision = named<QComboBox>(dialog, QStringLiteral("Decision"));
            decision->setCurrentIndex(decision->count() - 1); // overturn (warnings cannot be reduced)
            named<QLineEdit>(dialog, QStringLiteral("Reason"))->setText(QStringLiteral("fair point"));
            QVERIFY(unnamedFocusableWidgets(dialog).isEmpty());
            dialog->accept();
        });
        buttonWithText(admin.pageWidget(Page::Appeals), QStringLiteral("&Decide..."))->click();
        QTRY_VERIFY_WITH_TIMEOUT(admin.statusText().startsWith(QStringLiteral("The appeal from alice was decided")),
                                 kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(alice->history().contains(QStringLiteral("Your warning was overturned on appeal.")),
                                 kWaitMs);
    }

    void administrationShowsOnlyAllowedPagesAndIsAccessible()
    {
        auto boss = login(QStringLiteral("boss"));
        auto mod = login(QStringLiteral("mod"));
        QVERIFY(boss && mod);

        ui::AdminDialog admin(boss->window->connection());
        admin.show();
        QCOMPARE(admin.pages().size(), 9);
        for (Page page : admin.pages()) {
            QVERIFY(admin.showPage(page));
            waitIdle(admin);
            const QStringList problems = unnamedFocusableWidgets(&admin);
            QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral(", "))));
        }
        auto *accounts = named<QTreeWidget>(admin.pageWidget(Page::Accounts), QStringLiteral("Accounts"));
        QCOMPARE(accounts->topLevelItemCount(), 3);
        QVERIFY(listTexts(accounts).contains(QStringLiteral("mod, Role: Moderator, State: active")));
        auto *stats = named<QListWidget>(admin.pageWidget(Page::Statistics), QStringLiteral("Server statistics"));
        QVERIFY(!stats->findItems(QStringLiteral("People online: 2"), Qt::MatchExactly).isEmpty());
        auto *audit = named<QTreeWidget>(admin.pageWidget(Page::Audit), QStringLiteral("Audit log"));
        QVERIFY(audit->topLevelItemCount() > 0);

        // A moderator gets the moderation pages only.
        ui::AdminDialog modAdmin(mod->window->connection());
        QCOMPARE(modAdmin.pages(),
                 (QList<Page>{Page::Sanctions, Page::Reports, Page::Appeals, Page::Audit, Page::Statistics}));
    }

    void settingsRolesAndRulesThroughTheDialog()
    {
        auto boss = login(QStringLiteral("boss"));
        QVERIFY(boss);
        ui::AdminDialog admin(boss->window->connection());
        admin.show();

        // Settings: a welcome message for everyone who logs in after.
        QVERIFY(admin.showPage(Page::Settings));
        waitIdle(admin);
        QWidget *settings = admin.pageWidget(Page::Settings);
        named<QLineEdit>(settings, QStringLiteral("Welcome message"))->setText(QStringLiteral("Hello and welcome!"));
        buttonWithText(settings, QStringLiteral("&Save settings"))->click();
        QTRY_COMPARE_WITH_TIMEOUT(admin.statusText(), QStringLiteral("The server settings were saved."), kWaitMs);

        // Roles: a "Greeter" who may warn.
        QVERIFY(admin.showPage(Page::Roles));
        whenDialogOpens<ui::RoleDialog>([](ui::RoleDialog *dialog) {
            named<QLineEdit>(dialog, QStringLiteral("Role name"))->setText(QStringLiteral("Greeter"));
            QListWidget *caps = dialog->capabilityList();
            for (int i = 0; i < caps->count(); ++i) {
                if (caps->item(i)->text().endsWith(QStringLiteral("(user.warn)"))) {
                    caps->item(i)->setCheckState(Qt::Checked);
                }
            }
            QVERIFY(unnamedFocusableWidgets(dialog).isEmpty());
            dialog->accept();
        });
        buttonWithText(admin.pageWidget(Page::Roles), QStringLiteral("&New role..."))->click();
        QTRY_COMPARE_WITH_TIMEOUT(admin.statusText(), QStringLiteral("Role Greeter was created."), kWaitMs);

        // Accounts: alice becomes a greeter, and her window offers "Warn" at once.
        auto alice = login(QStringLiteral("alice"));
        QVERIFY(alice);
        QTRY_VERIFY_WITH_TIMEOUT(alice->history().contains(QStringLiteral("Server: Hello and welcome!")), kWaitMs);
        QVERIFY(!alice->action("warnAction")->isVisible());
        QVERIFY(admin.showPage(Page::Accounts));
        waitIdle(admin);
        auto *accounts = named<QTreeWidget>(admin.pageWidget(Page::Accounts), QStringLiteral("Accounts"));
        accounts->setCurrentItem(accounts->findItems(QStringLiteral("alice"), Qt::MatchExactly).value(0));
        whenDialogOpens<QInputDialog>([](QInputDialog *dialog) {
            dialog->setTextValue(QStringLiteral("Greeter"));
            dialog->accept();
        });
        buttonWithText(admin.pageWidget(Page::Accounts), QStringLiteral("Change &role..."))->click();
        QTRY_COMPARE_WITH_TIMEOUT(admin.statusText(), QStringLiteral("alice is now Greeter."), kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(alice->history().contains(QStringLiteral("You are now Greeter.")), kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(alice->action("warnAction")->isVisible(), kWaitMs);

        // Channel permissions: nobody may talk in the root channel except alice.
        QVERIFY(admin.showPage(Page::Rules));
        waitIdle(admin);
        whenDialogOpens<ui::RuleDialog>([](ui::RuleDialog *dialog) {
            auto *capability = named<QComboBox>(dialog, QStringLiteral("Permission"));
            capability->setCurrentIndex(capability->findText(QStringLiteral("voice.transmit")));
            auto *role = named<QComboBox>(dialog, QStringLiteral("Role"));
            role->setCurrentIndex(role->findText(QStringLiteral("Registered user")));
            auto *effect = named<QComboBox>(dialog, QStringLiteral("Effect"));
            effect->setCurrentIndex(1); // deny
            QVERIFY(unnamedFocusableWidgets(dialog).isEmpty());
            dialog->accept();
        });
        buttonWithText(admin.pageWidget(Page::Rules), QStringLiteral("&Add rule..."))->click();
        QTRY_COMPARE_WITH_TIMEOUT(admin.statusText(), QStringLiteral("The rule was added."), kWaitMs);
        waitIdle(admin);
        auto *rules = named<QTreeWidget>(admin.pageWidget(Page::Rules), QStringLiteral("Permission rules"));
        QCOMPARE(listTexts(rules),
                 QStringList{QStringLiteral(
                     "the whole server, For: role Registered user, Permission: voice.transmit, Effect: deny")});
    }

    void userLabelsSayRolesMutesAndBots()
    {
        QCOMPARE(ui::ChannelTree::userLabel(QStringLiteral("dj"), false, QString(), protocol::BotFlag),
                 QStringLiteral("dj, bot"));
        QCOMPARE(ui::ChannelTree::userLabel(QStringLiteral("me"), true, QStringLiteral("moderator"),
                                            protocol::VoiceMutedFlag | protocol::TextMutedFlag),
                 QStringLiteral("me (you), moderator, muted"));
        QCOMPARE(ui::ChannelTree::userLabel(QStringLiteral("x"), false, QString(),
                                            protocol::TextMutedFlag | protocol::RestrictedFlag),
                 QStringLiteral("x, text muted, restricted"));
        QCOMPARE(ui::ChannelTree::userLabel(QStringLiteral("al"), false, QString(), 0, QStringLiteral("back at 5")),
                 QStringLiteral("al, status: back at 5"));
    }

    void statusMessagesAreShownToOthers()
    {
        auto boss = login(QStringLiteral("boss"));
        auto alice = login(QStringLiteral("alice"));
        QVERIFY(boss && alice);
        QAction *status = alice->action("statusAction");
        QVERIFY(status && status->isEnabled());
        whenDialogOpens<QInputDialog>([](QInputDialog *dialog) {
            dialog->setTextValue(QStringLiteral("back at 5"));
            dialog->accept();
        });
        status->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(
            treeTexts(boss->window->channelTree()).contains(QStringLiteral("alice, status: back at 5")), kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(boss->history().contains(QStringLiteral("alice's status: back at 5")), kWaitMs);
        QTRY_VERIFY_WITH_TIMEOUT(alice->window->statusText() == QStringLiteral("Your status is now: back at 5"),
                                 kWaitMs);
        // Cleared again.
        whenDialogOpens<QInputDialog>([](QInputDialog *dialog) {
            QCOMPARE(dialog->textValue(), QStringLiteral("back at 5")); // the dialog starts with the current one
            dialog->setTextValue(QString());
            dialog->accept();
        });
        status->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(boss->history().contains(QStringLiteral("alice cleared their status.")), kWaitMs);
        // The channel list refreshes shortly after a change, not at once.
        QTRY_VERIFY_WITH_TIMEOUT(treeTexts(boss->window->channelTree()).contains(QStringLiteral("alice")), kWaitMs);
    }

    void dialogsAreAccessible()
    {
        ui::SanctionDialog::Options options;
        options.targetName = QStringLiteral("alice");
        options.channelId = 1000;
        options.channelName = QStringLiteral("Lobby");
        options.canBanAddress = true;
        options.canBanPermanently = true;
        ui::SanctionDialog sanction(options);
        protocol::ChannelInfo channel{1000, 1, QStringLiteral("Lobby"), QString(), true, true, 0};
        ui::EditChannelDialog edit(channel);
        ui::ReportDialog report(QStringLiteral("alice"));
        ui::ChannelPickerDialog picker(QStringLiteral("Move"), QStringLiteral("Move to:"),
                                       {{1, QStringLiteral("Server")}, {1000, QStringLiteral("Server / Lobby")}}, 1000);
        ui::AccountDialog account({{1, QStringLiteral("Registered user")}});
        ui::PasswordDialog password(QStringLiteral("alice"));
        ui::RoleDialog role(protocol::RoleInfo{}, {1, 2, 31}, 80);
        ui::RuleDialog rule({{1000, QStringLiteral("Lobby")}}, {{1, QStringLiteral("Registered user")}});
        ui::AppealDecisionDialog appeal(QStringLiteral("alice appeals"), true);
        for (QDialog *dialog : std::initializer_list<QDialog *>{&sanction, &edit, &report, &picker, &account, &password,
                                                                &role, &rule, &appeal}) {
            dialog->show();
            const QStringList problems = unnamedFocusableWidgets(dialog);
            QVERIFY2(problems.isEmpty(),
                     qPrintable(dialog->windowTitle() + QStringLiteral(": ") + problems.join(QStringLiteral(", "))));
            dialog->hide();
        }
        // Mutes may last until lifted without the permanent-ban permission; bans may not.
        options.canBanPermanently = false;
        options.type = moderation::SanctionType::Mute;
        ui::SanctionDialog limited(options);
        auto *duration = named<QComboBox>(&limited, QStringLiteral("How long"));
        QVERIFY(duration->findData(0u) >= 0);
        auto *type = named<QComboBox>(&limited, QStringLiteral("What to do"));
        type->setCurrentIndex(type->findData(static_cast<int>(moderation::SanctionType::Ban)));
        QCOMPARE(duration->findData(0u), -1);
        // Reasons are required where the server requires them.
        type->setCurrentIndex(type->findData(static_cast<int>(moderation::SanctionType::Warning)));
        QVERIFY(!duration->isEnabled());
        QCOMPARE(limited.request().durationSeconds, 0u);
    }
};

QTEST_MAIN(TestModerationUi)
#include "tst_moderation_ui.moc"
