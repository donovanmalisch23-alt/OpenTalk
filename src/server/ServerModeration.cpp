// SPDX-License-Identifier: GPL-3.0-only
// ServerCore, part 2: moderation and administration requests, and the helpers that
// apply roles, rules and sanctions.

#include "opentalk/logging/Logging.h"
#include "opentalk/server/PasswordWorker.h"
#include "opentalk/server/ServerCore.h"
#include "opentalk/server/ServerDatabase.h"

#include <algorithm>
#include <type_traits>

namespace opentalk::server {

using namespace protocol;
using moderation::Capability;
using moderation::Sanction;
using moderation::SanctionState;
using moderation::SanctionType;

namespace {

constexpr qint64 kSecondMs = 1000;

bool isModeratorView(const moderation::PermissionResolver &p, const moderation::Actor &actor)
{
    for (Capability c : {Capability::UserWarn, Capability::UserMute, Capability::UserKick, Capability::UserBanTemporary,
                         Capability::UserBanPermanent, Capability::SanctionRevoke, Capability::AuditRead}) {
        if (p.allows(actor, c)) {
            return true;
        }
    }
    return false;
}

} // namespace

// ------------------------------------------------------------------- dispatch

namespace {

template <typename T>
constexpr bool kIsAdminRequest =
    std::is_same_v<T, MoveUser> || std::is_same_v<T, ApplySanction> || std::is_same_v<T, RevokeSanction> ||
    std::is_same_v<T, ListRequest> || std::is_same_v<T, SetUserRole> || std::is_same_v<T, ReportUser> ||
    std::is_same_v<T, ResolveReport> || std::is_same_v<T, SubmitAppeal> || std::is_same_v<T, DecideAppeal> ||
    std::is_same_v<T, ReadAudit> || std::is_same_v<T, CreateAccount> || std::is_same_v<T, DeleteAccount> ||
    std::is_same_v<T, SetAccountPassword> || std::is_same_v<T, SaveRole> || std::is_same_v<T, DeleteRole> ||
    std::is_same_v<T, AddRule> || std::is_same_v<T, DeleteRule> || std::is_same_v<T, ServerSettingsRequest> ||
    std::is_same_v<T, GetServerStats> || std::is_same_v<T, TransferOwnership>;

} // namespace

bool ServerCore::handleAdminMessage(Client &client, const Message &message)
{
    return std::visit(
        [this, &client](const auto &m) -> bool {
            using T = std::decay_t<decltype(m)>;
            // Every branch returns, so no instantiation has unreachable code (MSVC C4702).
            if constexpr (!kIsAdminRequest<T>) {
                return false;
            } else {
                if constexpr (std::is_same_v<T, MoveUser>) {
                    moveOther(client, m);
                } else if constexpr (std::is_same_v<T, ApplySanction>) {
                    applySanction(client, m);
                } else if constexpr (std::is_same_v<T, RevokeSanction>) {
                    revokeSanction(client, m);
                } else if constexpr (std::is_same_v<T, ListRequest>) {
                    list(client, m);
                } else if constexpr (std::is_same_v<T, SetUserRole>) {
                    setUserRole(client, m);
                } else if constexpr (std::is_same_v<T, ReportUser>) {
                    reportUser(client, m);
                } else if constexpr (std::is_same_v<T, ResolveReport>) {
                    resolveReport(client, m);
                } else if constexpr (std::is_same_v<T, SubmitAppeal>) {
                    submitAppeal(client, m);
                } else if constexpr (std::is_same_v<T, DecideAppeal>) {
                    decideAppeal(client, m);
                } else if constexpr (std::is_same_v<T, ReadAudit>) {
                    readAudit(client, m);
                } else if constexpr (std::is_same_v<T, CreateAccount>) {
                    createAccount(client, m);
                } else if constexpr (std::is_same_v<T, DeleteAccount>) {
                    deleteAccount(client, m);
                } else if constexpr (std::is_same_v<T, SetAccountPassword>) {
                    setAccountPassword(client, m);
                } else if constexpr (std::is_same_v<T, SaveRole>) {
                    saveRole(client, m);
                } else if constexpr (std::is_same_v<T, DeleteRole>) {
                    deleteRole(client, m);
                } else if constexpr (std::is_same_v<T, AddRule>) {
                    addRule(client, m);
                } else if constexpr (std::is_same_v<T, DeleteRule>) {
                    deleteRule(client, m);
                } else if constexpr (std::is_same_v<T, ServerSettingsRequest>) {
                    serverSettings(client, m);
                } else if constexpr (std::is_same_v<T, GetServerStats>) {
                    serverStats(client, m);
                } else if constexpr (std::is_same_v<T, TransferOwnership>) {
                    transferOwnership(client, m);
                }
                return true;
            }
        },
        message);
}

// ---------------------------------------------------------------- helpers

moderation::Actor ServerCore::actorOf(const Client &client) const
{
    return {client.accountId, client.effectiveRole};
}

QList<quint32> ServerCore::chainOf(quint32 channelId) const
{
    QList<quint32> chain;
    quint32 id = channelId;
    for (int guard = 0; id != 0 && guard < 64; ++guard) {
        const auto it = m_channels.constFind(id);
        if (it == m_channels.cend()) {
            break;
        }
        chain.append(id);
        id = it->info.parentId;
    }
    return chain;
}

bool ServerCore::can(const Client &client, Capability capability, quint32 channelId) const
{
    return m_permissions.allows(actorOf(client), capability, chainOf(channelId));
}

moderation::SanctionSubject ServerCore::subjectOf(const Client &client) const
{
    return {client.accountId, client.address.toString()};
}

bool ServerCore::mayActOn(const Client &actor, moderation::RoleId targetRole, Capability capability,
                          quint32 channelId) const
{
    const auto actorRole = static_cast<moderation::RoleId>(actor.user.role);
    if (m_permissions.outranks(actorRole, targetRole)) {
        return true;
    }
    if (channelId == 0 || targetRole == moderation::roles::kOwner) {
        return false;
    }
    const moderation::Decision decision = m_permissions.check(actorOf(actor), capability, chainOf(channelId));
    return decision.allowed && decision.source == moderation::Decision::Source::AccountRule &&
           m_permissions.rankOf(actorRole) >= m_permissions.rankOf(targetRole);
}

QList<ServerCore::Client *> ServerCore::clientsMatching(const Sanction &sanction)
{
    QList<Client *> result;
    for (Client &c : m_clients) {
        if (c.state == ClientState::Online && moderation::SanctionBook::appliesTo(sanction, subjectOf(c))) {
            result.append(&c);
        }
    }
    return result;
}

void ServerCore::refreshUser(Client &client)
{
    if (client.state != ClientState::Online) {
        return;
    }
    const qint64 now = m_wallClock();
    const moderation::SanctionSubject subject = subjectOf(client);
    const QList<quint32> chain = chainOf(client.user.channelId);
    quint32 flags = client.bot ? quint32{BotFlag} : 0u;
    moderation::RoleId effective = static_cast<moderation::RoleId>(client.user.role);
    if (m_sanctions.isRestricted(subject, now) && client.user.role != Role::Owner) {
        flags |= RestrictedFlag;
        effective = moderation::roles::kRestricted;
    }
    if (m_sanctions.isVoiceMuted(subject, chain, now)) {
        flags |= VoiceMutedFlag;
    }
    if (m_sanctions.isTextMuted(subject, chain, now)) {
        flags |= TextMutedFlag;
    }
    const bool changed = flags != client.user.flags;
    client.user.flags = flags;
    client.effectiveRole = effective;
    QList<quint16> capabilities;
    for (Capability c : m_permissions.serverCapabilities(actorOf(client))) {
        capabilities.append(static_cast<quint16>(c));
    }
    QList<quint16> channelCapabilities;
    for (Capability c : m_permissions.channelOnlyCapabilities(actorOf(client))) {
        channelCapabilities.append(static_cast<quint16>(c));
    }
    if (capabilities != client.capabilities || channelCapabilities != client.channelCapabilities) {
        client.capabilities = capabilities;
        client.channelCapabilities = channelCapabilities;
        emit sendTo(client.id, CapabilitiesChanged{capabilities, channelCapabilities});
    }
    if (changed) {
        broadcast(UserUpdated{client.id, static_cast<quint32>(client.user.role), flags});
    }
}

void ServerCore::refreshAllUsers()
{
    for (Client &c : m_clients) {
        refreshUser(c);
    }
}

void ServerCore::reloadPermissions()
{
    if (m_database != nullptr) {
        QList<moderation::RoleDefinition> roles = m_database->roles();
        if (roles.isEmpty()) {
            roles = moderation::builtInRoles();
        }
        m_permissions.setRoles(roles);
        m_permissions.setRules(m_database->rules());
    }
}

QList<RoleInfo> ServerCore::roleInfos() const
{
    QList<RoleInfo> result;
    for (const moderation::RoleDefinition &role : m_permissions.roles()) {
        RoleInfo info{role.id, role.name, role.rank, {}, role.builtIn};
        for (Capability c : moderation::allCapabilities()) {
            if (role.grants.contains(c)) {
                info.capabilities.append(static_cast<quint16>(c));
            }
        }
        result.append(info);
    }
    return result;
}

void ServerCore::audit(const Client *actor, const QString &action, const QString &target, quint32 channelId,
                       const QString &reason, const QString &details)
{
    moderation::AuditEntry entry;
    entry.timeMs = m_wallClock();
    entry.actorAccountId = actor != nullptr ? actor->accountId : 0;
    // Bots act under their own accounts; the log says so.
    entry.actorName = actor == nullptr ? QString()
                      : actor->bot     ? actor->user.name + QStringLiteral(" (bot)")
                                       : actor->user.name;
    entry.action = action;
    entry.target = target.left(256);
    entry.channelId = channelId;
    entry.reason = reason.left(kMaxReasonLength);
    entry.details = details.left(1024);
    if (m_database != nullptr) {
        if (!m_database->appendAudit(entry)) {
            qCWarning(lcServer).noquote() << "Could not write the audit log entry" << action;
        }
    } else {
        entry = moderation::chainAuditEntry(entry, m_memoryAudit.isEmpty() ? std::nullopt
                                                                           : std::optional(m_memoryAudit.last()));
        m_memoryAudit.append(entry);
        while (m_memoryAudit.size() > 10000) {
            m_memoryAudit.removeFirst();
        }
    }
    qCInfo(lcServer).noquote() << "Audit:" << action << (actor != nullptr ? actor->user.name : QStringLiteral("server"))
                               << target;
}

void ServerCore::notifyHolders(Capability capability, const QString &text, qint64 exceptAccountId)
{
    for (const Client &c : std::as_const(m_clients)) {
        if (c.state == ClientState::Online && (exceptAccountId == 0 || c.accountId != exceptAccountId) &&
            can(c, capability)) {
            emit sendTo(c.id, TextMessage{TextTarget::User, 0, c.id, text, m_wallClock()});
        }
    }
}

std::optional<qint64> ServerCore::accountIdOf(const QString &name) const
{
    if (m_database == nullptr) {
        return std::nullopt;
    }
    const auto account = m_database->findAccount(name);
    return account ? std::optional<qint64>(account->id) : std::nullopt;
}

QString ServerCore::accountNameOf(qint64 accountId) const
{
    if (m_database == nullptr || accountId == 0) {
        return {};
    }
    const auto account = m_database->findAccountById(accountId);
    return account ? account->username : QString();
}

moderation::RoleId ServerCore::roleOfAccount(qint64 accountId) const
{
    if (m_database == nullptr || accountId == 0) {
        return moderation::roles::kGuest;
    }
    const auto account = m_database->findAccountById(accountId);
    return account ? static_cast<moderation::RoleId>(account->role) : moderation::roles::kGuest;
}

bool ServerCore::someoneElseCanDecideAppeals(qint64 exceptAccountId) const
{
    if (m_database == nullptr) {
        return false;
    }
    for (const Account &account : m_database->accounts()) {
        if (account.id != exceptAccountId && !account.disabled &&
            m_permissions.allows({account.id, static_cast<moderation::RoleId>(account.role)},
                                 Capability::AppealDecide)) {
            return true;
        }
    }
    return false;
}

SanctionInfo ServerCore::sanctionInfo(const Sanction &s, bool withAddress) const
{
    SanctionInfo info;
    info.id = s.id;
    info.type = static_cast<quint8>(s.type);
    info.channelId = s.channelId;
    info.targetName = s.targetName.isEmpty() ? QStringLiteral("(unknown)") : s.targetName.left(kMaxDisplayNameLength);
    info.targetAccount = accountNameOf(s.targetAccountId);
    info.targetAddress = withAddress ? s.targetAddress : QString();
    info.actorName = s.actorName.isEmpty() ? QStringLiteral("server") : s.actorName;
    info.reason = s.reason;
    info.createdMs = s.createdMs;
    info.expiresMs = s.expiresMs;
    info.state = static_cast<quint8>(s.state);
    info.resolvedBy = s.resolvedBy;
    info.resolution = s.resolution.left(kMaxReasonLength);
    return info;
}

void ServerCore::sendSanctionNotice(const Sanction &sanction)
{
    for (const Client *c : clientsMatching(sanction)) {
        emit sendTo(c->id, SanctionNotice{sanctionInfo(sanction, false)});
    }
}

void ServerCore::storeSanction(Sanction &sanction)
{
    if (m_database != nullptr) {
        if (!m_database->insertSanction(sanction)) {
            qCWarning(lcServer) << "Could not store a sanction";
        }
    } else {
        sanction.id = m_nextMemoryId++;
    }
    m_sanctions.add(sanction);
}

void ServerCore::updateSanction(const Sanction &sanction)
{
    if (m_database != nullptr) {
        m_database->updateSanction(sanction);
    }
}

void ServerCore::enforceSanction(const Sanction &sanction)
{
    const QList<Client *> affected = clientsMatching(sanction);
    const QList<quint32> ids = [&affected] {
        QList<quint32> result;
        for (const Client *c : affected) {
            result.append(c->id);
        }
        return result;
    }();
    for (quint32 id : ids) {
        Client *c = onlineClient(id);
        if (c == nullptr) {
            continue;
        }
        emit sendTo(id, SanctionNotice{sanctionInfo(sanction, false)});
        switch (sanction.type) {
        case SanctionType::Kick:
            if (sanction.channelId == 0) {
                emit closeSession(id, ErrorCode::Kicked,
                                  sanction.reason.isEmpty()
                                      ? QStringLiteral("you were kicked")
                                      : QStringLiteral("you were kicked: %1").arg(sanction.reason));
            } else if (chainOf(c->user.channelId).contains(sanction.channelId)) {
                moveUser(*c, kRootChannelId);
            }
            break;
        case SanctionType::Ban:
            if (sanction.channelId == 0) {
                emit closeSession(id, ErrorCode::Banned, describeBan(sanction));
            } else if (chainOf(c->user.channelId).contains(sanction.channelId)) {
                moveUser(*c, kRootChannelId);
            }
            break;
        case SanctionType::Warning:
            break;
        case SanctionType::VoiceMute:
        case SanctionType::TextMute:
        case SanctionType::Mute:
        case SanctionType::Restrict:
            refreshUser(*c);
            break;
        }
    }
}

void ServerCore::applySettings(const ServerSettingsInfo &settings, bool persist)
{
    m_settings = settings;
    if (persist && m_database != nullptr) {
        m_database->setSetting(QStringLiteral("server.name"), settings.name);
        m_database->setSetting(QStringLiteral("server.welcomeMessage"), settings.welcomeMessage);
        m_database->setSetting(QStringLiteral("server.contact"), settings.contact);
        m_database->setSetting(QStringLiteral("server.allowGuests"),
                               settings.allowGuests ? QStringLiteral("1") : QStringLiteral("0"));
        m_database->setSetting(QStringLiteral("server.maxUsers"), QString::number(settings.maxUsers));
        m_database->setSetting(QStringLiteral("server.maxVoiceBitrate"), QString::number(settings.maxVoiceBitrate));
        m_database->setSetting(QStringLiteral("server.maxTemporaryBanDays"),
                               QString::number(settings.maxTemporaryBanDays));
    }
    emit settingsChanged(m_settings);
}

// ------------------------------------------------------------------ moving

void ServerCore::moveOther(Client &actor, const MoveUser &request)
{
    Client *target = onlineClient(request.userId);
    if (target == nullptr || !m_channels.contains(request.channelId)) {
        refuse(actor.id, request.requestId, ErrorCode::NotFound,
               target == nullptr ? QStringLiteral("that user is not online") : QStringLiteral("no such channel"));
        return;
    }
    // Needs the right where the person is and where they are going.
    if (!can(actor, Capability::UserMove, target->user.channelId)) {
        refuseUnauthorized(actor, request.requestId, Capability::UserMove, target->user.channelId);
        return;
    }
    if (!can(actor, Capability::UserMove, request.channelId)) {
        refuseUnauthorized(actor, request.requestId, Capability::UserMove, request.channelId);
        return;
    }
    if (target->id != actor.id && !mayActOn(actor, static_cast<moderation::RoleId>(target->user.role),
                                            Capability::UserMove, target->user.channelId)) {
        refuse(actor.id, request.requestId, ErrorCode::NotAuthorized,
               QStringLiteral("you can only move people whose role ranks below yours"));
        return;
    }
    audit(&actor, QStringLiteral("user.moved"), target->user.name, request.channelId);
    moveUser(*target, request.channelId);
    reply(actor.id, request.requestId);
}

// --------------------------------------------------------------- sanctions

void ServerCore::applySanction(Client &actor, const ApplySanction &request)
{
    if (request.type > static_cast<quint8>(SanctionType::Restrict)) {
        refuse(actor.id, request.requestId, ErrorCode::InvalidRequest, QStringLiteral("unknown sanction type"));
        return;
    }
    const auto type = static_cast<SanctionType>(request.type);
    if (request.channelId != 0 && !m_channels.contains(request.channelId)) {
        refuse(actor.id, request.requestId, ErrorCode::NotFound, QStringLiteral("no such channel"));
        return;
    }
    if (request.channelId != 0 && type == SanctionType::Restrict) {
        refuse(actor.id, request.requestId, ErrorCode::InvalidRequest,
               QStringLiteral("restrictions apply to the whole server"));
        return;
    }
    const qint64 durationMs = static_cast<qint64>(request.durationSeconds) * kSecondMs;
    moderation::SanctionPolicy policy;
    policy.maxTemporaryBanMs = static_cast<qint64>(m_settings.maxTemporaryBanDays) * 24 * 3600 * kSecondMs;
    const moderation::SanctionRequirements requirements =
        moderation::requirementsFor(type, durationMs, request.banAddress, policy);
    if (requirements.problem) {
        refuse(actor.id, request.requestId, ErrorCode::InvalidRequest, *requirements.problem);
        return;
    }
    if (!can(actor, requirements.capability, request.channelId)) {
        refuseUnauthorized(actor, request.requestId, requirements.capability, request.channelId);
        return;
    }
    if (requirements.reasonRequired && request.reason.trimmed().isEmpty()) {
        refuse(actor.id, request.requestId, ErrorCode::InvalidRequest,
               QStringLiteral("a reason is required for this sanction"));
        return;
    }

    Sanction sanction;
    sanction.type = type;
    sanction.channelId = request.channelId;
    sanction.actorAccountId = actor.accountId;
    sanction.actorName = actor.user.name;
    sanction.reason = request.reason.trimmed();
    sanction.createdMs = m_wallClock();
    sanction.expiresMs = sanction.isLasting() && durationMs > 0 ? sanction.createdMs + durationMs : 0;
    if (!sanction.isLasting()) {
        sanction.expiresMs = sanction.createdMs;
    }

    if (request.userId != 0) {
        const Client *target = onlineClient(request.userId);
        if (target == nullptr) {
            refuse(actor.id, request.requestId, ErrorCode::NotFound, QStringLiteral("that user is not online"));
            return;
        }
        sanction.targetAccountId = target->accountId;
        sanction.targetName = target->user.name;
        sanction.targetRole = static_cast<moderation::RoleId>(target->user.role);
        if (target->accountId == 0 || request.banAddress) {
            sanction.targetAddress = target->address.toString();
        }
        if (target->id == actor.id) {
            refuse(actor.id, request.requestId, ErrorCode::InvalidRequest,
                   QStringLiteral("you cannot sanction yourself"));
            return;
        }
    } else {
        if (m_database == nullptr) {
            refuse(actor.id, request.requestId, ErrorCode::NotFound, QStringLiteral("this server has no accounts"));
            return;
        }
        const auto account = m_database->findAccount(request.account);
        if (!account) {
            refuse(actor.id, request.requestId, ErrorCode::NotFound, QStringLiteral("no such account"));
            return;
        }
        if (request.banAddress) {
            refuse(actor.id, request.requestId, ErrorCode::InvalidRequest,
                   QStringLiteral("an address can only be banned while the person is online"));
            return;
        }
        if (type == SanctionType::Kick) {
            refuse(actor.id, request.requestId, ErrorCode::NotFound, QStringLiteral("that user is not online"));
            return;
        }
        if (account->id == actor.accountId) {
            refuse(actor.id, request.requestId, ErrorCode::InvalidRequest,
                   QStringLiteral("you cannot sanction yourself"));
            return;
        }
        sanction.targetAccountId = account->id;
        sanction.targetName = account->username;
        sanction.targetRole = static_cast<moderation::RoleId>(account->role);
    }
    if (!mayActOn(actor, sanction.targetRole, requirements.capability, sanction.channelId)) {
        refuse(actor.id, request.requestId, ErrorCode::NotAuthorized,
               QStringLiteral("you can only sanction people whose role ranks below yours"));
        return;
    }

    storeSanction(sanction);
    QString details = QStringLiteral("sanction=%1").arg(sanction.id);
    if (sanction.expiresMs != 0 && sanction.isLasting()) {
        details += QStringLiteral("; durationSeconds=%1").arg(request.durationSeconds);
    } else if (sanction.isLasting()) {
        details += QStringLiteral("; until lifted");
    }
    if (!sanction.targetAddress.isEmpty() && request.banAddress) {
        details += QStringLiteral("; address=%1").arg(sanction.targetAddress);
    }
    audit(&actor, QStringLiteral("sanction.") + moderation::toString(type).replace(QLatin1Char(' '), QLatin1Char('-')),
          sanction.targetName, sanction.channelId, sanction.reason, details);
    reply(actor.id, request.requestId, static_cast<quint32>(sanction.id));
    enforceSanction(sanction);
}

void ServerCore::revokeSanction(Client &actor, const RevokeSanction &request)
{
    const std::optional<Sanction> sanction = m_sanctions.find(request.sanctionId);
    if (!sanction || sanction->state != SanctionState::Active || !sanction->isLasting()) {
        refuse(actor.id, request.requestId, ErrorCode::NotFound, QStringLiteral("no such active sanction"));
        return;
    }
    if (!can(actor, Capability::SanctionRevoke, sanction->channelId)) {
        refuseUnauthorized(actor, request.requestId, Capability::SanctionRevoke, sanction->channelId);
        return;
    }
    if (!mayActOn(actor, sanction->targetRole, Capability::SanctionRevoke, sanction->channelId)) {
        refuse(actor.id, request.requestId, ErrorCode::NotAuthorized,
               QStringLiteral("you can only lift sanctions on people whose role ranks below yours"));
        return;
    }
    m_sanctions.resolve(sanction->id, SanctionState::Revoked, actor.user.name, request.reason, m_wallClock());
    const Sanction updated = *m_sanctions.find(sanction->id);
    updateSanction(updated);
    audit(&actor, QStringLiteral("sanction.revoked"), updated.targetName, updated.channelId, request.reason,
          QStringLiteral("sanction=%1").arg(updated.id));
    reply(actor.id, request.requestId);
    sendSanctionNotice(updated);
    for (Client *c : clientsMatching(updated)) {
        refreshUser(*c);
    }
}

// ------------------------------------------------------------------- lists

void ServerCore::list(Client &actor, const ListRequest &request)
{
    ListResult result;
    result.requestId = request.requestId;
    result.kind = request.kind;
    const moderation::Actor who = actorOf(actor);
    const qint64 now = m_wallClock();
    switch (request.kind) {
    case ListKind::Sanctions: {
        const bool all = isModeratorView(m_permissions, who);
        const bool addresses = can(actor, Capability::UserBanAddress);
        const moderation::SanctionSubject self = subjectOf(actor);
        for (const Sanction &s : m_sanctions.sanctions()) {
            const bool mine = moderation::SanctionBook::appliesTo(s, self);
            if (!all && !mine) {
                continue;
            }
            if (!request.includeClosed && !s.isActiveAt(now)) {
                continue;
            }
            result.sanctions.append(sanctionInfo(s, addresses));
        }
        break;
    }
    case ListKind::Reports:
        if (!can(actor, Capability::ReportReview)) {
            refuseUnauthorized(actor, request.requestId, Capability::ReportReview);
            return;
        }
        for (const moderation::Report &r : std::as_const(m_reports)) {
            if (request.includeClosed || r.state == moderation::Report::State::Open) {
                result.reports.append(ReportInfo{r.id, r.reporterName, r.targetName, accountNameOf(r.targetAccountId),
                                                 r.reason, r.createdMs, static_cast<quint8>(r.state), r.handledBy,
                                                 r.resolution});
            }
        }
        break;
    case ListKind::Appeals: {
        const bool all = can(actor, Capability::AppealDecide);
        for (const moderation::Appeal &a : std::as_const(m_appeals)) {
            const bool mine = actor.accountId != 0 && a.appellantAccountId == actor.accountId;
            if ((all || mine) && (request.includeClosed || a.state == moderation::Appeal::State::Open)) {
                result.appeals.append(AppealInfo{a.id, a.sanctionId, a.appellantName, a.text, a.createdMs,
                                                 static_cast<quint8>(a.state), a.decidedBy, a.decisionReason});
            }
        }
        break;
    }
    case ListKind::Accounts:
        if (!can(actor, Capability::AccountManage) && !can(actor, Capability::RoleAssign)) {
            refuseUnauthorized(actor, request.requestId, Capability::AccountManage);
            return;
        }
        if (m_database != nullptr) {
            for (const Account &a : m_database->accounts()) {
                result.accounts.append(AccountInfo{a.username, static_cast<quint32>(a.role), a.disabled, a.bot});
            }
        }
        break;
    case ListKind::Rules:
        if (!can(actor, Capability::RoleManage)) {
            refuseUnauthorized(actor, request.requestId, Capability::RoleManage);
            return;
        }
        for (const moderation::PermissionRule &rule : m_permissions.rules()) {
            RuleInfo info;
            info.id = rule.id;
            info.channelId = rule.channelId;
            info.forAccount = rule.subject == moderation::PermissionRule::Subject::Account;
            info.account = info.forAccount ? accountNameOf(rule.subjectId) : QString();
            info.roleId = info.forAccount ? 0 : static_cast<quint32>(rule.subjectId);
            info.capability = static_cast<quint16>(rule.capability);
            info.allow = rule.allow;
            if (info.forAccount && info.account.isEmpty()) {
                continue; // the account was deleted
            }
            result.rules.append(info);
        }
        break;
    }
    emit sendTo(actor.id, result);
}

// ------------------------------------------------------------------- roles

void ServerCore::setUserRole(Client &actor, const SetUserRole &request)
{
    if (!can(actor, Capability::RoleAssign)) {
        refuseUnauthorized(actor, request.requestId, Capability::RoleAssign);
        return;
    }
    const auto account = m_database != nullptr ? m_database->findAccount(request.account) : std::nullopt;
    if (!account) {
        refuse(actor.id, request.requestId, ErrorCode::NotFound, QStringLiteral("no such account"));
        return;
    }
    const auto newRole = m_permissions.role(request.roleId);
    if (!newRole || request.roleId == moderation::roles::kGuest || request.roleId == moderation::roles::kOwner) {
        refuse(actor.id, request.requestId, ErrorCode::InvalidRequest,
               request.roleId == moderation::roles::kOwner ? QStringLiteral("use ownership transfer for the owner role")
                                                           : QStringLiteral("no such role"));
        return;
    }
    const auto actorRole = static_cast<moderation::RoleId>(actor.user.role);
    if (!m_permissions.outranks(actorRole, static_cast<moderation::RoleId>(account->role)) ||
        !m_permissions.outranks(actorRole, request.roleId)) {
        refuse(actor.id, request.requestId, ErrorCode::NotAuthorized,
               QStringLiteral("you can only give roles below your own, to people below you"));
        return;
    }
    if (!m_database->setRole(account->username, static_cast<Role>(request.roleId))) {
        refuse(actor.id, request.requestId, ErrorCode::InternalError, QStringLiteral("could not change the role"));
        return;
    }
    audit(&actor, QStringLiteral("role.assigned"), account->username, 0, QString(),
          QStringLiteral("from=%1; to=%2")
              .arg(m_permissions.role(static_cast<moderation::RoleId>(account->role)).value_or(*newRole).name,
                   newRole->name));
    reply(actor.id, request.requestId);
    if (Client *target = onlineAccount(account->id)) {
        target->user.role = static_cast<Role>(request.roleId);
        broadcast(UserUpdated{target->id, request.roleId, target->user.flags});
        refreshUser(*target);
    }
}

void ServerCore::saveRole(Client &actor, const SaveRole &request)
{
    if (!can(actor, Capability::RoleManage)) {
        refuseUnauthorized(actor, request.requestId, Capability::RoleManage);
        return;
    }
    if (m_database == nullptr) {
        refuse(actor.id, request.requestId, ErrorCode::InvalidRequest, QStringLiteral("this server has no database"));
        return;
    }
    const auto actorRole = static_cast<moderation::RoleId>(actor.user.role);
    const int actorRank = m_permissions.rankOf(actorRole);
    moderation::RoleDefinition role;
    const RoleInfo &wanted = request.role;
    if (wanted.id != 0) {
        const auto existing = m_permissions.role(wanted.id);
        if (!existing || existing->id == moderation::roles::kOwner) {
            refuse(actor.id, request.requestId, ErrorCode::NotFound,
                   existing ? QStringLiteral("the owner role cannot be changed") : QStringLiteral("no such role"));
            return;
        }
        if (!m_permissions.outranks(actorRole, existing->id)) {
            refuse(actor.id, request.requestId, ErrorCode::NotAuthorized,
                   QStringLiteral("you can only change roles below your own"));
            return;
        }
        role = *existing;
    }
    if (!role.builtIn) {
        role.name = wanted.name.trimmed();
        role.rank = wanted.rank;
        if (role.rank < 2 || role.rank >= actorRank || role.rank >= moderation::roles::kAdministratorRank) {
            refuse(actor.id, request.requestId, ErrorCode::InvalidRequest,
                   QStringLiteral("custom roles need a rank from 2 to %1")
                       .arg(std::min(actorRank, moderation::roles::kAdministratorRank) - 1));
            return;
        }
    }
    // You can only hand out what you can do yourself.
    QSet<Capability> grants;
    for (quint16 value : wanted.capabilities) {
        const auto c = moderation::capabilityFromValue(value);
        if (!c || moderation::isOwnerOnly(*c)) {
            refuse(actor.id, request.requestId, ErrorCode::InvalidRequest,
                   QStringLiteral("unknown capability %1").arg(value));
            return;
        }
        if (!can(actor, *c)) {
            refuse(actor.id, request.requestId, ErrorCode::NotAuthorized,
                   QStringLiteral("you cannot grant %1, which you do not have").arg(moderation::capabilityName(*c)));
            return;
        }
        grants.insert(*c);
    }
    role.grants = grants;
    QString error;
    const bool created = role.id == 0;
    if (!m_database->saveRole(role, &error)) {
        refuse(actor.id, request.requestId, ErrorCode::InvalidRequest, error);
        return;
    }
    reloadPermissions();
    audit(&actor, created ? QStringLiteral("role.created") : QStringLiteral("role.changed"), role.name, 0, QString(),
          QStringLiteral("role=%1; rank=%2; capabilities=%3").arg(role.id).arg(role.rank).arg(grants.size()));
    reply(actor.id, request.requestId, role.id);
    broadcast(RolesChanged{roleInfos()});
    refreshAllUsers();
}

void ServerCore::deleteRole(Client &actor, const DeleteRole &request)
{
    if (!can(actor, Capability::RoleManage)) {
        refuseUnauthorized(actor, request.requestId, Capability::RoleManage);
        return;
    }
    const auto role = m_permissions.role(request.roleId);
    if (!role || role->builtIn || m_database == nullptr) {
        refuse(actor.id, request.requestId, role ? ErrorCode::InvalidRequest : ErrorCode::NotFound,
               role ? QStringLiteral("built-in roles cannot be deleted") : QStringLiteral("no such role"));
        return;
    }
    if (!m_permissions.outranks(static_cast<moderation::RoleId>(actor.user.role), role->id)) {
        refuse(actor.id, request.requestId, ErrorCode::NotAuthorized,
               QStringLiteral("you can only delete roles below your own"));
        return;
    }
    if (const int holders = m_database->accountsWithRole(role->id); holders > 0) {
        refuse(actor.id, request.requestId, ErrorCode::InvalidRequest,
               QStringLiteral("%1 account(s) still have this role; give them another role first").arg(holders));
        return;
    }
    m_database->deleteRole(role->id);
    reloadPermissions();
    audit(&actor, QStringLiteral("role.deleted"), role->name, 0, QString(), QStringLiteral("role=%1").arg(role->id));
    reply(actor.id, request.requestId);
    broadcast(RolesChanged{roleInfos()});
    refreshAllUsers();
}

void ServerCore::addRule(Client &actor, const AddRule &request)
{
    const RuleInfo &wanted = request.rule;
    if (wanted.channelId != 0 && !m_channels.contains(wanted.channelId)) {
        refuse(actor.id, request.requestId, ErrorCode::NotFound, QStringLiteral("no such channel"));
        return;
    }
    if (!can(actor, Capability::RoleManage, wanted.channelId)) {
        refuseUnauthorized(actor, request.requestId, Capability::RoleManage, wanted.channelId);
        return;
    }
    if (m_database == nullptr) {
        refuse(actor.id, request.requestId, ErrorCode::InvalidRequest, QStringLiteral("this server has no database"));
        return;
    }
    const auto capability = moderation::capabilityFromValue(wanted.capability);
    if (!capability || moderation::isOwnerOnly(*capability)) {
        refuse(actor.id, request.requestId, ErrorCode::InvalidRequest, QStringLiteral("unknown capability"));
        return;
    }
    if (wanted.allow && !can(actor, *capability, wanted.channelId)) {
        refuse(actor.id, request.requestId, ErrorCode::NotAuthorized,
               QStringLiteral("you cannot grant %1, which you do not have there")
                   .arg(moderation::capabilityName(*capability)));
        return;
    }
    if (wanted.channelId != 0 && !m_channels.value(wanted.channelId).info.persistent) {
        refuse(actor.id, request.requestId, ErrorCode::InvalidRequest,
               QStringLiteral("rules can only be set on permanent channels"));
        return;
    }
    moderation::PermissionRule rule;
    rule.channelId = wanted.channelId;
    rule.capability = *capability;
    rule.allow = wanted.allow;
    moderation::RoleId subjectRole = 0;
    QString subjectName;
    if (wanted.forAccount) {
        const auto account = m_database->findAccount(wanted.account);
        if (!account) {
            refuse(actor.id, request.requestId, ErrorCode::NotFound, QStringLiteral("no such account"));
            return;
        }
        rule.subject = moderation::PermissionRule::Subject::Account;
        rule.subjectId = account->id;
        subjectRole = static_cast<moderation::RoleId>(account->role);
        subjectName = account->username;
    } else {
        const auto role = m_permissions.role(wanted.roleId);
        if (!role) {
            refuse(actor.id, request.requestId, ErrorCode::NotFound, QStringLiteral("no such role"));
            return;
        }
        rule.subject = moderation::PermissionRule::Subject::Role;
        rule.subjectId = role->id;
        subjectRole = role->id;
        subjectName = QStringLiteral("role ") + role->name;
    }
    if (!m_permissions.outranks(static_cast<moderation::RoleId>(actor.user.role), subjectRole)) {
        refuse(actor.id, request.requestId, ErrorCode::NotAuthorized,
               QStringLiteral("you can only set rules for roles and people below your own"));
        return;
    }
    if (!m_database->insertRule(rule)) {
        refuse(actor.id, request.requestId, ErrorCode::InternalError, QStringLiteral("could not save the rule"));
        return;
    }
    reloadPermissions();
    audit(&actor, QStringLiteral("rule.added"), subjectName, rule.channelId, QString(),
          QStringLiteral("rule=%1; %2 %3")
              .arg(rule.id)
              .arg(rule.allow ? QStringLiteral("allow") : QStringLiteral("deny"),
                   moderation::capabilityName(rule.capability)));
    reply(actor.id, request.requestId, static_cast<quint32>(rule.id));
    refreshAllUsers();
}

void ServerCore::deleteRule(Client &actor, const DeleteRule &request)
{
    const auto rules = m_permissions.rules();
    const auto it = std::find_if(rules.cbegin(), rules.cend(),
                                 [&](const moderation::PermissionRule &r) { return r.id == request.ruleId; });
    if (it == rules.cend() || m_database == nullptr) {
        refuse(actor.id, request.requestId, ErrorCode::NotFound, QStringLiteral("no such rule"));
        return;
    }
    if (!can(actor, Capability::RoleManage, it->channelId)) {
        refuseUnauthorized(actor, request.requestId, Capability::RoleManage, it->channelId);
        return;
    }
    const moderation::RoleId subjectRole = it->subject == moderation::PermissionRule::Subject::Role
                                               ? static_cast<moderation::RoleId>(it->subjectId)
                                               : roleOfAccount(it->subjectId);
    if (!m_permissions.outranks(static_cast<moderation::RoleId>(actor.user.role), subjectRole)) {
        refuse(actor.id, request.requestId, ErrorCode::NotAuthorized,
               QStringLiteral("you can only remove rules for roles and people below your own"));
        return;
    }
    m_database->deleteRule(it->id);
    const moderation::PermissionRule removed = *it;
    reloadPermissions();
    audit(&actor, QStringLiteral("rule.deleted"),
          removed.subject == moderation::PermissionRule::Subject::Account
              ? accountNameOf(removed.subjectId)
              : QStringLiteral("role %1").arg(removed.subjectId),
          removed.channelId, QString(), QStringLiteral("rule=%1").arg(removed.id));
    reply(actor.id, request.requestId);
    refreshAllUsers();
}

// ------------------------------------------------------- reports and appeals

void ServerCore::reportUser(Client &actor, const ReportUser &request)
{
    const Client *target = onlineClient(request.userId);
    if (target == nullptr || target->id == actor.id) {
        refuse(actor.id, request.requestId, ErrorCode::NotFound, QStringLiteral("that user is not online"));
        return;
    }
    if (request.reason.trimmed().isEmpty()) {
        refuse(actor.id, request.requestId, ErrorCode::InvalidRequest, QStringLiteral("say what happened"));
        return;
    }
    // Reports share the chat flood limit, so they cannot be used to spam moderators.
    const qint64 now = m_now();
    actor.textTokens = std::min(m_config.textBurst, actor.textTokens + static_cast<double>(now - actor.textRefillMs) /
                                                                           1000.0 * m_config.textPerSecond);
    actor.textRefillMs = now;
    if (actor.textTokens < 1.0) {
        refuse(actor.id, request.requestId, ErrorCode::RateLimited, QStringLiteral("you are sending too fast"));
        return;
    }
    actor.textTokens -= 1.0;
    moderation::Report report;
    report.reporterAccountId = actor.accountId;
    report.reporterName = actor.user.name;
    report.targetAccountId = target->accountId;
    report.targetName = target->user.name;
    report.targetAddress = target->address.toString();
    report.reason = request.reason.trimmed();
    report.createdMs = m_wallClock();
    if (m_database != nullptr) {
        m_database->insertReport(report);
    } else {
        report.id = m_nextMemoryId++;
    }
    m_reports.append(report);
    audit(&actor, QStringLiteral("report.created"), report.targetName, 0, report.reason,
          QStringLiteral("report=%1").arg(report.id));
    reply(actor.id, request.requestId, static_cast<quint32>(report.id));
    notifyHolders(Capability::ReportReview,
                  QStringLiteral("New report about %1: %2").arg(report.targetName, report.reason), 0);
}

void ServerCore::resolveReport(Client &actor, const ResolveReport &request)
{
    if (!can(actor, Capability::ReportReview)) {
        refuseUnauthorized(actor, request.requestId, Capability::ReportReview);
        return;
    }
    auto it = std::find_if(m_reports.begin(), m_reports.end(),
                           [&](const moderation::Report &r) { return r.id == request.reportId; });
    if (it == m_reports.end() || it->state != moderation::Report::State::Open) {
        refuse(actor.id, request.requestId, ErrorCode::NotFound, QStringLiteral("no such open report"));
        return;
    }
    if (request.state != static_cast<quint8>(moderation::Report::State::ActionTaken) &&
        request.state != static_cast<quint8>(moderation::Report::State::Dismissed)) {
        refuse(actor.id, request.requestId, ErrorCode::InvalidRequest, QStringLiteral("unknown report outcome"));
        return;
    }
    it->state = static_cast<moderation::Report::State>(request.state);
    it->handledBy = actor.user.name;
    it->resolution = request.resolution;
    it->handledMs = m_wallClock();
    if (m_database != nullptr) {
        m_database->updateReport(*it);
    }
    audit(&actor, QStringLiteral("report.resolved"), it->targetName, 0, request.resolution,
          QStringLiteral("report=%1; outcome=%2").arg(it->id).arg(moderation::toString(it->state)));
    reply(actor.id, request.requestId);
}

void ServerCore::submitAppeal(Client &actor, const SubmitAppeal &request)
{
    const std::optional<Sanction> sanction = m_sanctions.find(request.sanctionId);
    if (!sanction || !moderation::SanctionBook::appliesTo(*sanction, subjectOf(actor)) ||
        sanction->state == SanctionState::Revoked || sanction->state == SanctionState::Overturned) {
        refuse(actor.id, request.requestId, ErrorCode::NotFound, QStringLiteral("no such sanction on you"));
        return;
    }
    if (actor.accountId == 0) {
        refuse(actor.id, request.requestId, ErrorCode::NotAuthorized,
               QStringLiteral("guests cannot appeal; contact the server's team instead"));
        return;
    }
    const bool exists = std::any_of(m_appeals.cbegin(), m_appeals.cend(),
                                    [&](const moderation::Appeal &a) { return a.sanctionId == sanction->id; });
    if (exists) {
        refuse(actor.id, request.requestId, ErrorCode::InvalidRequest,
               QStringLiteral("this sanction has already been appealed"));
        return;
    }
    moderation::Appeal appeal;
    appeal.sanctionId = sanction->id;
    appeal.appellantAccountId = actor.accountId;
    appeal.appellantName = actor.user.name;
    appeal.text = request.text;
    appeal.createdMs = m_wallClock();
    if (m_database != nullptr) {
        if (!m_database->insertAppeal(appeal)) {
            refuse(actor.id, request.requestId, ErrorCode::InternalError, QStringLiteral("could not save the appeal"));
            return;
        }
    } else {
        appeal.id = m_nextMemoryId++;
    }
    m_appeals.append(appeal);
    audit(&actor, QStringLiteral("appeal.submitted"), sanction->targetName, sanction->channelId, QString(),
          QStringLiteral("appeal=%1; sanction=%2").arg(appeal.id).arg(sanction->id));
    reply(actor.id, request.requestId, static_cast<quint32>(appeal.id));
    notifyHolders(
        Capability::AppealDecide,
        QStringLiteral("New appeal from %1 against a %2.").arg(actor.user.name, moderation::toString(sanction->type)),
        actor.accountId);
}

void ServerCore::decideAppeal(Client &actor, const DecideAppeal &request)
{
    if (!can(actor, Capability::AppealDecide)) {
        refuseUnauthorized(actor, request.requestId, Capability::AppealDecide);
        return;
    }
    auto it = std::find_if(m_appeals.begin(), m_appeals.end(),
                           [&](const moderation::Appeal &a) { return a.id == request.appealId; });
    if (it == m_appeals.end() || it->state != moderation::Appeal::State::Open) {
        refuse(actor.id, request.requestId, ErrorCode::NotFound, QStringLiteral("no such open appeal"));
        return;
    }
    const std::optional<Sanction> sanction = m_sanctions.find(it->sanctionId);
    if (!sanction) {
        refuse(actor.id, request.requestId, ErrorCode::NotFound, QStringLiteral("the sanction no longer exists"));
        return;
    }
    if (it->appellantAccountId == actor.accountId) {
        refuse(actor.id, request.requestId, ErrorCode::NotAuthorized,
               QStringLiteral("you cannot decide your own appeal"));
        return;
    }
    if (!moderation::mayDecideAppeal(actor.accountId, sanction->actorAccountId,
                                     someoneElseCanDecideAppeals(sanction->actorAccountId))) {
        refuse(actor.id, request.requestId, ErrorCode::NotAuthorized,
               QStringLiteral("someone other than the moderator who issued the sanction must decide this appeal"));
        return;
    }
    const auto decision = static_cast<moderation::Appeal::State>(request.decision);
    if (decision != moderation::Appeal::State::Upheld && decision != moderation::Appeal::State::Reduced &&
        decision != moderation::Appeal::State::Overturned) {
        refuse(actor.id, request.requestId, ErrorCode::InvalidRequest, QStringLiteral("unknown decision"));
        return;
    }
    const qint64 now = m_wallClock();
    Sanction changed = *sanction;
    if (decision == moderation::Appeal::State::Reduced) {
        const qint64 newExpiry = sanction->createdMs + static_cast<qint64>(request.newDurationSeconds) * kSecondMs;
        const bool shorter = sanction->expiresMs == 0 || newExpiry < sanction->expiresMs;
        if (!sanction->isLasting() || request.newDurationSeconds == 0 || !shorter) {
            refuse(actor.id, request.requestId, ErrorCode::InvalidRequest,
                   QStringLiteral("a reduction needs a shorter, non-zero duration for a lasting sanction"));
            return;
        }
        changed.expiresMs = newExpiry;
        // Find and update the stored sanction (it may now be over).
        QList<Sanction> all = m_sanctions.sanctions();
        for (Sanction &s : all) {
            if (s.id == changed.id) {
                s.expiresMs = newExpiry;
            }
        }
        m_sanctions.setSanctions(all);
        updateSanction(changed);
        tick();
    } else if (decision == moderation::Appeal::State::Overturned && sanction->state == SanctionState::Active) {
        m_sanctions.resolve(sanction->id, SanctionState::Overturned, actor.user.name, request.reason, now);
        changed = *m_sanctions.find(sanction->id);
        updateSanction(changed);
    } else if (decision == moderation::Appeal::State::Overturned) {
        // An event (warning, kick) or an expired sanction: record the outcome on it.
        QList<Sanction> all = m_sanctions.sanctions();
        for (Sanction &s : all) {
            if (s.id == changed.id) {
                s.state = SanctionState::Overturned;
                s.resolvedBy = actor.user.name;
                s.resolution = request.reason;
                s.resolvedMs = now;
                changed = s;
            }
        }
        m_sanctions.setSanctions(all);
        updateSanction(changed);
    }
    it->state = decision;
    it->decidedBy = actor.user.name;
    it->decisionReason = request.reason;
    it->decidedMs = now;
    if (m_database != nullptr) {
        m_database->updateAppeal(*it);
    }
    audit(&actor, QStringLiteral("appeal.decided"), it->appellantName, sanction->channelId, request.reason,
          QStringLiteral("appeal=%1; sanction=%2; decision=%3")
              .arg(it->id)
              .arg(sanction->id)
              .arg(moderation::toString(decision)));
    reply(actor.id, request.requestId);
    if (Client *appellant = onlineAccount(it->appellantAccountId)) {
        emit sendTo(appellant->id, SanctionNotice{sanctionInfo(changed, false)});
        emit sendTo(appellant->id,
                    TextMessage{TextTarget::User, 0, appellant->id,
                                QStringLiteral("Your appeal was %1%2")
                                    .arg(moderation::toString(decision),
                                         request.reason.isEmpty() ? QStringLiteral(".")
                                                                  : QStringLiteral(": %1").arg(request.reason)),
                                now});
        refreshUser(*appellant);
    }
}

// -------------------------------------------------------------------- audit

void ServerCore::readAudit(Client &actor, const ReadAudit &request)
{
    if (!can(actor, Capability::AuditRead)) {
        refuseUnauthorized(actor, request.requestId, Capability::AuditRead);
        return;
    }
    AuditPage page;
    page.requestId = request.requestId;
    page.chainIntact = m_auditStatus.intact;
    page.firstBadSequence = m_auditStatus.firstBadSequence.value_or(0);
    QList<moderation::AuditEntry> entries;
    if (m_database != nullptr) {
        entries = m_database->auditEntries(request.beforeSequence, request.limit);
    } else {
        for (auto it = m_memoryAudit.crbegin(); it != m_memoryAudit.crend() && entries.size() < request.limit; ++it) {
            if (request.beforeSequence == 0 || it->sequence < request.beforeSequence) {
                entries.append(*it);
            }
        }
    }
    for (const moderation::AuditEntry &e : entries) {
        page.entries.append(
            AuditEntryInfo{e.sequence, e.timeMs,
                           e.actorName.isEmpty() ? QStringLiteral("server") : e.actorName.left(kMaxDisplayNameLength),
                           e.action, e.target, e.channelId, e.reason, e.details, e.hash});
    }
    emit sendTo(actor.id, page);
}

// ----------------------------------------------------------------- accounts

void ServerCore::createAccount(Client &actor, const CreateAccount &request)
{
    if (!can(actor, Capability::AccountManage)) {
        refuseUnauthorized(actor, request.requestId, Capability::AccountManage);
        return;
    }
    if (m_database == nullptr) {
        refuse(actor.id, request.requestId, ErrorCode::InvalidRequest, QStringLiteral("this server has no database"));
        return;
    }
    if (!ServerDatabase::isValidUsername(request.name)) {
        refuse(actor.id, request.requestId, ErrorCode::InvalidRequest,
               QStringLiteral("usernames are 1 to 32 characters: letters A-Z, digits, '.', '_' or '-'"));
        return;
    }
    if (request.password.size() < m_config.minPasswordLength) {
        refuse(actor.id, request.requestId, ErrorCode::InvalidRequest,
               QStringLiteral("passwords need at least %1 characters").arg(m_config.minPasswordLength));
        return;
    }
    const auto role = m_permissions.role(request.roleId);
    if (!role || request.roleId == moderation::roles::kGuest || request.roleId == moderation::roles::kOwner ||
        !m_permissions.outranks(static_cast<moderation::RoleId>(actor.user.role), request.roleId)) {
        refuse(actor.id, request.requestId, ErrorCode::NotAuthorized,
               QStringLiteral("you can only create accounts with roles below your own"));
        return;
    }
    if (m_database->findAccount(request.name)) {
        refuse(actor.id, request.requestId, ErrorCode::NameInUse, QStringLiteral("that account already exists"));
        return;
    }
    const quint32 actorId = actor.id;
    m_passwords.hash(request.password, this, [this, actorId, request](std::optional<QByteArray> hash) {
        Client *a = onlineClient(actorId);
        if (a == nullptr) {
            return;
        }
        QString error;
        if (!hash || !m_database->createAccount(request.name, *hash, static_cast<Role>(request.roleId), &error) ||
            (request.bot && !m_database->setBot(request.name, true))) {
            refuse(actorId, request.requestId, ErrorCode::InvalidRequest,
                   error.isEmpty() ? QStringLiteral("could not create the account") : error);
            return;
        }
        audit(a, QStringLiteral("account.created"), request.name, 0, QString(),
              QStringLiteral("role=%1").arg(request.roleId) + (request.bot ? QStringLiteral("; bot") : QString()));
        reply(actorId, request.requestId);
    });
}

void ServerCore::deleteAccount(Client &actor, const DeleteAccount &request)
{
    if (!can(actor, Capability::AccountManage)) {
        refuseUnauthorized(actor, request.requestId, Capability::AccountManage);
        return;
    }
    const auto account = m_database != nullptr ? m_database->findAccount(request.name) : std::nullopt;
    if (!account) {
        refuse(actor.id, request.requestId, ErrorCode::NotFound, QStringLiteral("no such account"));
        return;
    }
    if (account->id == actor.accountId || !m_permissions.outranks(static_cast<moderation::RoleId>(actor.user.role),
                                                                  static_cast<moderation::RoleId>(account->role))) {
        refuse(actor.id, request.requestId, ErrorCode::NotAuthorized,
               QStringLiteral("you can only delete accounts whose role ranks below yours"));
        return;
    }
    if (!m_database->deleteAccount(account->username)) {
        refuse(actor.id, request.requestId, ErrorCode::InternalError, QStringLiteral("could not delete the account"));
        return;
    }
    audit(&actor, QStringLiteral("account.deleted"), account->username);
    reply(actor.id, request.requestId);
    if (Client *online = onlineAccount(account->id)) {
        emit closeSession(online->id, ErrorCode::Kicked, QStringLiteral("your account was deleted"));
    }
}

void ServerCore::setAccountPassword(Client &actor, const SetAccountPassword &request)
{
    const auto account = m_database != nullptr ? m_database->findAccount(request.name) : std::nullopt;
    if (!account) {
        refuse(actor.id, request.requestId, ErrorCode::NotFound, QStringLiteral("no such account"));
        return;
    }
    const bool self = account->id == actor.accountId;
    if (!self) {
        if (!can(actor, Capability::AccountManage)) {
            refuseUnauthorized(actor, request.requestId, Capability::AccountManage);
            return;
        }
        if (!m_permissions.outranks(static_cast<moderation::RoleId>(actor.user.role),
                                    static_cast<moderation::RoleId>(account->role))) {
            refuse(actor.id, request.requestId, ErrorCode::NotAuthorized,
                   QStringLiteral("you can only reset passwords of accounts below your role"));
            return;
        }
    }
    if (request.password.size() < m_config.minPasswordLength) {
        refuse(actor.id, request.requestId, ErrorCode::InvalidRequest,
               QStringLiteral("passwords need at least %1 characters").arg(m_config.minPasswordLength));
        return;
    }
    const quint32 actorId = actor.id;
    const QString username = account->username;
    m_passwords.hash(request.password, this, [this, actorId, request, username, self](std::optional<QByteArray> hash) {
        Client *a = onlineClient(actorId);
        if (a == nullptr) {
            return;
        }
        if (!hash || !m_database->setPasswordHash(username, *hash)) {
            refuse(actorId, request.requestId, ErrorCode::InternalError,
                   QStringLiteral("could not change the password"));
            return;
        }
        audit(a, self ? QStringLiteral("account.password-changed") : QStringLiteral("account.password-reset"),
              username);
        reply(actorId, request.requestId);
    });
}

void ServerCore::transferOwnership(Client &actor, const TransferOwnership &request)
{
    if (!can(actor, Capability::OwnerTransfer)) {
        refuseUnauthorized(actor, request.requestId, Capability::OwnerTransfer);
        return;
    }
    const auto account = m_database != nullptr ? m_database->findAccount(request.account) : std::nullopt;
    if (!account || account->id == actor.accountId) {
        refuse(actor.id, request.requestId, ErrorCode::NotFound, QStringLiteral("no such other account"));
        return;
    }
    QString error;
    if (!m_database->setOwner(account->username, &error)) {
        refuse(actor.id, request.requestId, ErrorCode::InternalError, error);
        return;
    }
    audit(&actor, QStringLiteral("owner.transferred"), account->username);
    reply(actor.id, request.requestId);
    actor.user.role = Role::Admin;
    broadcast(UserUpdated{actor.id, static_cast<quint32>(Role::Admin), actor.user.flags});
    refreshUser(actor);
    if (Client *newOwner = onlineAccount(account->id)) {
        newOwner->user.role = Role::Owner;
        broadcast(UserUpdated{newOwner->id, static_cast<quint32>(Role::Owner), newOwner->user.flags});
        refreshUser(*newOwner);
    }
}

// ----------------------------------------------------------------- settings

void ServerCore::serverSettings(Client &actor, const ServerSettingsRequest &request)
{
    if (!can(actor, Capability::ServerConfigure)) {
        refuseUnauthorized(actor, request.requestId, Capability::ServerConfigure);
        return;
    }
    if (request.settings) {
        const ServerSettingsInfo previous = m_settings;
        applySettings(*request.settings, true);
        QStringList changes;
        if (previous.name != m_settings.name) {
            changes << QStringLiteral("name");
        }
        if (previous.welcomeMessage != m_settings.welcomeMessage) {
            changes << QStringLiteral("welcomeMessage");
        }
        if (previous.allowGuests != m_settings.allowGuests) {
            changes << QStringLiteral("allowGuests=%1").arg(m_settings.allowGuests ? 1 : 0);
        }
        if (previous.maxUsers != m_settings.maxUsers) {
            changes << QStringLiteral("maxUsers=%1").arg(m_settings.maxUsers);
        }
        if (previous.maxVoiceBitrate != m_settings.maxVoiceBitrate) {
            changes << QStringLiteral("maxVoiceBitrate=%1").arg(m_settings.maxVoiceBitrate);
        }
        if (previous.maxTemporaryBanDays != m_settings.maxTemporaryBanDays) {
            changes << QStringLiteral("maxTemporaryBanDays=%1").arg(m_settings.maxTemporaryBanDays);
        }
        if (previous.contact != m_settings.contact) {
            changes << QStringLiteral("contact");
        }
        audit(&actor, QStringLiteral("server.settings-changed"), QString(), 0, QString(),
              changes.join(QStringLiteral("; ")));
    }
    emit sendTo(actor.id, ServerSettingsResult{request.requestId, m_settings});
}

void ServerCore::serverStats(Client &actor, const GetServerStats &request)
{
    if (!can(actor, Capability::ServerMonitor)) {
        refuseUnauthorized(actor, request.requestId, Capability::ServerMonitor);
        return;
    }
    ServerStatsInfo stats;
    stats.uptimeSeconds = (m_now() - m_startedMs) / 1000;
    stats.onlineUsers = static_cast<quint32>(onlineUserCount());
    stats.connections = static_cast<quint32>(m_clients.size());
    stats.channels = static_cast<quint32>(m_channels.size());
    const qint64 now = m_wallClock();
    stats.activeSanctions =
        static_cast<quint32>(std::count_if(m_sanctions.sanctions().cbegin(), m_sanctions.sanctions().cend(),
                                           [now](const Sanction &s) { return s.isActiveAt(now); }));
    stats.openReports = static_cast<quint32>(std::count_if(m_reports.cbegin(), m_reports.cend(), [](const auto &r) {
        return r.state == moderation::Report::State::Open;
    }));
    stats.openAppeals = static_cast<quint32>(std::count_if(m_appeals.cbegin(), m_appeals.cend(), [](const auto &a) {
        return a.state == moderation::Appeal::State::Open;
    }));
    stats.auditEntries = m_database != nullptr ? m_database->auditCount() : static_cast<quint64>(m_memoryAudit.size());
    stats.auditIntact = m_auditStatus.intact;
    if (m_statsProvider) {
        m_statsProvider(stats);
    }
    emit sendTo(actor.id, ServerStatsResult{request.requestId, stats});
}

} // namespace opentalk::server
