// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/moderation/Appeals.h"
#include "opentalk/moderation/Audit.h"
#include "opentalk/moderation/Permissions.h"
#include "opentalk/moderation/Sanctions.h"

#include <QSet>
#include <QTest>

using namespace opentalk::moderation;
namespace r = opentalk::moderation::roles;

namespace {

PermissionResolver resolverWith(const QList<PermissionRule> &rules = {})
{
    PermissionResolver resolver;
    resolver.setRoles(builtInRoles());
    resolver.setRules(rules);
    return resolver;
}

PermissionRule rule(quint32 channel, PermissionRule::Subject subject, qint64 id, Capability c, bool allow)
{
    return {0, channel, subject, id, c, allow};
}

constexpr auto kRole = PermissionRule::Subject::Role;
constexpr auto kAccount = PermissionRule::Subject::Account;
constexpr qint64 kDay = 24LL * 60 * 60 * 1000;

Sanction sanction(quint64 id, SanctionType type, quint32 channel, qint64 account, const QString &name = {},
                  const QString &address = {})
{
    Sanction s;
    s.id = id;
    s.type = type;
    s.channelId = channel;
    s.targetAccountId = account;
    s.targetName = name;
    s.targetAddress = address;
    return s;
}

} // namespace

class TestModeration : public QObject {
    Q_OBJECT

private slots:
    void capabilitiesHaveUniqueNamesAndValues()
    {
        QSet<QString> names;
        QSet<quint16> values;
        for (Capability c : allCapabilities()) {
            names.insert(capabilityName(c));
            values.insert(static_cast<quint16>(c));
            QVERIFY(capabilityFromName(capabilityName(c)) == std::optional<Capability>(c));
            QVERIFY(capabilityFromValue(static_cast<quint16>(c)) == std::optional<Capability>(c));
            QVERIFY(!capabilityDescription(c).isEmpty());
        }
        QCOMPARE(names.size(), allCapabilities().size());
        QCOMPARE(values.size(), allCapabilities().size());
        QVERIFY(!capabilityFromName(QStringLiteral("user.explode")));
        QVERIFY(!capabilityFromValue(9999));
    }

    void builtInRolesAreOrdered()
    {
        const PermissionResolver p = resolverWith();
        QVERIFY(p.outranks(r::kOwner, r::kAdministrator));
        QVERIFY(p.outranks(r::kAdministrator, r::kModerator));
        QVERIFY(p.outranks(r::kModerator, r::kUser));
        QVERIFY(p.outranks(r::kUser, r::kGuest));
        QVERIFY(p.outranks(r::kGuest, r::kRestricted));
        // Strictly lower only: no one acts on peers, and no one acts on the owner.
        QVERIFY(!p.outranks(r::kModerator, r::kModerator));
        QVERIFY(!p.outranks(r::kAdministrator, r::kAdministrator));
        QVERIFY(!p.outranks(r::kOwner, r::kOwner));
        QVERIFY(!p.outranks(r::kAdministrator, r::kOwner));
        QVERIFY(!p.outranks(r::kUser, r::kModerator));
    }

    void defaultGrants()
    {
        const PermissionResolver p = resolverWith();
        const Actor guest{0, r::kGuest};
        const Actor user{7, r::kUser};
        const Actor moderator{8, r::kModerator};
        const Actor admin{9, r::kAdministrator};
        const Actor owner{1, r::kOwner};
        const Actor restricted{10, r::kRestricted};
        QVERIFY(p.allows(guest, Capability::VoiceTransmit));
        QVERIFY(!p.allows(guest, Capability::ChannelCreateTemporary));
        QVERIFY(p.allows(user, Capability::ChannelCreateTemporary));
        QVERIFY(!p.allows(user, Capability::UserKick));
        QVERIFY(p.allows(moderator, Capability::UserKick));
        QVERIFY(!p.allows(moderator, Capability::UserBanPermanent));
        QVERIFY(p.allows(admin, Capability::UserBanPermanent));
        QVERIFY(!p.allows(admin, Capability::OwnerTransfer)); // owner only
        QVERIFY(p.allows(owner, Capability::OwnerTransfer));
        QVERIFY(p.allows(restricted, Capability::ChannelJoin));
        QVERIFY(!p.allows(restricted, Capability::TextSend));
        QVERIFY(!p.allows(restricted, Capability::VoiceTransmit));
        QVERIFY(!p.allows({0, 4242}, Capability::ChannelJoin)); // unknown role: default deny
        QCOMPARE(p.serverCapabilities(owner).size(), allCapabilities().size());
    }

