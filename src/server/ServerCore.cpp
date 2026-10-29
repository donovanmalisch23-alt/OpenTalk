// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/server/ServerCore.h"

#include "opentalk/logging/Logging.h"
#include "opentalk/server/Backups.h"
#include "opentalk/server/PasswordWorker.h"
#include "opentalk/server/ServerDatabase.h"

#include <QDateTime>
#include <QDir>
#include <QRandomGenerator>
#include <QTimeZone>

#include <algorithm>

namespace opentalk::server {

using namespace protocol;
using moderation::Capability;

ServerCore::ServerCore(Config config, ServerDatabase *database, PasswordWorker &passwords,
                       std::function<qint64()> nowMs, std::function<qint64()> wallClockMs, QObject *parent)
    : QObject(parent)
    , m_config(std::move(config))
    , m_database(database)
    , m_passwords(passwords)
    , m_now(std::move(nowMs))
    , m_wallClock(std::move(wallClockMs))
{
    ChannelInfo root;
    root.id = kRootChannelId;
    root.persistent = true;
    m_channels.insert(kRootChannelId, Channel{root, {}, 0, {}});

    const QString randomPassword = QString::number(QRandomGenerator::system()->generate64(), 36);
    m_dummyHash = security::hashPassword(randomPassword, passwords.cost()).value_or(QByteArray());

    m_settings.name = m_config.serverName;
    m_settings.allowGuests = m_config.allowGuests;
    m_settings.maxUsers = static_cast<quint16>(std::clamp(m_config.maxUsers, 1, 65535));
    m_settings.maxVoiceBitrate = m_config.maxVoiceBitrate;
    m_permissions.setRoles(moderation::builtInRoles());
}

ServerCore::~ServerCore() = default;

bool ServerCore::load(QString *errorMessage)
{
    m_startedMs = m_now();
    if (m_database == nullptr) {
        applySettings(m_settings, false);
        audit(nullptr, QStringLiteral("server.started"), QString(), 0, QString(), QStringLiteral("database=none"));
        return true;
    }
    for (const StoredChannel &stored : m_database->channels()) {
        if (stored.id <= kRootChannelId ||
            !m_channels.contains(stored.parentId == 0 ? kRootChannelId : stored.parentId)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("stored channel %1 has an invalid parent").arg(stored.id);
            }
            return false;
        }
        ChannelInfo info;
        info.id = stored.id;
        info.parentId = stored.parentId == 0 ? kRootChannelId : stored.parentId;
        info.name = stored.name;
        info.topic = stored.topic;
        info.persistent = true;
        info.hasPassword = !stored.passwordHash.isEmpty();
        info.maxUsers = stored.maxUsers;
        m_channels.insert(info.id, Channel{info, stored.passwordHash, 0, {}});
    }
    qCInfo(lcServer) << "Loaded" << (m_channels.size() - 1) << "persistent channel(s)";

    reloadPermissions();
    m_sanctions.setSanctions(m_database->sanctions());
    m_reports = m_database->reports();
    m_appeals = m_database->appeals();

    // Saved settings win over the start-up configuration once someone changed them.
    ServerSettingsInfo saved = m_settings;
    saved.name = m_database->setting(QStringLiteral("server.name"), saved.name);
    saved.welcomeMessage = m_database->setting(QStringLiteral("server.welcomeMessage"));
    saved.contact = m_database->setting(QStringLiteral("server.contact"));
    const QString guests = m_database->setting(QStringLiteral("server.allowGuests"));
    if (!guests.isEmpty()) {
        saved.allowGuests = guests == QLatin1String("1");
    }
    saved.maxUsers = static_cast<quint16>(
        m_database->setting(QStringLiteral("server.maxUsers"), QString::number(saved.maxUsers)).toUInt());
    saved.maxVoiceBitrate =
        m_database->setting(QStringLiteral("server.maxVoiceBitrate"), QString::number(saved.maxVoiceBitrate)).toUInt();
    saved.maxTemporaryBanDays = static_cast<quint16>(
        m_database->setting(QStringLiteral("server.maxTemporaryBanDays"), QString::number(saved.maxTemporaryBanDays))
            .toUInt());
    if (saved.maxUsers == 0) {
        saved.maxUsers = 200;
    }
    applySettings(saved, false);

    pruneAuditLog();
    m_auditStatus = m_database->verifyAudit();
    if (!m_auditStatus.intact) {
        qCWarning(lcServer).noquote() << "The audit log has been tampered with or damaged:" << m_auditStatus.problem;
    }
    if (!m_database->owner()) {
        qCWarning(lcServer) << "This server has no owner. Create one with --create-account <name> --role owner, or "
                               "--recover-owner <name>.";
    }
    audit(nullptr, QStringLiteral("server.started"), QString(), 0, QString(),
          QStringLiteral("auditIntact=%1; checked=%2")
              .arg(m_auditStatus.intact ? QStringLiteral("yes") : QStringLiteral("no"))
              .arg(m_auditStatus.checked));
    return true;
}

