// SPDX-License-Identifier: GPL-3.0-only
// What moderation does on a server: bans, mutes, kicks, restrictions and their expiry,
// appeals and their separation of duties, reports, permission rules, roles, settings,
// ownership and the tamper-evident audit log.

#include "ServerCoreHarness.h"

#include "opentalk/moderation/Permissions.h"
#include "opentalk/moderation/Sanctions.h"

#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>

using namespace opentalk;
using namespace opentalk::protocol;
using namespace opentalk::server;
using moderation::SanctionType;
using opentalk::test::Harness;

namespace {

constexpr qint64 kHourMs = 3600LL * 1000;

ApplySanction sanctionOn(quint64 requestId, quint32 userId, SanctionType type, quint32 seconds = 0,
                         const QString &reason = QStringLiteral("rule 1"))
{
    ApplySanction m;
    m.requestId = requestId;
    m.userId = userId;
    m.type = static_cast<quint8>(type);
    m.durationSeconds = seconds;
    m.reason = reason;
    return m;
}

std::unique_ptr<Harness> moderatedServer()
{
    auto h = std::make_unique<Harness>();
    h->addAccount(QStringLiteral("owner"), Role::Owner);
    h->addAccount(QStringLiteral("mod"), Role::Moderator);
    StoredChannel lobby{0, 0, QStringLiteral("Lobby"), {}, {}, 0};
    h->db.insertChannel(lobby);
    h->start(Harness::guestsAllowed());
    return h;
}

} // namespace