    void mostSpecificLevelWins()
    {
        // Channels: 1 (root) > 1000 (Games) > 1001 (Quiet room).
        const QList<quint32> quietRoom{1001, 1000, 1};
        const QList<quint32> games{1000, 1};
        const PermissionResolver p = resolverWith({
            rule(1000, kRole, r::kUser, Capability::VoiceTransmit, false), // Games is listen-only for users...
            rule(1001, kRole, r::kUser, Capability::VoiceTransmit, true),  // ...except the quiet room.
        });
        const Actor user{7, r::kUser};
        QVERIFY(p.allows(user, Capability::VoiceTransmit));         // server level: role grant
        QVERIFY(!p.allows(user, Capability::VoiceTransmit, games)); // denied at Games
        QVERIFY(p.allows(user, Capability::VoiceTransmit, quietRoom));
        QVERIFY(p.check(user, Capability::VoiceTransmit, games).reason.contains(QStringLiteral("denied")));
        QVERIFY(p.check(user, Capability::VoiceTransmit, games).reason.contains(QStringLiteral("channel 1000")));
        // Other roles are not affected by rules for users.
        QVERIFY(p.allows({0, r::kGuest}, Capability::VoiceTransmit, games));
    }

    void denyBeatsAllowAtTheSameLevel()
    {
        const PermissionResolver p = resolverWith({
            rule(1000, kAccount, 7, Capability::TextSend, true),
            rule(1000, kAccount, 7, Capability::TextSend, false),
        });
        QVERIFY(!p.allows({7, r::kUser}, Capability::TextSend, {1000, 1}));
    }

    void accountRulesAreMoreSpecificThanRoleRules()
    {
        // "Users may not talk in the Lobby, except alice" (a channel moderator).
        const PermissionResolver p = resolverWith({
            rule(1000, kRole, r::kUser, Capability::VoiceTransmit, false),
            rule(1000, kAccount, 7, Capability::VoiceTransmit, true),
        });
        QVERIFY(p.allows({7, r::kUser}, Capability::VoiceTransmit, {1000, 1}));
        QVERIFY(!p.allows({8, r::kUser}, Capability::VoiceTransmit, {1000, 1}));
        QVERIFY(
            p.check({7, r::kUser}, Capability::VoiceTransmit, {1000, 1}).reason.contains(QStringLiteral("account")));
    }

    void accountRulesMakeChannelModerators()
    {
        const PermissionResolver p = resolverWith({rule(1000, kAccount, 7, Capability::UserKick, true)});
        QVERIFY(p.allows({7, r::kUser}, Capability::UserKick, {1000, 1}));
        QVERIFY(p.allows({7, r::kUser}, Capability::UserKick, {1005, 1000, 1})); // sub-channels too
        QVERIFY(!p.allows({7, r::kUser}, Capability::UserKick, {2000, 1}));      // not elsewhere
        QVERIFY(!p.allows({7, r::kUser}, Capability::UserKick));                 // not server-wide
        QVERIFY(!p.allows({8, r::kUser}, Capability::UserKick, {1000, 1}));      // only that account
        // A guest (account 0) never matches account rules.
        const PermissionResolver q = resolverWith({rule(0, kAccount, 0, Capability::UserKick, true)});
        QVERIFY(!q.allows({0, r::kGuest}, Capability::UserKick));
    }

    void channelOnlyCapabilitiesAreReported()
    {
        const PermissionResolver p = resolverWith({
            rule(1000, kAccount, 7, Capability::UserKick, true),
            rule(1000, kRole, r::kUser, Capability::UserMute, true),
            rule(1000, kAccount, 7, Capability::UserMute, false),        // alice is excluded
            rule(1000, kRole, r::kUser, Capability::TextSend, true),     // already held server-wide
            rule(2000, kAccount, 8, Capability::UserBanTemporary, true), // someone else's
        });
        QCOMPARE(p.channelOnlyCapabilities({7, r::kUser}), QList<Capability>{Capability::UserKick});
        QCOMPARE(p.channelOnlyCapabilities({9, r::kUser}), QList<Capability>{Capability::UserMute});
        QVERIFY(p.channelOnlyCapabilities({1, r::kOwner}).isEmpty());
    }