int ServerCore::onlineUserCount() const
{
    return static_cast<int>(std::count_if(m_clients.cbegin(), m_clients.cend(),
                                          [](const Client &c) { return c.state == ClientState::Online; }));
}

QList<ChannelInfo> ServerCore::channels() const
{
    QList<ChannelInfo> result;
    for (const Channel &c : m_channels) {
        result.append(c.info);
    }
    return result;
}

// ------------------------------------------------------------------------ sessions

void ServerCore::sessionStarted(quint32 sessionId, const QHostAddress &address)
{
    Client client;
    client.id = sessionId;
    client.address = address;
    client.textTokens = m_config.textBurst;
    client.textRefillMs = m_now();
    m_clients.insert(sessionId, client);
}

void ServerCore::sessionEnded(quint32 sessionId)
{
    const auto it = m_clients.find(sessionId);
    if (it == m_clients.end()) {
        return;
    }
    const bool wasOnline = it->state == ClientState::Online;
    const quint32 channelId = it->user.channelId;
    m_clients.erase(it);
    if (wasOnline) {
        if (auto channel = m_channels.find(channelId); channel != m_channels.end()) {
            channel->members.remove(sessionId);
        }
        broadcast(UserLeft{sessionId});
        removeIfEmptyTemporary(channelId);
    }
}

void ServerCore::handleMessage(quint32 sessionId, const Message &message)
{
    const auto it = m_clients.find(sessionId);
    if (it == m_clients.end()) {
        return;
    }
    Client &client = *it;
    switch (client.state) {
    case ClientState::AwaitingAuth:
        if (const auto *auth = std::get_if<Authenticate>(&message)) {
            authenticate(client, *auth);
        } else {
            emit closeSession(sessionId, ErrorCode::ProtocolViolation, QStringLiteral("log in first"));
        }
        return;
    case ClientState::Authenticating:
        emit closeSession(sessionId, ErrorCode::ProtocolViolation, QStringLiteral("login is still in progress"));
        return;
    case ClientState::Online:
        break;
    }

    if (const auto *join = std::get_if<JoinChannel>(&message)) {
        joinChannel(client, *join);
    } else if (const auto *create = std::get_if<CreateChannel>(&message)) {
        createChannel(client, *create);
    } else if (const auto *remove = std::get_if<DeleteChannel>(&message)) {
        deleteChannel(client, *remove);
    } else if (const auto *edit = std::get_if<EditChannel>(&message)) {
        editChannel(client, *edit);
    } else if (const auto *text = std::get_if<SendText>(&message)) {
        sendText(client, *text);
    } else if (const auto *status = std::get_if<SetStatus>(&message)) {
        setStatus(client, *status);
    } else if (std::holds_alternative<Error>(message)) {
        // Informational; nothing to do.
    } else if (!handleAdminMessage(client, message)) {
        emit closeSession(sessionId, ErrorCode::ProtocolViolation, QStringLiteral("unexpected message"));
    }
}

void ServerCore::pruneAuditLog()
{
    m_lastAuditPruneMs = m_wallClock();
    if (m_database == nullptr || m_config.auditRetentionDays <= 0) {
        return;
    }
    const qint64 cutoff = m_lastAuditPruneMs - static_cast<qint64>(m_config.auditRetentionDays) * 24 * 3600 * 1000;
    if (const int pruned = m_database->pruneAudit(cutoff); pruned > 0) {
        qCInfo(lcServer) << "Removed" << pruned << "audit entries older than" << m_config.auditRetentionDays << "days";
    }
}

void ServerCore::backupIfDue()
{
    if (m_database == nullptr || m_config.backupFolder.isEmpty() || m_config.backupIntervalHours <= 0) {
        return;
    }
    const qint64 now = m_wallClock();
    if (m_lastBackupMs < 0) {
        const QList<BackupFile> existing = listBackups(m_config.backupFolder);
        m_lastBackupMs = existing.isEmpty() ? 0 : existing.first().time.toMSecsSinceEpoch();
    }
    if (now - m_lastBackupMs < static_cast<qint64>(m_config.backupIntervalHours) * 3600 * 1000) {
        return;
    }
    m_lastBackupMs = now; // also after a failure, so a broken disk is not retried every second
    QString error;
    const auto path =
        writeBackup(*m_database, m_config.backupFolder, QDateTime::fromMSecsSinceEpoch(now, QTimeZone::utc()), &error);
    if (!path) {
        qCWarning(lcServer).noquote() << "Automatic backup failed:" << error;
        return;
    }
    const int removed = pruneBackups(m_config.backupFolder, m_config.backupsToKeep);
    qCInfo(lcServer).noquote() << "Backed up the database to" << QDir::toNativeSeparators(*path)
                               << (removed > 0 ? QStringLiteral("(removed %1 older backups)").arg(removed) : QString());
}

