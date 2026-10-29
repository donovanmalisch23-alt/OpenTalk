// SPDX-License-Identifier: GPL-3.0-only
// The authorization matrix: every moderation and administration request, tried by every
// built-in role, must be allowed for exactly the roles that should have it and refused
// with NotAuthorized for everyone else. Each row uses a fresh server.

#include "ServerCoreHarness.h"

#include "opentalk/moderation/Permissions.h"
#include "opentalk/moderation/Sanctions.h"

using namespace opentalk;
using namespace opentalk::protocol;
using namespace opentalk::server;
using opentalk::test::Harness;

namespace {

enum class Who { Owner, Admin, Moderator, User, Restricted, Guest };

QString nameOf(Who who)
{
    switch (who) {
    case Who::Owner:
        return QStringLiteral("owner");
    case Who::Admin:
        return QStringLiteral("admin");
    case Who::Moderator:
        return QStringLiteral("mod");
    case Who::User:
        return QStringLiteral("alice");
    case Who::Restricted:
        return QStringLiteral("rita");
    case Who::Guest:
        return QStringLiteral("Visitor");
    }
    return {};
}

constexpr quint32 kActor = 1;
constexpr quint32 kTarget = 2; // an online guest, ranked below everyone but Restricted
constexpr quint64 kRequest = 77;

/// What one request needs: the message to send (for `kRequest`), and the roles that
/// may do it.
struct Action {
    std::function<Message(Harness &)> make;
    QList<Who> allowed;
};

QList<Who> everyone()
{
    return {Who::Owner, Who::Admin, Who::Moderator, Who::User, Who::Restricted, Who::Guest};
}

ApplySanction sanction(moderation::SanctionType type, quint32 seconds = 0, bool address = false)
{
    ApplySanction m;
    m.requestId = kRequest;
    m.userId = kTarget;
    m.type = static_cast<quint8>(type);
    m.durationSeconds = seconds;
    m.reason = QStringLiteral("testing");
    m.banAddress = address;
    return m;
}

QHash<QString, Action> actions()
{
    using T = moderation::SanctionType;
    const QList<Who> staff{Who::Owner, Who::Admin, Who::Moderator};
    const QList<Who> admins{Who::Owner, Who::Admin};
    QHash<QString, Action> a;
    a.insert(QStringLiteral("move someone"),
             {[](Harness &) { return Message(MoveUser{kRequest, kTarget, 1000}); }, staff});
    a.insert(QStringLiteral("warn"), {[](Harness &) { return Message(sanction(T::Warning)); }, staff});
    a.insert(QStringLiteral("mute 10 minutes"), {[](Harness &) { return Message(sanction(T::Mute, 600)); }, staff});
    a.insert(QStringLiteral("kick"), {[](Harness &) { return Message(sanction(T::Kick)); }, staff});
    a.insert(QStringLiteral("ban 1 day"), {[](Harness &) { return Message(sanction(T::Ban, 86400)); }, staff});
    a.insert(QStringLiteral("ban forever"), {[](Harness &) { return Message(sanction(T::Ban, 0)); }, admins});
    a.insert(QStringLiteral("ban address"), {[](Harness &) { return Message(sanction(T::Ban, 86400, true)); }, admins});
    a.insert(QStringLiteral("restrict"), {[](Harness &) { return Message(sanction(T::Restrict, 3600)); }, staff});
    a.insert(QStringLiteral("list reports"),
             {[](Harness &) { return Message(ListRequest{kRequest, ListKind::Reports, true}); }, staff});
    a.insert(QStringLiteral("list accounts"),
             {[](Harness &) { return Message(ListRequest{kRequest, ListKind::Accounts, true}); }, admins});
    a.insert(QStringLiteral("list rules"),
             {[](Harness &) { return Message(ListRequest{kRequest, ListKind::Rules, true}); }, admins});
    a.insert(QStringLiteral("own sanctions"),
             {[](Harness &) { return Message(ListRequest{kRequest, ListKind::Sanctions, false}); }, everyone()});
    a.insert(QStringLiteral("read audit log"), {[](Harness &) { return Message(ReadAudit{kRequest, 0, 50}); }, staff});
    a.insert(QStringLiteral("create account"),
             {[](Harness &) {
                  return Message(CreateAccount{kRequest, QStringLiteral("newbie"), QStringLiteral("long-password"), 1});
              },
              admins});
    a.insert(QStringLiteral("reset a password"),
             {[](Harness &) {
                  return Message(
                      SetAccountPassword{kRequest, QStringLiteral("bob"), QStringLiteral("another-password")});
              },
              admins});
    a.insert(QStringLiteral("delete account"),
             {[](Harness &) { return Message(DeleteAccount{kRequest, QStringLiteral("bob")}); }, admins});
    a.insert(QStringLiteral("make someone moderator"),
             {[](Harness &) { return Message(SetUserRole{kRequest, QStringLiteral("bob"), 4}); }, admins});
    a.insert(QStringLiteral("create role"),
             {[](Harness &) {
                  return Message(SaveRole{kRequest, RoleInfo{0, QStringLiteral("Helper"), 20, {1, 10}, false}});
              },
              admins});
    a.insert(QStringLiteral("add channel rule"), {[](Harness &) {
                                                      RuleInfo rule;
                                                      rule.channelId = 1000;
                                                      rule.roleId = 1;
                                                      rule.capability = 20;
                                                      rule.allow = false;
                                                      return Message(AddRule{kRequest, rule});
                                                  },
                                                  admins});
    a.insert(QStringLiteral("read server settings"),
             {[](Harness &) { return Message(ServerSettingsRequest{kRequest, std::nullopt}); }, admins});
    a.insert(QStringLiteral("server statistics"), {[](Harness &) { return Message(GetServerStats{kRequest}); }, staff});
    a.insert(QStringLiteral("transfer ownership"),
             {[](Harness &) { return Message(TransferOwnership{kRequest, QStringLiteral("admin")}); }, {Who::Owner}});
    a.insert(QStringLiteral("edit permanent channel"),
             {[](Harness &) {
                  return Message(EditChannel{kRequest, 1000, QStringLiteral("Renamed"), QString(), 0, false, {}});
              },
              admins});
    a.insert(QStringLiteral("delete channel"),
             {[](Harness &) { return Message(DeleteChannel{kRequest, 1000}); }, admins});
    a.insert(QStringLiteral("create permanent channel"),
             {[](Harness &) {
                  return Message(CreateChannel{kRequest, kRootChannelId, QStringLiteral("Hall"), {}, true, {}, 0});
              },
              admins});
    a.insert(QStringLiteral("create temporary channel"),
             {[](Harness &) {
                  return Message(CreateChannel{kRequest, kRootChannelId, QStringLiteral("Den"), {}, false, {}, 0});
              },
              {Who::Owner, Who::Admin, Who::Moderator, Who::User}});
    a.insert(QStringLiteral("message the server"),
             {[](Harness &) { return Message(SendText{kRequest, TextTarget::Server, 0, QStringLiteral("hi all")}); },
              admins});
    a.insert(QStringLiteral("message the channel"),
             {[](Harness &) {
                  return Message(SendText{kRequest, TextTarget::Channel, kRootChannelId, QStringLiteral("hello")});
              },
              {Who::Owner, Who::Admin, Who::Moderator, Who::User, Who::Guest}});
    a.insert(QStringLiteral("private message"),
             {[](Harness &) { return Message(SendText{kRequest, TextTarget::User, kTarget, QStringLiteral("psst")}); },
              {Who::Owner, Who::Admin, Who::Moderator, Who::User, Who::Guest}});
    a.insert(QStringLiteral("join a channel"),
             {[](Harness &) { return Message(JoinChannel{kRequest, 1000, {}}); }, everyone()});
    return a;
}

} // namespace

