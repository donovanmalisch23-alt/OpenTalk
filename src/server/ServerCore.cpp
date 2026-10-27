// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/server/ServerCore.h"

#include "opentalk/logging/Logging.h"
#include "opentalk/server/PasswordWorker.h"
#include "opentalk/server/ServerDatabase.h"

#include <QRandomGenerator>

#include <algorithm>

namespace opentalk::server {

using namespace protocol;

ServerCore::ServerCore(Config config, ServerDatabase *database, PasswordWorker &passwords,
                       std::function<qint64()> nowMs, std::function<qint64()> wallClockMs, QObject *parent)
    : QObject(parent)
    , m_config(config)
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
}

ServerCore::~ServerCore() = default;

bool ServerCore::loadChannels(QString *errorMessage)
{
    if (m_database == nullptr) {
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
    } else if (const auto *text = std::get_if<SendText>(&message)) {
        sendText(client, *text);
    } else if (std::holds_alternative<Error>(message)) {
        // Informational; nothing to do.
    } else {
        emit closeSession(sessionId, ErrorCode::ProtocolViolation, QStringLiteral("unexpected message"));
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

void ServerCore::authenticate(Client &client, const Authenticate &request)
{
    const quint32 sessionId = client.id;
    if (tooManyFailedLogins(client.address)) {
        emit closeSession(sessionId, ErrorCode::AuthenticationFailed,
                          QStringLiteral("too many failed logins from your address; try again later"));
        return;
    }

    if (request.mode == Authenticate::Mode::Guest) {
        if (!m_config.allowGuests) {
            emit closeSession(sessionId, ErrorCode::AuthenticationFailed,
                              QStringLiteral("this server does not allow guests"));
            return;
        }
        const bool reserved = m_database != nullptr && m_database->findAccount(request.name).has_value();
        if (reserved || nameTakenOnline(request.name)) {
            emit closeSession(sessionId, ErrorCode::NameInUse, QStringLiteral("that name is already in use"));
            return;
        }
        goOnline(client, request.name, Role::Guest);
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
        goOnline(*it, account->username, account->role);
    });
}

void ServerCore::goOnline(Client &client, const QString &name, Role role)
{
    client.state = ClientState::Online;
    client.user = UserInfo{client.id, name, role, kRootChannelId};
    m_channels[kRootChannelId].members.insert(client.id);
    qCInfo(lcServer).noquote() << "Session" << client.id << "logged in as" << name << "(" << toString(role) << ")";

    emit sendTo(client.id, AuthResult{client.id, name, role});
    StateSnapshot snapshot;
    for (const Channel &c : std::as_const(m_channels)) {
        snapshot.channels.append(c.info); // ordered by ID, so parents come before children
    }
    for (const Client &c : std::as_const(m_clients)) {
        if (c.state == ClientState::Online) {
            snapshot.users.append(c.user);
        }
    }
    emit sendTo(client.id, snapshot);
    broadcast(UserJoined{client.user}, client.id);
}

// ------------------------------------------------------------------------ channels

ServerCore::Client *ServerCore::onlineClient(quint32 id)
{
    auto it = m_clients.find(id);
    return (it != m_clients.end() && it->state == ClientState::Online) ? &*it : nullptr;
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
    const bool admin = client.user.role == Role::Admin;
    if (!admin && target->info.maxUsers > 0 && target->members.size() >= target->info.maxUsers) {
        refuse(client.id, request.requestId, ErrorCode::ChannelFull, QStringLiteral("the channel is full"));
        return;
    }
    if (admin || target->passwordHash.isEmpty()) {
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
    const Role role = client.user.role;
    if (role == Role::Guest || (request.persistent && role != Role::Admin)) {
        refuse(client.id, request.requestId, ErrorCode::NotAuthorized,
               request.persistent ? QStringLiteral("only administrators can create persistent channels")
                                  : QStringLiteral("guests cannot create channels"));
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
    if (client.user.role != Role::Admin) {
        refuse(client.id, request.requestId, ErrorCode::NotAuthorized,
               QStringLiteral("only administrators can delete channels"));
        return;
    }
    if (request.channelId == kRootChannelId) {
        refuse(client.id, request.requestId, ErrorCode::InvalidRequest, QStringLiteral("the root cannot be deleted"));
        return;
    }
    const auto it = m_channels.find(request.channelId);
    if (it == m_channels.end()) {
        refuse(client.id, request.requestId, ErrorCode::NotFound, QStringLiteral("no such channel"));
        return;
    }
    const bool hasChildren = std::any_of(m_channels.cbegin(), m_channels.cend(),
                                         [&request](const Channel &c) { return c.info.parentId == request.channelId; });
    if (hasChildren) {
        refuse(client.id, request.requestId, ErrorCode::InvalidRequest,
               QStringLiteral("delete its sub-channels first"));
        return;
    }
    if (it->info.persistent && !m_database->deleteChannel(request.channelId)) {
        refuse(client.id, request.requestId, ErrorCode::InternalError, QStringLiteral("could not delete the channel"));
        return;
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
    broadcast(ChannelRemoved{request.channelId});
    reply(client.id, request.requestId);
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

void ServerCore::sendText(Client &client, const SendText &request)
{
    const qint64 now = m_now();
    client.textTokens =
        std::min(m_config.textBurst,
                 client.textTokens + static_cast<double>(now - client.textRefillMs) / 1000.0 * m_config.textPerSecond);
    client.textRefillMs = now;
    if (client.textTokens < 1.0) {
        refuse(client.id, request.requestId, ErrorCode::RateLimited, QStringLiteral("you are sending too fast"));
        return;
    }
    client.textTokens -= 1.0;

    TextMessage message{request.target, client.id, request.targetId, request.text, m_wallClock()};
    switch (request.target) {
    case TextTarget::Channel: {
        if (request.targetId != client.user.channelId) {
            refuse(client.id, request.requestId, ErrorCode::NotAuthorized,
                   QStringLiteral("you can only write to the channel you are in"));
            return;
        }
        const QSet<quint32> members = m_channels.value(request.targetId).members;
        for (quint32 member : members) {
            emit sendTo(member, message);
        }
        break;
    }
    case TextTarget::User:
        if (request.targetId == client.id || onlineClient(request.targetId) == nullptr) {
            refuse(client.id, request.requestId, ErrorCode::NotFound, QStringLiteral("that user is not online"));
            return;
        }
        emit sendTo(request.targetId, message);
        emit sendTo(client.id, message); // echo, so the sender sees the server's time stamp
        break;
    case TextTarget::Server:
        if (client.user.role != Role::Admin) {
            refuse(client.id, request.requestId, ErrorCode::NotAuthorized,
                   QStringLiteral("only administrators can send server-wide messages"));
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

} // namespace opentalk::server