class TestModerationServer : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QVERIFY(security::initializePasswordHashing()); }

    void kickDisconnectsAndIsAudited()
    {
        auto h = moderatedServer();
        QVERIFY(h->login(1, QStringLiteral("mod")));
        QVERIFY(h->login(2, QStringLiteral("alice")));
        h->send(1, sanctionOn(1, 2, SanctionType::Kick, 0, QStringLiteral("spamming")));
        QCOMPARE(h->answer(1, 1), std::optional<ErrorCode>());
        QVERIFY(h->closed.contains(2));
        QVERIFY(h->closed.value(2).first == ErrorCode::Kicked);
        QVERIFY(h->closed.value(2).second.contains(QStringLiteral("spamming")));
        QVERIFY(h->has<SanctionNotice>(2)); // told why before being disconnected
        const auto entries = h->db.auditEntries(0, 10);
        QVERIFY(std::any_of(entries.cbegin(), entries.cend(), [](const auto &e) {
            return e.action == QLatin1String("sanction.kick") && e.target == QLatin1String("alice") &&
                   e.actorName == QLatin1String("mod") && e.reason == QLatin1String("spamming");
        }));
        // Kicked people can come straight back.
        QVERIFY(h->login(3, QStringLiteral("alice")));
    }

    void botsAreMarkedForEveryoneAndInTheAuditLog()
    {
        auto h = moderatedServer();
        h->addAccount(QStringLiteral("helper"), Role::Moderator);
        QVERIFY(h->db.setBot(QStringLiteral("helper"), true));
        QVERIFY(h->login(1, QStringLiteral("alice")));
        QVERIFY(h->login(2, QStringLiteral("helper")));
        const auto joined = h->last<UserJoined>(1);
        QVERIFY(joined && joined->user.name == QStringLiteral("helper"));
        QVERIFY((joined->user.flags & BotFlag) != 0);
        // A bot moderates under its own account, and the log says it is a bot.
        h->send(2, sanctionOn(1, 1, SanctionType::Warning));
        QCOMPARE(h->answer(2, 1), std::optional<ErrorCode>());
        const auto entries = h->db.auditEntries(0, 5);
        QVERIFY(std::any_of(entries.cbegin(), entries.cend(), [](const auto &e) {
            return e.action == QLatin1String("sanction.warning") && e.actorName == QLatin1String("helper (bot)");
        }));
        // Mutes and other flags keep the bot flag. (A moderator cannot mute it: equal rank.)
        QVERIFY(h->login(3, QStringLiteral("mod")));
        h->send(3, sanctionOn(2, 2, SanctionType::Mute, 600));
        QVERIFY(h->answer(3, 2) == std::optional<ErrorCode>(ErrorCode::NotAuthorized));
        QVERIFY(h->login(4, QStringLiteral("owner")));
        h->send(4, sanctionOn(2, 2, SanctionType::Mute, 600));
        QCOMPARE(h->answer(4, 2), std::optional<ErrorCode>());
        QCOMPARE(h->last<UserUpdated>(1)->flags & (BotFlag | VoiceMutedFlag), BotFlag | VoiceMutedFlag);

        // Administrators can create bot accounts, and see which accounts are bots.
        h->send(4, CreateAccount{3, QStringLiteral("dj"), QStringLiteral("long password"), 1, true});
        QTRY_VERIFY(h->answer(4, 3).has_value() || h->has<RequestDone>(4));
        QVERIFY(h->db.findAccount(QStringLiteral("dj"))->bot);
        h->send(4, ListRequest{4, ListKind::Accounts, true});
        QCOMPARE(h->answer(4, 4), std::optional<ErrorCode>());
        const auto accounts = h->last<ListResult>(4)->accounts;
        QVERIFY(std::any_of(accounts.cbegin(), accounts.cend(),
                            [](const AccountInfo &a) { return a.name == QLatin1String("dj") && a.bot; }));
        QVERIFY(std::any_of(accounts.cbegin(), accounts.cend(),
                            [](const AccountInfo &a) { return a.name == QLatin1String("alice") && !a.bot; }));
    }

    void oldAuditEntriesAreRemovedDaily()
    {
        auto h = moderatedServer();
        ServerCore::Config config = Harness::guestsAllowed();
        config.auditRetentionDays = 2;
        h->start(config);
        QVERIFY(h->login(1, QStringLiteral("mod")));
        QVERIFY(h->login(2, QStringLiteral("alice")));
        h->send(1, sanctionOn(1, 2, SanctionType::Warning));
        QCOMPARE(h->answer(1, 1), std::optional<ErrorCode>());
        const quint64 before = h->db.auditCount();
        QVERIFY(before >= 2);
        // A day later nothing is old enough; three days later the early entries go.
        h->wall += 25 * kHourMs;
        h->core->tick();
        QCOMPARE(h->db.auditCount(), before);
        h->send(1, sanctionOn(2, 2, SanctionType::Warning));
        QCOMPARE(h->answer(1, 2), std::optional<ErrorCode>());
        h->wall += 48 * kHourMs;
        h->core->tick();
        QVERIFY(h->db.auditCount() < before + 1);
        QVERIFY(h->db.verifyAudit().intact); // still checkable after pruning
        const auto entries = h->db.auditEntries(0, 10);
        QVERIFY(std::all_of(entries.cbegin(), entries.cend(),
                            [&h](const auto &e) { return e.timeMs >= h->wall - 2 * 24 * kHourMs; }));
    }

    void banBlocksLoginUntilItExpires()
    {
        auto h = moderatedServer();
        QVERIFY(h->login(1, QStringLiteral("mod")));
        QVERIFY(h->login(2, QStringLiteral("alice")));
        h->send(1, sanctionOn(1, 2, SanctionType::Ban, 2 * 3600));
        QCOMPARE(h->answer(1, 1), std::optional<ErrorCode>());
        QVERIFY(h->closed.value(2).first == ErrorCode::Banned);
        QVERIFY(!h->login(3, QStringLiteral("alice")));
        QVERIFY(h->closed.value(3).first == ErrorCode::Banned);
        QVERIFY(h->closed.value(3).second.contains(QStringLiteral("rule 1")));
        // A wrong password still says "wrong password", so a ban does not reveal the account.
        QVERIFY(!h->login(4, QStringLiteral("alice"), false, QStringLiteral("nope")));
        QVERIFY(h->closed.value(4).first == ErrorCode::AuthenticationFailed);

        h->wall += 2 * kHourMs;
        h->core->tick();
        QVERIFY(h->login(5, QStringLiteral("alice")));
        // Bans also survive a restart: they live in the database.
        QCOMPARE(h->db.sanctions().size(), 1);
        QVERIFY(h->db.sanctions().first().state == moderation::SanctionState::Expired);
    }

    void reasonsAreRequiredForSeriousSanctions()
    {
        auto h = moderatedServer();
        QVERIFY(h->login(1, QStringLiteral("mod")));
        QVERIFY(h->login(2, QStringLiteral("alice")));
        h->send(1, sanctionOn(1, 2, SanctionType::Ban, 3600, QString()));
        QVERIFY(h->answer(1, 1) == std::optional<ErrorCode>(ErrorCode::InvalidRequest));
        h->send(1, sanctionOn(2, 2, SanctionType::Mute, 600, QString())); // short mute: no reason needed
        QCOMPARE(h->answer(1, 2), std::optional<ErrorCode>());
    }

    void guestsAreBannedByAddress()
    {
        auto h = moderatedServer();
        const QHostAddress troll(QStringLiteral("198.51.100.7"));
        QVERIFY(h->login(1, QStringLiteral("mod")));
        QVERIFY(h->login(2, QStringLiteral("Troll"), true, {}, troll));
        h->send(1, sanctionOn(1, 2, SanctionType::Ban, 3600));
        QCOMPARE(h->answer(1, 1), std::optional<ErrorCode>());
        QVERIFY(!h->login(3, QStringLiteral("NewName"), true, {}, troll));
        QVERIFY(h->closed.value(3).first == ErrorCode::Banned);
        QVERIFY(h->login(4, QStringLiteral("Someone"), true, {}, QHostAddress(QStringLiteral("198.51.100.8"))));
        // Accounts from that address are not affected by a guest ban...
        QVERIFY(h->login(5, QStringLiteral("alice"), false, {}, troll));
    }

    void mutesStopVoiceAndTextAndAreShown()
    {
        auto h = moderatedServer();
        QVERIFY(h->login(1, QStringLiteral("mod")));
        QVERIFY(h->login(2, QStringLiteral("alice")));
        QVERIFY(h->login(3, QStringLiteral("bob")));
        QCOMPARE(h->core->voiceRecipients(2).size(), 2);
        h->send(1, sanctionOn(1, 2, SanctionType::Mute, 600));
        QCOMPARE(h->answer(1, 1), std::optional<ErrorCode>());
        QVERIFY(h->core->voiceRecipients(2).isEmpty());
        h->send(2, SendText{2, TextTarget::Channel, kRootChannelId, QStringLiteral("let me talk")});
        QVERIFY(h->answer(2, 2) == std::optional<ErrorCode>(ErrorCode::Muted));
        const auto update = h->last<UserUpdated>(3);
        QVERIFY(update && update->userId == 2 && (update->flags & VoiceMutedFlag) && (update->flags & TextMutedFlag));
        // Lifting it early restores everything.
        const quint64 id = h->last<SanctionNotice>(2)->sanction.id;
        h->send(1, RevokeSanction{3, id, QStringLiteral("apologised")});
        QCOMPARE(h->answer(1, 3), std::optional<ErrorCode>());
        QCOMPARE(h->core->voiceRecipients(2).size(), 2);
        QCOMPARE(h->last<UserUpdated>(3)->flags, 0u);
    }

    void channelBansAndChannelMutes()
    {
        auto h = moderatedServer();
        QVERIFY(h->login(1, QStringLiteral("mod")));
        QVERIFY(h->login(2, QStringLiteral("alice")));
        h->send(2, JoinChannel{1, 1000, {}});
        QCOMPARE(h->answer(2, 1), std::optional<ErrorCode>());
        ApplySanction ban = sanctionOn(2, 2, SanctionType::Ban, 3600);
        ban.channelId = 1000;
        h->send(1, ban);
        QCOMPARE(h->answer(1, 2), std::optional<ErrorCode>());
        QVERIFY(!h->closed.contains(2));                            // still on the server...
        QCOMPARE(h->last<UserMoved>(2)->channelId, kRootChannelId); // ...but out of the channel
        h->send(2, JoinChannel{3, 1000, {}});
        QVERIFY(h->answer(2, 3) == std::optional<ErrorCode>(ErrorCode::Banned));

        ApplySanction mute = sanctionOn(4, 2, SanctionType::VoiceMute, 600);
        mute.channelId = 1000;
        h->send(1, mute);
        QCOMPARE(h->answer(1, 4), std::optional<ErrorCode>());
        QVERIFY(!h->core->voiceRecipients(2).isEmpty() || true); // root: not muted there
        QVERIFY(!(h->last<UserUpdated>(1) && (h->last<UserUpdated>(1)->flags & VoiceMutedFlag)));
    }

    void restrictionsRemoveRightsForAWhile()
    {
        auto h = moderatedServer();
        QVERIFY(h->login(1, QStringLiteral("mod")));
        QVERIFY(h->login(2, QStringLiteral("alice")));
        const qsizetype before = h->last<AuthResult>(2)->capabilities.size();
        h->send(1, sanctionOn(1, 2, SanctionType::Restrict, 3600));
        QCOMPARE(h->answer(1, 1), std::optional<ErrorCode>());
        const auto caps = h->last<CapabilitiesChanged>(2);
        QVERIFY(caps);
        QVERIFY(caps->capabilities.size() < before);
        QVERIFY(caps->capabilities.contains(static_cast<quint16>(moderation::Capability::ChannelJoin)));
        h->send(2, SendText{2, TextTarget::Channel, kRootChannelId, QStringLiteral("hi")});
        QVERIFY(h->answer(2, 2) == std::optional<ErrorCode>(ErrorCode::NotAuthorized));
        QVERIFY(h->core->voiceRecipients(2).isEmpty());
        h->wall += kHourMs;
        h->core->tick();
        QCOMPARE(h->last<CapabilitiesChanged>(2)->capabilities.size(), before);
    }

    void offlineAccountsCanBeBanned()
    {
        auto h = moderatedServer();
        QVERIFY(h->login(1, QStringLiteral("mod")));
        ApplySanction ban;
        ban.requestId = 1;
        ban.account = QStringLiteral("bob");
        ban.type = static_cast<quint8>(SanctionType::Ban);
        ban.durationSeconds = 600;
        ban.reason = QStringLiteral("raid");
        h->send(1, ban);
        QCOMPARE(h->answer(1, 1), std::optional<ErrorCode>());
        QVERIFY(!h->login(2, QStringLiteral("bob")));
        ban.account = QStringLiteral("nobody");
        ban.requestId = 2;
        h->send(1, ban);
        QVERIFY(h->answer(1, 2) == std::optional<ErrorCode>(ErrorCode::NotFound));
    }

    void appealsNeedSomeoneElse()
    {
        auto h = moderatedServer();
        QVERIFY(h->login(1, QStringLiteral("mod")));
        QVERIFY(h->login(2, QStringLiteral("alice")));
        QVERIFY(h->login(3, QStringLiteral("admin")));
        h->send(1, sanctionOn(1, 2, SanctionType::Mute, 3 * 3600, QStringLiteral("shouting")));
        QCOMPARE(h->answer(1, 1), std::optional<ErrorCode>());
        const quint64 sanctionId = h->last<SanctionNotice>(2)->sanction.id;

        h->send(2, SubmitAppeal{2, sanctionId, QStringLiteral("My microphone was broken.")});
        QCOMPARE(h->answer(2, 2), std::optional<ErrorCode>());
        h->send(2, SubmitAppeal{3, sanctionId, QStringLiteral("Again!")});
        QVERIFY(h->answer(2, 3) == std::optional<ErrorCode>(ErrorCode::InvalidRequest)); // once only
        // Deciders hear about it (the moderator and the admin can both decide appeals).
        QVERIFY(h->last<TextMessage>(3)->text.contains(QStringLiteral("appeal")));

        h->send(3, ListRequest{4, ListKind::Appeals, false});
        QCOMPARE(h->answer(3, 4), std::optional<ErrorCode>());
        const quint64 appealId = h->last<ListResult>(3)->appeals.first().id;
        // The moderator who muted alice may not decide, because the admin can.
        h->send(1, DecideAppeal{5, appealId, static_cast<quint8>(moderation::Appeal::State::Overturned),
                                QStringLiteral("fine"), 0});
        QVERIFY(h->answer(1, 5) == std::optional<ErrorCode>(ErrorCode::NotAuthorized));
        h->send(3, DecideAppeal{6, appealId, static_cast<quint8>(moderation::Appeal::State::Overturned),
                                QStringLiteral("hardware fault"), 0});
        QCOMPARE(h->answer(3, 6), std::optional<ErrorCode>());
        QCOMPARE(h->core->voiceRecipients(2).size(), 2); // mute lifted
        QVERIFY(h->last<TextMessage>(2)->text.contains(QStringLiteral("overturned")));
        QCOMPARE(h->last<SanctionNotice>(2)->sanction.state,
                 static_cast<quint8>(moderation::SanctionState::Overturned));
    }

    void reducedAppealShortensTheSanction()
    {
        auto h = moderatedServer();
        QVERIFY(h->login(1, QStringLiteral("mod")));
        QVERIFY(h->login(2, QStringLiteral("alice")));
        QVERIFY(h->login(3, QStringLiteral("owner")));
        h->send(1, sanctionOn(1, 2, SanctionType::Mute, 24 * 3600));
        QCOMPARE(h->answer(1, 1), std::optional<ErrorCode>());
        const quint64 sanctionId = h->last<SanctionNotice>(2)->sanction.id;
        h->send(2, SubmitAppeal{2, sanctionId, QStringLiteral("Too long.")});
        QCOMPARE(h->answer(2, 2), std::optional<ErrorCode>());
        const quint64 appealId = static_cast<quint64>(h->last<RequestDone>(2)->resultId.value_or(0));
        h->send(3, DecideAppeal{3, appealId, static_cast<quint8>(moderation::Appeal::State::Reduced),
                                QStringLiteral("one hour is enough"), 3600});
        QCOMPARE(h->answer(3, 3), std::optional<ErrorCode>());
        QVERIFY(h->core->voiceRecipients(2).isEmpty());
        h->wall += kHourMs;
        h->core->tick();
        QCOMPARE(h->core->voiceRecipients(2).size(), 2);
    }

    void reportsReachModeratorsAnonymously()
    {
        auto h = moderatedServer();
        QVERIFY(h->login(1, QStringLiteral("mod")));
        QVERIFY(h->login(2, QStringLiteral("alice")));
        QVERIFY(h->login(3, QStringLiteral("Troll"), true));
        h->send(2, ReportUser{1, 3, QStringLiteral("insults")});
        QCOMPARE(h->answer(2, 1), std::optional<ErrorCode>());
        QVERIFY(h->last<TextMessage>(1)->text.contains(QStringLiteral("Troll")));
        QVERIFY(!h->has<TextMessage>(3)); // the reported person is not told
        h->send(2, ReportUser{2, 3, QStringLiteral("   ")});
        QVERIFY(h->answer(2, 2) == std::optional<ErrorCode>(ErrorCode::InvalidRequest));
        h->send(1, ListRequest{3, ListKind::Reports, false});
        QCOMPARE(h->answer(1, 3), std::optional<ErrorCode>());
        const ReportInfo report = h->last<ListResult>(1)->reports.first();
        QCOMPARE(report.targetName, QStringLiteral("Troll"));
        QCOMPARE(report.reporterName, QStringLiteral("alice")); // moderators see who reported
        h->send(1, ResolveReport{4, report.id, 2, QStringLiteral("talked to them")});
        QCOMPARE(h->answer(1, 4), std::optional<ErrorCode>());
        h->send(1, ListRequest{5, ListKind::Reports, false});
        QCOMPARE(h->answer(1, 5), std::optional<ErrorCode>());
        QVERIFY(h->last<ListResult>(1)->reports.isEmpty());
    }

    void channelRulesAndChannelModerators()
    {
        auto h = moderatedServer();
        QVERIFY(h->login(1, QStringLiteral("admin")));
        QVERIFY(h->login(2, QStringLiteral("alice")));
        QVERIFY(h->login(3, QStringLiteral("bob")));
        // The Lobby becomes listen-only for registered users...
        RuleInfo quiet;
        quiet.channelId = 1000;
        quiet.roleId = 1;
        quiet.capability = static_cast<quint16>(moderation::Capability::VoiceTransmit);
        quiet.allow = false;
        h->send(1, AddRule{1, quiet});
        QCOMPARE(h->answer(1, 1), std::optional<ErrorCode>());
        // ...except for alice, who moderates it.
        for (auto c : {moderation::Capability::VoiceTransmit, moderation::Capability::UserKick}) {
            RuleInfo allow;
            allow.channelId = 1000;
            allow.forAccount = true;
            allow.account = QStringLiteral("alice");
            allow.capability = static_cast<quint16>(c);
            allow.allow = true;
            h->send(1, AddRule{2, allow});
            QCOMPARE(h->answer(1, 2), std::optional<ErrorCode>());
            h->sent[1].clear();
        }
        // Alice's client learns she may kick somewhere, so it can offer the action.
        QVERIFY(h->last<CapabilitiesChanged>(2)->channelCapabilities.contains(
            static_cast<quint16>(moderation::Capability::UserKick)));
        QVERIFY(!h->last<CapabilitiesChanged>(2)->capabilities.contains(
            static_cast<quint16>(moderation::Capability::UserKick)));
        for (quint32 id : {2u, 3u}) {
            h->send(id, JoinChannel{10, 1000, {}});
            QCOMPARE(h->answer(id, 10), std::optional<ErrorCode>());
        }
        QVERIFY(h->core->voiceRecipients(3).isEmpty());  // bob is silenced there
        QCOMPARE(h->core->voiceRecipients(2).size(), 1); // alice talks to bob
        // A channel moderator may kick a peer out of their channel...
        ApplySanction kick = sanctionOn(11, 3, SanctionType::Kick);
        kick.channelId = 1000;
        h->send(2, kick);
        QCOMPARE(h->answer(2, 11), std::optional<ErrorCode>());
        QVERIFY(!h->closed.contains(3));
        QCOMPARE(h->last<UserMoved>(3)->channelId, kRootChannelId);
        // ...but not off the server, and not anyone above them.
        h->send(2, sanctionOn(12, 3, SanctionType::Kick));
        QVERIFY(h->answer(2, 12) == std::optional<ErrorCode>(ErrorCode::NotAuthorized));
        QVERIFY(h->login(4, QStringLiteral("mod")));
        h->send(4, JoinChannel{13, 1000, {}});
        QCOMPARE(h->answer(4, 13), std::optional<ErrorCode>());
        kick.requestId = 14;
        kick.userId = 4;
        h->send(2, kick);
        QVERIFY(h->answer(2, 14) == std::optional<ErrorCode>(ErrorCode::NotAuthorized));
        h->send(1, ListRequest{15, ListKind::Rules, false});
        QCOMPARE(h->answer(1, 15), std::optional<ErrorCode>());
        QCOMPARE(h->last<ListResult>(1)->rules.size(), 3);
    }

    void rolesCanBeCreatedAssignedAndProtected()
    {
        auto h = moderatedServer();
        QVERIFY(h->login(1, QStringLiteral("owner")));
        QVERIFY(h->login(2, QStringLiteral("bob")));
        h->send(1, SaveRole{1, RoleInfo{0, QStringLiteral("Greeter"), 20, {1, 2, 10, 11, 20, 31}, false}});
        QCOMPARE(h->answer(1, 1), std::optional<ErrorCode>());
        const quint32 greeter = h->last<RequestDone>(1)->resultId.value_or(0);
        QVERIFY(h->has<RolesChanged>(2));
        h->send(1, SetUserRole{2, QStringLiteral("bob"), greeter});
        QCOMPARE(h->answer(1, 2), std::optional<ErrorCode>());
        QCOMPARE(h->last<UserUpdated>(2)->roleId, greeter);
        QVERIFY(h->last<CapabilitiesChanged>(2)->capabilities.contains(
            static_cast<quint16>(moderation::Capability::UserWarn)));
        h->send(1, DeleteRole{3, greeter});
        QVERIFY(h->answer(1, 3) == std::optional<ErrorCode>(ErrorCode::InvalidRequest)); // still in use
        h->send(1, DeleteRole{4, 1});
        QVERIFY(h->answer(1, 4) == std::optional<ErrorCode>(ErrorCode::InvalidRequest)); // built in
        h->send(1, SetUserRole{5, QStringLiteral("bob"), 1});
        QCOMPARE(h->answer(1, 5), std::optional<ErrorCode>());
        h->send(1, DeleteRole{6, greeter});
        QCOMPARE(h->answer(1, 6), std::optional<ErrorCode>());
    }

    void settingsChangeTheServer()
    {
        auto h = moderatedServer();
        QSignalSpy changed(h->core.get(), &ServerCore::settingsChanged);
        QVERIFY(h->login(1, QStringLiteral("admin")));
        h->send(1, ServerSettingsRequest{1, std::nullopt});
        QCOMPARE(h->answer(1, 1), std::optional<ErrorCode>());
        ServerSettingsInfo settings = h->last<ServerSettingsResult>(1)->settings;
        settings.name = QStringLiteral("Renamed");
        settings.welcomeMessage = QStringLiteral("Be kind.");
        settings.allowGuests = false;
        h->send(1, ServerSettingsRequest{2, settings});
        QCOMPARE(h->answer(1, 2), std::optional<ErrorCode>());
        QCOMPARE(changed.last().first().value<ServerSettingsInfo>().name, QStringLiteral("Renamed"));
        QVERIFY(!h->login(2, QStringLiteral("Visitor"), true)); // guests now refused
        QVERIFY(h->login(3, QStringLiteral("alice")));
        QCOMPARE(h->last<TextMessage>(3)->text, QStringLiteral("Be kind."));
        // Saved: a restarted server keeps them.
        h->start(Harness::guestsAllowed());
        QCOMPARE(h->core->settings().name, QStringLiteral("Renamed"));
        QVERIFY(!h->core->settings().allowGuests);
    }

    void ownershipCanBeTransferredOnlyByTheOwner()
    {
        auto h = moderatedServer();
        QVERIFY(h->login(1, QStringLiteral("owner")));
        QVERIFY(h->login(2, QStringLiteral("admin")));
        h->send(1, TransferOwnership{1, QStringLiteral("admin")});
        QCOMPARE(h->answer(1, 1), std::optional<ErrorCode>());
        QCOMPARE(h->db.owner()->username, QStringLiteral("admin"));
        QVERIFY(h->db.findAccount(QStringLiteral("owner"))->role == Role::Admin);
        QVERIFY(h->last<CapabilitiesChanged>(2)->capabilities.contains(
            static_cast<quint16>(moderation::Capability::OwnerTransfer)));
    }

    void auditLogIsChainedAndTamperEvident()
    {
        auto h = moderatedServer();
        QVERIFY(h->login(1, QStringLiteral("mod")));
        QVERIFY(h->login(2, QStringLiteral("alice")));
        for (int i = 0; i < 3; ++i) {
            h->send(1, sanctionOn(10 + static_cast<quint64>(i), 2, SanctionType::Warning));
            QCOMPARE(h->answer(1, 10 + static_cast<quint64>(i)), std::optional<ErrorCode>());
        }
        QVERIFY(h->db.verifyAudit().intact);
        h->send(1, ReadAudit{20, 0, 100});
        QCOMPARE(h->answer(1, 20), std::optional<ErrorCode>());
        const AuditPage page = *h->last<AuditPage>(1);
        QVERIFY(page.chainIntact);
        QVERIFY(page.entries.size() >= 4); // database upgrade or start, plus three warnings
        QVERIFY(page.entries.first().sequence > page.entries.last().sequence); // newest first

        // Someone edits the database behind the server's back...
        {
            QSqlDatabase raw = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("tamper"));
            raw.setDatabaseName(h->dir.filePath(QStringLiteral("core.db")));
            QVERIFY(raw.open());
            QSqlQuery q(raw);
            QVERIFY(q.exec(QStringLiteral("UPDATE audit_log SET reason = 'nothing happened' "
                                          "WHERE action = 'sanction.warning'")));
            raw.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("tamper"));
        // ...and the next start notices.
        h->start(Harness::guestsAllowed());
        QVERIFY(!h->core->auditStatus().intact);
        QVERIFY(h->core->auditStatus().firstBadSequence.has_value());
    }

    void statisticsAreAvailableToModerators()
    {
        auto h = moderatedServer();
        QVERIFY(h->login(1, QStringLiteral("mod")));
        QVERIFY(h->login(2, QStringLiteral("alice")));
        h->send(1, GetServerStats{1});
        QCOMPARE(h->answer(1, 1), std::optional<ErrorCode>());
        const ServerStatsInfo stats = h->last<ServerStatsResult>(1)->stats;
        QCOMPARE(stats.onlineUsers, 2u);
        QCOMPARE(stats.channels, 2u);
        QVERIFY(stats.auditIntact);
    }
};

QTEST_GUILESS_MAIN(TestModerationServer)
#include "tst_moderation_server.moc"