void ServerCore::tick()
{
    constexpr qint64 kDayMs = 24LL * 3600 * 1000;
    if (m_wallClock() - m_lastAuditPruneMs >= kDayMs) {
        pruneAuditLog();
    }
    backupIfDue();
    const QList<moderation::Sanction> expired = m_sanctions.expire(m_wallClock());
    for (const moderation::Sanction &sanction : expired) {
        updateSanction(sanction);
        audit(nullptr, QStringLiteral("sanction.expired"), sanction.targetName, sanction.channelId, QString(),
              QStringLiteral("sanction=%1; type=%2").arg(sanction.id).arg(moderation::toString(sanction.type)));
        sendSanctionNotice(sanction);
        for (Client *client : clientsMatching(sanction)) {
            refreshUser(*client);
        }
    }
}

// --------------------------------------------------------------------------- login

bool ServerCore::tooManyFailedLogins(const QHostAddress &address)
{
    auto it = m_failedLogins.find(address);
    if (it == m_failedLogins.end()) {
        return false;
    }
    const qint64 cutoff = m_now() - m_config.failedLoginWindowMs;
    it->erase(std::remove_if(it->begin(), it->end(), [cutoff](qint64 t) { return t < cutoff; }), it->end());
    if (it->isEmpty()) {
        m_failedLogins.erase(it);
        return false;
    }
    return it->size() >= m_config.maxFailedLoginsPerAddress;
}

void ServerCore::loginFailed(quint32 sessionId, const QHostAddress &address, const QString &detail)
{
    m_failedLogins[address].append(m_now());
    ++m_failedLoginCount;
    qCInfo(lcServer).noquote() << "Login failed for session" << sessionId << "-" << detail;
    // The client is told only that login failed, never whether the account exists.
    emit closeSession(sessionId, ErrorCode::AuthenticationFailed, QStringLiteral("wrong username or password"));
}

bool ServerCore::nameTakenOnline(const QString &name) const
{
    return std::any_of(m_clients.cbegin(), m_clients.cend(), [&name](const Client &c) {
        return c.state == ClientState::Online && c.user.name.compare(name, Qt::CaseInsensitive) == 0;
    });
}