    void serverRulesOverrideRoleGrants()
    {
        const PermissionResolver p = resolverWith({rule(0, kRole, r::kGuest, Capability::TextPrivate, false)});
        QVERIFY(!p.allows({0, r::kGuest}, Capability::TextPrivate));
        QVERIFY(!p.allows({0, r::kGuest}, Capability::TextPrivate, {1000, 1}));
        // Nobody can be granted an owner-only capability, not even by a rule.
        const PermissionResolver q = resolverWith({rule(0, kAccount, 9, Capability::OwnerTransfer, true)});
        QVERIFY(!q.allows({9, r::kAdministrator}, Capability::OwnerTransfer));
        // The owner cannot be denied anything.
        const PermissionResolver o = resolverWith({rule(0, kRole, r::kOwner, Capability::ChannelJoin, false)});
        QVERIFY(o.allows({1, r::kOwner}, Capability::ChannelJoin));
    }

    void sanctionRequirements()
    {
        auto req = requirementsFor(SanctionType::Ban, kDay, false);
        QVERIFY(req.capability == Capability::UserBanTemporary);
        QVERIFY(req.reasonRequired);
        QVERIFY(requirementsFor(SanctionType::Ban, 0, false).capability == Capability::UserBanPermanent);
        QVERIFY(requirementsFor(SanctionType::Ban, 60 * kDay, false).capability == Capability::UserBanPermanent);
        QVERIFY(requirementsFor(SanctionType::Ban, kDay, true).capability == Capability::UserBanAddress);
        QVERIFY(!requirementsFor(SanctionType::Mute, 10 * 60 * 1000, false).reasonRequired);
        QVERIFY(requirementsFor(SanctionType::Mute, 2 * 60 * 60 * 1000, false).reasonRequired);
        QVERIFY(requirementsFor(SanctionType::Mute, 0, false).reasonRequired); // until lifted
        QVERIFY(requirementsFor(SanctionType::Kick, 0, true).problem.has_value());
        QVERIFY(requirementsFor(SanctionType::Warning, -1, false).problem.has_value());
        QVERIFY(requirementsFor(SanctionType::Kick, 0, false).capability == Capability::UserKick);
    }

    void sanctionMatching()
    {
        SanctionBook book;
        Sanction accountBan = sanction(1, SanctionType::Ban, 0, 7, QStringLiteral("alice"));
        accountBan.expiresMs = 1000 + kDay;
        accountBan.createdMs = 1000;
        Sanction guestMute =
            sanction(2, SanctionType::VoiceMute, 0, 0, QStringLiteral("troll"), QStringLiteral("192.0.2.5"));
        Sanction channelBan = sanction(3, SanctionType::Ban, 1000, 8, QStringLiteral("bob"));
        Sanction addressBan = sanction(4, SanctionType::Ban, 0, 9, QStringLiteral("carol"));
        addressBan.targetAddress = QStringLiteral("198.51.100.1");
        Sanction restrict = sanction(5, SanctionType::Restrict, 0, 10, QStringLiteral("dave"));
        Sanction warning = sanction(6, SanctionType::Warning, 0, 11, QStringLiteral("erin"));
        book.setSanctions({accountBan, guestMute, channelBan, addressBan, restrict, warning});

        const qint64 now = 5000;
        QVERIFY(book.activeBan({7, QStringLiteral("203.0.113.9")}, {}, now).has_value());
        QVERIFY(!book.activeBan({7, {}}, {}, 1000 + kDay).has_value()); // expired by time
        // A guest mute follows the address, including in IPv4-mapped IPv6 form.
        QVERIFY(book.isVoiceMuted({0, QStringLiteral("::ffff:192.0.2.5")}, {}, now));
        QVERIFY(!book.isTextMuted({0, QStringLiteral("192.0.2.5")}, {}, now));
        QVERIFY(!book.isVoiceMuted({0, QStringLiteral("192.0.2.6")}, {}, now));
        // ...but a guest sanction does not catch accounts from that address.
        QVERIFY(!book.isVoiceMuted({12, QStringLiteral("192.0.2.5")}, {}, now));
        // A channel ban covers the channel and everything below it, not the server.
        QVERIFY(book.activeBan({8, {}}, {1000, 1}, now).has_value());
        QVERIFY(book.activeBan({8, {}}, {1003, 1000, 1}, now).has_value());
        QVERIFY(!book.activeBan({8, {}}, {2000, 1}, now).has_value());
        QVERIFY(!book.activeBan({8, {}}, {}, now).has_value());
        // An address ban catches other accounts from the same address.
        QVERIFY(book.activeBan({99, QStringLiteral("198.51.100.1")}, {}, now).has_value());
        QVERIFY(book.isRestricted({10, {}}, now));
        // Warnings block nothing.
        QVERIFY(book.activeFor({11, {}}, now).isEmpty());

        QVERIFY(book.resolve(1, SanctionState::Revoked, QStringLiteral("mod"), QStringLiteral("mistake"), now));
        QVERIFY(!book.resolve(1, SanctionState::Revoked, QStringLiteral("mod"), QString(), now)); // already
        QVERIFY(!book.activeBan({7, {}}, {}, now).has_value());
        QCOMPARE(book.find(1)->resolvedBy, QStringLiteral("mod"));
    }