class TestAuthorization : public QObject {
    Q_OBJECT

private:
    /// A server with an owner, an administrator, a moderator, a user, a restricted user
    /// and a persistent channel 1000; `who` is logged in as session 1 and a guest target
    /// as session 2.
    std::unique_ptr<Harness> serverWith(Who who)
    {
        auto h = std::make_unique<Harness>();
        h->addAccount(QStringLiteral("owner"), Role::Owner);
        h->addAccount(QStringLiteral("mod"), Role::Moderator);
        h->addAccount(QStringLiteral("rita"), Role::Restricted);
        StoredChannel lobby{0, 0, QStringLiteral("Lobby"), {}, {}, 0};
        if (!h->db.insertChannel(lobby) || lobby.id != 1000) {
            return nullptr;
        }
        h->start(Harness::guestsAllowed());
        if (!h->login(kActor, nameOf(who), who == Who::Guest) || !h->login(kTarget, QStringLiteral("Target"), true)) {
            return nullptr;
        }
        return h;
    }

private slots:
    void initTestCase() { QVERIFY(security::initializePasswordHashing()); }

    void matrix_data()
    {
        QTest::addColumn<QString>("action");
        QTest::addColumn<int>("who");
        QStringList names = actions().keys();
        names.sort();
        for (const QString &action : std::as_const(names)) {
            for (Who who : everyone()) {
                QTest::newRow(qPrintable(action + QStringLiteral(" / ") + nameOf(who)))
                    << action << static_cast<int>(who);
            }
        }
    }