QString ServerCore::describeBan(const moderation::Sanction &ban) const
{
    QString text = ban.expiresMs == 0 ? QStringLiteral("you are banned from this server")
                                      : QStringLiteral("you are banned from this server until %1 UTC")
                                            .arg(QDateTime::fromMSecsSinceEpoch(ban.expiresMs)
                                                     .toUTC()
                                                     .toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    if (!ban.reason.isEmpty()) {
        text += QStringLiteral(". Reason: %1").arg(ban.reason);
    }
    if (!m_settings.contact.isEmpty()) {
        text += QStringLiteral(". Contact: %1").arg(m_settings.contact);
    }
    return text.left(kMaxDetailLength);
}

void ServerCore::authenticate(Client &client, const Authenticate &request)
{
    const quint32 sessionId = client.id;
    if (tooManyFailedLogins(client.address)) {
        emit closeSession(sessionId, ErrorCode::AuthenticationFailed,
                          QStringLiteral("too many failed logins from your address; try again later"));
        return;
    }
    if (onlineUserCount() >= m_settings.maxUsers) {
        emit closeSession(sessionId, ErrorCode::ServerFull, QStringLiteral("the server is full"));
        return;
    }

    if (request.mode == Authenticate::Mode::Guest) {
        if (!m_settings.allowGuests) {
            emit closeSession(sessionId, ErrorCode::AuthenticationFailed,
                              QStringLiteral("this server does not allow guests"));
            return;
        }
        if (const auto ban = m_sanctions.activeBan({0, client.address.toString()}, {}, m_wallClock())) {
            emit closeSession(sessionId, ErrorCode::Banned, describeBan(*ban));
            return;
        }
        const bool reserved = m_database != nullptr && m_database->findAccount(request.name).has_value();
        if (reserved || nameTakenOnline(request.name)) {
            emit closeSession(sessionId, ErrorCode::NameInUse, QStringLiteral("that name is already in use"));
            return;
        }
        goOnline(client, request.name, Role::Guest, 0);
        return;
    }

    const std::optional<Account> account = m_database != nullptr ? m_database->findAccount(request.name) : std::nullopt;
    const bool usable = account.has_value() && !account->disabled;
    const QByteArray hash = usable ? account->passwordHash : m_dummyHash;
    client.state = ClientState::Authenticating;
    const QHostAddress address = client.address;
    m_passwords.verify(hash, request.password, this, [this, sessionId, address, usable, account](bool matches) {
        auto it = m_clients.find(sessionId);
        if (it == m_clients.end() || it->state != ClientState::Authenticating) {
            return; // disconnected meanwhile
        }
        if (!usable || !matches) {
            loginFailed(sessionId, address,
                        usable ? QStringLiteral("wrong password") : QStringLiteral("unknown or disabled account"));
            return;
        }
        // Checked only after the password, so a ban does not reveal that an account exists.
        if (const auto ban = m_sanctions.activeBan({account->id, address.toString()}, {}, m_wallClock())) {
            emit closeSession(sessionId, ErrorCode::Banned, describeBan(*ban));
            return;
        }
        if (Client *existing = onlineAccount(account->id)) {
            // The same account logging in again replaces the old session (for example
            // after a network change).
            emit closeSession(existing->id, ErrorCode::NameInUse, QStringLiteral("logged in from somewhere else"));
            sessionEnded(existing->id);
        }
        goOnline(*it, account->username, account->role, account->id, account->bot);
    });
}

void ServerCore::goOnline(Client &client, const QString &name, Role role, qint64 accountId, bool bot)
{
    client.state = ClientState::Online;
    client.accountId = accountId;
    client.bot = bot;
    client.user = UserInfo{client.id, name, role, kRootChannelId, bot ? quint32{BotFlag} : 0u, QString()};
    client.effectiveRole = static_cast<moderation::RoleId>(role);
    m_channels[kRootChannelId].members.insert(client.id);
    qCInfo(lcServer).noquote() << "Session" << client.id << "logged in as" << name << "(" << toString(role) << ")";

    // Restrictions and mutes that already apply.
    const moderation::SanctionSubject subject = subjectOf(client);
    if (m_sanctions.isRestricted(subject, m_wallClock())) {
        client.effectiveRole = moderation::roles::kRestricted;
        client.user.flags |= RestrictedFlag;
    }
    if (m_sanctions.isVoiceMuted(subject, {}, m_wallClock())) {
        client.user.flags |= VoiceMutedFlag;
    }
    if (m_sanctions.isTextMuted(subject, {}, m_wallClock())) {
        client.user.flags |= TextMutedFlag;
    }
    for (Capability c : m_permissions.serverCapabilities(actorOf(client))) {
        client.capabilities.append(static_cast<quint16>(c));
    }
    for (Capability c : m_permissions.channelOnlyCapabilities(actorOf(client))) {
        client.channelCapabilities.append(static_cast<quint16>(c));
    }

    emit sendTo(client.id, AuthResult{client.id, name, role, client.capabilities, client.channelCapabilities});
    StateSnapshot snapshot;
    for (const Channel &c : std::as_const(m_channels)) {
        snapshot.channels.append(c.info); // ordered by ID, so parents come before children
    }
    for (const Client &c : std::as_const(m_clients)) {
        if (c.state == ClientState::Online) {
            snapshot.users.append(c.user);
        }
    }
    snapshot.roles = roleInfos();
    emit sendTo(client.id, snapshot);
    broadcast(UserJoined{client.user}, client.id);
    if (!m_settings.welcomeMessage.isEmpty()) {
        emit sendTo(client.id, TextMessage{TextTarget::Server, 0, 0, m_settings.welcomeMessage, m_wallClock()});
    }
    for (const moderation::Sanction &s : m_sanctions.activeFor(subject, m_wallClock())) {
        emit sendTo(client.id, SanctionNotice{sanctionInfo(s, false)});
    }
    emit userOnline(client.id);
}

QList<quint32> ServerCore::voiceRecipients(quint32 sessionId, AudioStream stream) const
{
    const Client *client = onlineClient(sessionId);
    if (client == nullptr) {
        return {};
    }
    const quint32 channelId = client->user.channelId;
    const Capability needed = stream == AudioStream::Voice ? Capability::VoiceTransmit : Capability::MediaTransmit;
    if (!can(*client, needed, channelId) ||
        m_sanctions.isVoiceMuted(subjectOf(*client), chainOf(channelId), m_wallClock())) {
        return {};
    }
    const auto channel = m_channels.constFind(channelId);
    if (channel == m_channels.constEnd()) {
        return {};
    }
    QList<quint32> recipients;
    recipients.reserve(channel->members.size());
    for (quint32 member : channel->members) {
        if (member != sessionId) {
            recipients.append(member);
        }
    }
    return recipients;
}

// ------------------------------------------------------------------------ channels

ServerCore::Client *ServerCore::onlineClient(quint32 id)
{
    auto it = m_clients.find(id);
    return (it != m_clients.end() && it->state == ClientState::Online) ? &*it : nullptr;
}

const ServerCore::Client *ServerCore::onlineClient(quint32 id) const
{
    auto it = m_clients.constFind(id);
    return (it != m_clients.cend() && it->state == ClientState::Online) ? &*it : nullptr;
}

ServerCore::Client *ServerCore::onlineAccount(qint64 accountId)
{
    if (accountId == 0) {
        return nullptr;
    }
    for (Client &c : m_clients) {
        if (c.state == ClientState::Online && c.accountId == accountId) {
            return &c;
        }
    }
    return nullptr;
}

void ServerCore::joinChannel(Client &client, const JoinChannel &request)
{
    const auto target = m_channels.constFind(request.channelId);
    if (target == m_channels.cend()) {
        refuse(client.id, request.requestId, ErrorCode::NotFound, QStringLiteral("no such channel"));
        return;
    }
    if (client.user.channelId == request.channelId) {
        reply(client.id, request.requestId);
        return;
    }
    if (!can(client, Capability::ChannelJoin, request.channelId)) {
        refuseUnauthorized(client, request.requestId, Capability::ChannelJoin, request.channelId);
        return;
    }
    if (const auto ban = m_sanctions.activeBan(subjectOf(client), chainOf(request.channelId), m_wallClock())) {
        refuse(client.id, request.requestId, ErrorCode::Banned,
               ban->reason.isEmpty() ? QStringLiteral("you are banned from this channel")
                                     : QStringLiteral("you are banned from this channel: %1").arg(ban->reason));
        return;
    }
    const bool bypass = can(client, Capability::ChannelBypassRestrictions, request.channelId);
    if (!bypass && target->info.maxUsers > 0 && target->members.size() >= target->info.maxUsers) {
        refuse(client.id, request.requestId, ErrorCode::ChannelFull, QStringLiteral("the channel is full"));
        return;
    }
    if (bypass || target->passwordHash.isEmpty()) {
        moveUser(client, request.channelId);
        reply(client.id, request.requestId);
        return;
    }
    if (request.password.isEmpty()) {
        refuse(client.id, request.requestId, ErrorCode::WrongChannelPassword,
               QStringLiteral("this channel needs a password"));
        return;
    }
    const quint32 clientId = client.id;
    m_passwords.verify(target->passwordHash, request.password, this, [this, clientId, request](bool matches) {
        Client *c = onlineClient(clientId);
        if (c == nullptr) {
            return;
        }
        const auto channel = m_channels.constFind(request.channelId);
        if (channel == m_channels.cend()) {
            refuse(clientId, request.requestId, ErrorCode::NotFound, QStringLiteral("no such channel"));
        } else if (!matches) {
            refuse(clientId, request.requestId, ErrorCode::WrongChannelPassword, QStringLiteral("wrong password"));
        } else if (channel->info.maxUsers > 0 && channel->members.size() >= channel->info.maxUsers) {
            refuse(clientId, request.requestId, ErrorCode::ChannelFull, QStringLiteral("the channel is full"));
        } else {
            moveUser(*c, request.channelId);
            reply(clientId, request.requestId);
        }
    });
}

void ServerCore::createChannel(Client &client, const CreateChannel &request)
{
    const Capability needed =
        request.persistent ? Capability::ChannelCreatePersistent : Capability::ChannelCreateTemporary;
    if (!can(client, needed, request.parentId)) {
        refuseUnauthorized(client, request.requestId, needed, request.parentId);
        return;
    }
    if (request.persistent && m_database == nullptr) {
        refuse(client.id, request.requestId, ErrorCode::InvalidRequest,
               QStringLiteral("this server has no database for persistent channels"));
        return;
    }
    if (request.maxUsers > m_config.maxUsersPerChannelLimit) {
        refuse(client.id, request.requestId, ErrorCode::InvalidRequest, QStringLiteral("user limit is too high"));
        return;
    }
    if (m_channels.size() >= m_config.maxChannels) {
        refuse(client.id, request.requestId, ErrorCode::InvalidRequest,
               QStringLiteral("the server has too many channels"));
        return;
    }
    if (!request.persistent) {
        const auto owned = std::count_if(m_channels.cbegin(), m_channels.cend(), [&client](const Channel &c) {
            return !c.info.persistent && c.ownerId == client.id;
        });
        if (owned >= m_config.maxTemporaryChannelsPerUser) {
            refuse(client.id, request.requestId, ErrorCode::InvalidRequest,
                   QStringLiteral("you have created too many temporary channels"));
            return;
        }
    }
    if (request.password.isEmpty()) {
        finishCreateChannel(client.id, request, {});
        return;
    }
    const quint32 clientId = client.id;
    m_passwords.hash(request.password, this, [this, clientId, request](std::optional<QByteArray> hash) {
        if (onlineClient(clientId) == nullptr) {
            return;
        }
        if (!hash) {
            refuse(clientId, request.requestId, ErrorCode::InternalError,
                   QStringLiteral("could not store the password"));
            return;
        }
        finishCreateChannel(clientId, request, *hash);
    });
}

void ServerCore::finishCreateChannel(quint32 clientId, const CreateChannel &request, const QByteArray &passwordHash)
{
    Client *client = onlineClient(clientId);
    if (client == nullptr) {
        return;
    }
    // Checked here (not before hashing) because the tree may have changed meanwhile.
    const auto parent = m_channels.constFind(request.parentId);
    if (parent == m_channels.cend()) {
        refuse(clientId, request.requestId, ErrorCode::NotFound, QStringLiteral("the parent channel does not exist"));
        return;
    }
    if (!parent->info.persistent) {
        refuse(clientId, request.requestId, ErrorCode::InvalidRequest,
               QStringLiteral("temporary channels cannot have sub-channels"));
        return;
    }
    const bool nameTaken = std::any_of(m_channels.cbegin(), m_channels.cend(), [&request](const Channel &c) {
        return c.info.parentId == request.parentId && c.info.name.compare(request.name, Qt::CaseInsensitive) == 0;
    });
    if (nameTaken) {
        refuse(clientId, request.requestId, ErrorCode::NameInUse,
               QStringLiteral("a channel with that name already exists here"));
        return;
    }

    ChannelInfo info;
    info.parentId = request.parentId;
    info.name = request.name;
    info.topic = request.topic;
    info.persistent = request.persistent;
    info.hasPassword = !passwordHash.isEmpty();
    info.maxUsers = request.maxUsers;
    if (request.persistent) {
        StoredChannel stored{0,
                             request.parentId == kRootChannelId ? 0 : request.parentId,
                             info.name,
                             info.topic,
                             passwordHash,
                             info.maxUsers};
        QString error;
        if (!m_database->insertChannel(stored, &error)) {
            qCWarning(lcServer).noquote() << "Could not store a channel:" << error;
            refuse(clientId, request.requestId, ErrorCode::InternalError, QStringLiteral("could not save the channel"));
            return;
        }
        info.id = stored.id;
        audit(client, QStringLiteral("channel.created"), info.name, info.id);
    } else {
        info.id = m_nextTemporaryChannelId++;
    }
    m_channels.insert(info.id, Channel{info, passwordHash, request.persistent ? 0 : clientId, {}});
    qCInfo(lcServer) << "Session" << clientId << "created" << (info.persistent ? "persistent" : "temporary")
                     << "channel" << info.id;
    broadcast(ChannelAdded{info});
    if (!info.persistent) {
        moveUser(*client, info.id); // otherwise an unused temporary channel would linger
    }
    reply(clientId, request.requestId, info.id);
}

void ServerCore::deleteChannel(Client &client, const DeleteChannel &request)
{
    if (request.channelId == kRootChannelId) {
        refuse(client.id, request.requestId, ErrorCode::InvalidRequest, QStringLiteral("the root cannot be deleted"));
        return;
    }
    const auto it = m_channels.find(request.channelId);
    if (it == m_channels.end()) {
        refuse(client.id, request.requestId, ErrorCode::NotFound, QStringLiteral("no such channel"));
        return;
    }
    if (!can(client, Capability::ChannelDelete, request.channelId)) {
        refuseUnauthorized(client, request.requestId, Capability::ChannelDelete, request.channelId);
        return;
    }
    const bool hasChildren = std::any_of(m_channels.cbegin(), m_channels.cend(),
                                         [&request](const Channel &c) { return c.info.parentId == request.channelId; });
    if (hasChildren) {
        refuse(client.id, request.requestId, ErrorCode::InvalidRequest,
               QStringLiteral("delete its sub-channels first"));
        return;
    }
    const QString name = it->info.name;
    if (it->info.persistent) {
        if (!m_database->deleteChannel(request.channelId)) {
            refuse(client.id, request.requestId, ErrorCode::InternalError,
                   QStringLiteral("could not delete the channel"));
            return;
        }
        m_database->deleteRulesForChannel(request.channelId);
        reloadPermissions();
    }
    // Everyone inside goes to the root first, so no one is ever in a missing channel.
    const QSet<quint32> members = it->members;
    for (quint32 memberId : members) {
        if (Client *member = onlineClient(memberId)) {
            moveUser(*member, kRootChannelId);
        }
    }
    m_channels.remove(request.channelId);
    qCInfo(lcServer) << "Session" << client.id << "deleted channel" << request.channelId;
    audit(&client, QStringLiteral("channel.deleted"), name, request.channelId);
    broadcast(ChannelRemoved{request.channelId});
    reply(client.id, request.requestId);
}

void ServerCore::editChannel(Client &client, const EditChannel &request)
{
    const auto it = m_channels.constFind(request.channelId);
    if (it == m_channels.cend() || request.channelId == kRootChannelId) {
        refuse(client.id, request.requestId,
               request.channelId == kRootChannelId ? ErrorCode::InvalidRequest : ErrorCode::NotFound,
               request.channelId == kRootChannelId ? QStringLiteral("the root is renamed in the server settings")
                                                   : QStringLiteral("no such channel"));
        return;
    }
    // People may edit temporary channels they created; everything else needs channel.edit.
    const bool ownTemporary = !it->info.persistent && it->ownerId == client.id;
    if (!ownTemporary && !can(client, Capability::ChannelEdit, request.channelId)) {
        refuseUnauthorized(client, request.requestId, Capability::ChannelEdit, request.channelId);
        return;
    }
    if (request.maxUsers > m_config.maxUsersPerChannelLimit) {
        refuse(client.id, request.requestId, ErrorCode::InvalidRequest, QStringLiteral("user limit is too high"));
        return;
    }
    const bool nameTaken = std::any_of(m_channels.cbegin(), m_channels.cend(), [&](const Channel &c) {
        return c.info.id != request.channelId && c.info.parentId == it->info.parentId &&
               c.info.name.compare(request.name, Qt::CaseInsensitive) == 0;
    });
    if (nameTaken) {
        refuse(client.id, request.requestId, ErrorCode::NameInUse,
               QStringLiteral("a channel with that name already exists here"));
        return;
    }
    if (!request.changePassword || request.password.isEmpty()) {
        finishEditChannel(client.id, request, request.changePassword ? QByteArray() : it->passwordHash);
        return;
    }
    const quint32 clientId = client.id;
    m_passwords.hash(request.password, this, [this, clientId, request](std::optional<QByteArray> hash) {
        if (onlineClient(clientId) == nullptr) {
            return;
        }
        if (!hash) {
            refuse(clientId, request.requestId, ErrorCode::InternalError,
                   QStringLiteral("could not store the password"));
            return;
        }
        finishEditChannel(clientId, request, *hash);
    });
}

void ServerCore::finishEditChannel(quint32 clientId, const EditChannel &request, const QByteArray &passwordHash)
{
    Client *client = onlineClient(clientId);
    auto it = m_channels.find(request.channelId);
    if (client == nullptr || it == m_channels.end()) {
        if (client != nullptr) {
            refuse(clientId, request.requestId, ErrorCode::NotFound, QStringLiteral("no such channel"));
        }
        return;
    }
    if (it->info.persistent) {
        StoredChannel stored{it->info.id,  it->info.parentId == kRootChannelId ? 0 : it->info.parentId,
                             request.name, request.topic,
                             passwordHash, request.maxUsers};
        if (!m_database->updateChannel(stored)) {
            refuse(clientId, request.requestId, ErrorCode::InternalError, QStringLiteral("could not save the channel"));
            return;
        }
    }
    it->info.name = request.name;
    it->info.topic = request.topic;
    it->info.maxUsers = request.maxUsers;
    it->info.hasPassword = !passwordHash.isEmpty();
    it->passwordHash = passwordHash;
    audit(client, QStringLiteral("channel.edited"), request.name, request.channelId, QString(),
          request.changePassword ? QStringLiteral("passwordChanged=yes") : QString());
    broadcast(ChannelUpdated{it->info});
    reply(clientId, request.requestId);
}

void ServerCore::moveUser(Client &client, quint32 channelId)
{
    const quint32 previous = client.user.channelId;
    if (previous == channelId) {
        return;
    }
    if (auto old = m_channels.find(previous); old != m_channels.end()) {
        old->members.remove(client.id);
    }
    m_channels[channelId].members.insert(client.id);
    client.user.channelId = channelId;
    broadcast(UserMoved{client.id, channelId});
    removeIfEmptyTemporary(previous);
    // Channel-scoped mutes may differ in the new channel.
    refreshUser(client);
}

void ServerCore::removeIfEmptyTemporary(quint32 channelId)
{
    const auto it = m_channels.constFind(channelId);
    if (it != m_channels.cend() && !it->info.persistent && it->members.isEmpty()) {
        m_channels.remove(channelId);
        broadcast(ChannelRemoved{channelId});
    }
}

// ---------------------------------------------------------------------------- text

bool ServerCore::takeTextToken(Client &client)
{
    const qint64 now = m_now();
    client.textTokens =
        std::min(m_config.textBurst,
                 client.textTokens + static_cast<double>(now - client.textRefillMs) / 1000.0 * m_config.textPerSecond);
    client.textRefillMs = now;
    if (client.textTokens < 1.0) {
        return false;
    }
    client.textTokens -= 1.0;
    return true;
}

void ServerCore::setStatus(Client &client, const SetStatus &request)
{
    const QString status = request.status.trimmed();
    // A status is shown to everyone, like a message, so it obeys text mutes and the flood limit.
    if (!status.isEmpty() &&
        m_sanctions.isTextMuted(subjectOf(client), chainOf(client.user.channelId), m_wallClock())) {
        refuse(client.id, request.requestId, ErrorCode::Muted, QStringLiteral("you are muted"));
        return;
    }
    if (!takeTextToken(client)) {
        refuse(client.id, request.requestId, ErrorCode::RateLimited, QStringLiteral("you are sending too fast"));
        return;
    }
    reply(client.id, request.requestId);
    if (status != client.user.status) {
        client.user.status = status;
        broadcast(UserUpdated{client.id, static_cast<quint32>(client.user.role), client.user.flags, status});
    }
}

void ServerCore::sendText(Client &client, const SendText &request)
{
    if (!takeTextToken(client)) {
        refuse(client.id, request.requestId, ErrorCode::RateLimited, QStringLiteral("you are sending too fast"));
        return;
    }

    const quint32 here = client.user.channelId;
    if (m_sanctions.isTextMuted(subjectOf(client), chainOf(here), m_wallClock())) {
        refuse(client.id, request.requestId, ErrorCode::Muted, QStringLiteral("you are muted"));
        return;
    }
    TextMessage message{request.target, client.id, request.targetId, request.text, m_wallClock()};
    switch (request.target) {
    case TextTarget::Channel: {
        if (request.targetId != here) {
            refuse(client.id, request.requestId, ErrorCode::NotAuthorized,
                   QStringLiteral("you can only write to the channel you are in"));
            return;
        }
        if (!can(client, Capability::TextSend, here)) {
            refuseUnauthorized(client, request.requestId, Capability::TextSend, here);
            return;
        }
        const QSet<quint32> members = m_channels.value(request.targetId).members;
        for (quint32 member : members) {
            emit sendTo(member, message);
        }
        break;
    }
    case TextTarget::User:
        if (!can(client, Capability::TextPrivate)) {
            refuseUnauthorized(client, request.requestId, Capability::TextPrivate);
            return;
        }
        if (request.targetId == client.id || onlineClient(request.targetId) == nullptr) {
            refuse(client.id, request.requestId, ErrorCode::NotFound, QStringLiteral("that user is not online"));
            return;
        }
        emit sendTo(request.targetId, message);
        emit sendTo(client.id, message); // echo, so the sender sees the server's time stamp
        break;
    case TextTarget::Server:
        if (!can(client, Capability::BroadcastSend)) {
            refuseUnauthorized(client, request.requestId, Capability::BroadcastSend);
            return;
        }
        message.targetId = 0;
        broadcast(message);
        break;
    }
    reply(client.id, request.requestId);
}

// ------------------------------------------------------------------------- helpers

void ServerCore::broadcast(const Message &message, quint32 exceptSessionId)
{
    const QList<quint32> ids = m_clients.keys();
    for (quint32 id : ids) {
        const auto it = m_clients.constFind(id);
        if (id != exceptSessionId && it != m_clients.cend() && it->state == ClientState::Online) {
            emit sendTo(id, message);
        }
    }
}

void ServerCore::reply(quint32 sessionId, quint64 requestId, std::optional<quint32> resultId)
{
    emit sendTo(sessionId, RequestDone{requestId, resultId});
}

void ServerCore::refuse(quint32 sessionId, quint64 requestId, ErrorCode code, const QString &detail)
{
    emit sendTo(sessionId, Error{code, requestId, detail});
}

void ServerCore::refuseUnauthorized(const Client &client, quint64 requestId, Capability capability, quint32 channelId)
{
    const moderation::Decision decision = m_permissions.check(actorOf(client), capability, chainOf(channelId));
    refuse(client.id, requestId, ErrorCode::NotAuthorized,
           QStringLiteral("%1: %2").arg(moderation::capabilityName(capability), decision.reason));
}

} // namespace opentalk::server