    void expiry()
    {
        SanctionBook book;
        Sanction mute = sanction(1, SanctionType::Mute, 0, 7);
        mute.expiresMs = 100;
        Sanction permanent = sanction(2, SanctionType::Ban, 0, 8);
        book.setSanctions({mute, permanent});
        QVERIFY(book.expire(99).isEmpty());
        const QList<Sanction> expired = book.expire(100);
        QCOMPARE(expired.size(), 1);
        QCOMPARE(expired.first().id, quint64{1});
        QVERIFY(book.find(1)->state == SanctionState::Expired);
        QVERIFY(book.expire(1'000'000'000).isEmpty()); // permanent ones never expire
    }

    void auditChainDetectsTampering()
    {
        QList<AuditEntry> log;
        std::optional<AuditEntry> last;
        for (int i = 0; i < 5; ++i) {
            AuditEntry e;
            e.timeMs = 1000 + i;
            e.actorAccountId = 1;
            e.actorName = QStringLiteral("owner");
            e.action = QStringLiteral("sanction.ban");
            e.target = QStringLiteral("user%1").arg(i);
            e.reason = QStringLiteral("spam");
            last = chainAuditEntry(e, last);
            log.append(*last);
        }
        QCOMPARE(log.first().sequence, quint64{1});
        QVERIFY(log.first().previousHash.isEmpty());
        QCOMPARE(log[1].previousHash, log[0].hash);
        QVERIFY(verifyAuditChain(log).intact);
        QCOMPARE(verifyAuditChain(log).checked, quint64{5});
        // Verifying a later part on its own needs the entry before it.
        QVERIFY(verifyAuditChain(log.mid(2), log[1]).intact);
        QVERIFY(!verifyAuditChain(log.mid(2)).intact);

        QList<AuditEntry> changed = log;
        changed[2].reason = QStringLiteral("no reason");
        AuditVerification v = verifyAuditChain(changed);
        QVERIFY(!v.intact);
        QCOMPARE(v.firstBadSequence, std::optional<quint64>(3));
        QVERIFY(v.problem.contains(QStringLiteral("changed")));

        QList<AuditEntry> removed = log;
        removed.removeAt(3);
        QCOMPARE(verifyAuditChain(removed).firstBadSequence, std::optional<quint64>(5));

        QList<AuditEntry> reordered = log;
        reordered.swapItemsAt(1, 2);
        QVERIFY(!verifyAuditChain(reordered).intact);

        // Rewriting one entry and its own hash still breaks the link from the next one.
        QList<AuditEntry> rehashed = log;
        rehashed[2].reason = QStringLiteral("rewritten");
        rehashed[2].hash = computeAuditHash(rehashed[2]);
        QCOMPARE(verifyAuditChain(rehashed).firstBadSequence, std::optional<quint64>(4));

        QVERIFY(formatAuditEntry(log[0]).contains(QStringLiteral("sanction.ban")));
        QVERIFY(formatAuditEntry(log[0]).contains(QString::fromLatin1(log[0].hash.toHex())));
    }

    void appealSeparationOfDuties()
    {
        QVERIFY(!mayDecideAppeal(5, 5, true)); // issuer, others available
        QVERIFY(mayDecideAppeal(5, 5, false)); // issuer alone on the server
        QVERIFY(mayDecideAppeal(6, 5, true));  // someone else
        QVERIFY(mayDecideAppeal(0, 0, true));  // issued by the server itself
    }
};

QTEST_GUILESS_MAIN(TestModeration)
#include "tst_moderation.moc"