    void matrix()
    {
        QFETCH(QString, action);
        QFETCH(int, who);
        const Who actor = static_cast<Who>(who);
        auto h = serverWith(actor);
        QVERIFY(h);
        const Action a = actions().value(action);
        h->send(kActor, a.make(*h));
        const std::optional<ErrorCode> result = h->answer(kActor, kRequest);
        QVERIFY2(result != std::optional<ErrorCode>(ErrorCode::Timeout), "no answer");
        if (a.allowed.contains(actor)) {
            QVERIFY2(!result.has_value(),
                     qPrintable(QStringLiteral("refused with %1: %2")
                                    .arg(toString(result.value_or(ErrorCode::None)))
                                    .arg(h->last<Error>(kActor) ? h->last<Error>(kActor)->detail : QString())));
        } else {
            QVERIFY2(result == std::optional<ErrorCode>(ErrorCode::NotAuthorized),
                     qPrintable(QStringLiteral("expected NotAuthorized, got %1")
                                    .arg(result ? toString(*result) : QStringLiteral("success"))));
        }
    }

    void nobodyActsOnTheirEqualsOrSuperiors()
    {
        // A moderator cannot touch another moderator, an administrator or the owner, and an
        // administrator cannot touch the owner.
        struct Case {
            QString actor;
            QString target;
        };
        for (const Case &c :
             {Case{QStringLiteral("mod"), QStringLiteral("mod2")}, Case{QStringLiteral("mod"), QStringLiteral("admin")},
              Case{QStringLiteral("mod"), QStringLiteral("owner")},
              Case{QStringLiteral("admin"), QStringLiteral("owner")},
              Case{QStringLiteral("admin"), QStringLiteral("admin2")}}) {
            Harness h;
            h.addAccount(QStringLiteral("owner"), Role::Owner);
            h.addAccount(QStringLiteral("mod"), Role::Moderator);
            h.addAccount(QStringLiteral("mod2"), Role::Moderator);
            h.addAccount(QStringLiteral("admin2"), Role::Admin);
            h.start(Harness::guestsAllowed());
            QVERIFY(h.login(1, c.actor));
            QVERIFY(h.login(2, c.target));
            for (auto type :
                 {moderation::SanctionType::Kick, moderation::SanctionType::Mute, moderation::SanctionType::Ban}) {
                ApplySanction m = sanction(type, 600);
                m.requestId = 10 + static_cast<quint64>(type);
                h.send(1, m);
                QVERIFY2(h.answer(1, m.requestId) == std::optional<ErrorCode>(ErrorCode::NotAuthorized),
                         qPrintable(c.actor + QStringLiteral(" acted on ") + c.target));
            }
            h.send(1, MoveUser{30, 2, kRootChannelId});
            QVERIFY(h.answer(1, 30) == std::optional<ErrorCode>(ErrorCode::NotAuthorized));
            h.send(1, SetUserRole{31, c.target, 1});
            QVERIFY(h.answer(1, 31) == std::optional<ErrorCode>(ErrorCode::NotAuthorized));
            QVERIFY(!h.closed.contains(2));
        }
    }

    void nobodyGrantsWhatTheyLack()
    {
        Harness h;
        h.addAccount(QStringLiteral("owner"), Role::Owner);
        h.start(Harness::guestsAllowed());
        QVERIFY(h.login(1, QStringLiteral("owner")));
        // The owner makes a role that can manage roles but not ban people permanently...
        RoleInfo manager{0, QStringLiteral("Role manager"), 60, {50, 51, 52}, false};
        h.send(1, SaveRole{1, manager});
        QCOMPARE(h.answer(1, 1), std::optional<ErrorCode>());
        const quint32 managerId = h.last<RequestDone>(1)->resultId.value_or(0);
        QVERIFY(managerId >= 100);
        h.send(1, SetUserRole{2, QStringLiteral("alice"), managerId});
        QCOMPARE(h.answer(1, 2), std::optional<ErrorCode>());
        // ...who therefore cannot create a role that can.
        QVERIFY(h.login(2, QStringLiteral("alice")));
        h.send(2, SaveRole{3, RoleInfo{0,
                                       QStringLiteral("Banner"),
                                       20,
                                       {static_cast<quint16>(moderation::Capability::UserBanPermanent)},
                                       false}});
        QVERIFY(h.answer(2, 3) == std::optional<ErrorCode>(ErrorCode::NotAuthorized));
        // Nor a role ranked at or above their own, nor give their own role away.
        h.send(2, SaveRole{4, RoleInfo{0, QStringLiteral("Peer"), 60, {1}, false}});
        QVERIFY(h.answer(2, 4) == std::optional<ErrorCode>(ErrorCode::InvalidRequest));
        h.send(2, SetUserRole{5, QStringLiteral("bob"), managerId});
        QVERIFY(h.answer(2, 5) == std::optional<ErrorCode>(ErrorCode::NotAuthorized));
        // No one can grant the owner-only capability.
        h.send(1, SaveRole{6, RoleInfo{0,
                                       QStringLiteral("Heir"),
                                       80,
                                       {static_cast<quint16>(moderation::Capability::OwnerTransfer)},
                                       false}});
        QVERIFY(h.answer(1, 6) == std::optional<ErrorCode>(ErrorCode::InvalidRequest));
    }
};

QTEST_GUILESS_MAIN(TestAuthorization)
#include "tst_authorization.moc"
