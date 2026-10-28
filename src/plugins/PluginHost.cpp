// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/plugins/PluginHost.h"

#include "opentalk/client/ClientState.h"
#include "opentalk/client/Connection.h"
#include "opentalk/logging/Logging.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QRegularExpression>

namespace opentalk::plugins {
namespace {

using Capability = PluginCapability;

const QString kType = QStringLiteral("type");

QJsonObject typed(const QString &type)
{
    return QJsonObject{{kType, type}};
}

/// Text a plugin may show to the user: visible characters only.
bool isCleanText(const QString &text, qsizetype maxLength)
{
    if (text.trimmed().isEmpty() || text.size() > maxLength) {
        return false;
    }
    return std::none_of(text.cbegin(), text.cend(), [](QChar c) {
        return c.category() == QChar::Other_Format || (c.category() == QChar::Other_Control && c != QLatin1Char('\n'));
    });
}

} // namespace

bool PluginHost::Bucket::take(double rate, double burst)
{
    if (!clock.isValid()) {
        clock.start();
        tokens = burst;
    }
    tokens = std::min(burst, tokens + static_cast<double>(clock.restart()) / 1000.0 * rate);
    if (tokens < 1.0) {
        return false;
    }
    tokens -= 1.0;
    return true;
}

PluginHost::PluginHost(PluginRegistry &registry, client::Connection &connection, QString logDir, Limits limits,
                       QObject *parent)
    : QObject(parent)
    , m_registry(registry)
    , m_connection(connection)
    , m_logDir(std::move(logDir))
    , m_limits(limits)
{
    connect(&connection, &client::Connection::connected, this, [this] {
        for (auto it = m_plugins.cbegin(); it != m_plugins.cend(); ++it) {
            sendSession(it.key());
        }
    });
    connect(&connection, &client::Connection::disconnected, this,
            [this] { broadcast(typed(QStringLiteral("disconnected"))); });
    connect(&connection, &client::Connection::connectionLost, this,
            [this] { broadcast(typed(QStringLiteral("disconnected"))); });
    connect(&connection, &client::Connection::requestFinished, this,
            [this](quint64 id, const std::optional<protocol::Error> &error, std::optional<quint32>) {
                onRequestFinished(id, error);
            });
    connect(&connection, &client::Connection::textReceived, this, [this](const protocol::TextMessage &m) {
        const client::ClientState &state = m_connection.serverState();
        QJsonObject message = typed(QStringLiteral("message"));
        const auto from = state.user(m.fromUserId);
        message.insert(QStringLiteral("from"),
                       QJsonObject{{QStringLiteral("id"), static_cast<qint64>(m.fromUserId)},
                                   {QStringLiteral("name"), m.fromUserId == 0 ? QStringLiteral("server")
                                                            : from            ? from->name
                                                                              : QString()}});
        message.insert(QStringLiteral("fromSelf"), m.fromUserId == state.selfId());
        message.insert(QStringLiteral("text"), m.text);
        message.insert(QStringLiteral("time"),
                       QDateTime::fromMSecsSinceEpoch(m.timestampMs).toUTC().toString(Qt::ISODateWithMs));
        switch (m.target) {
        case protocol::TextTarget::Channel:
            message.insert(QStringLiteral("kind"), QStringLiteral("channel"));
            message.insert(QStringLiteral("channelId"), static_cast<qint64>(m.targetId));
            broadcast(message, Capability::MessagesRead);
            break;
        case protocol::TextTarget::Server:
            message.insert(QStringLiteral("kind"), QStringLiteral("server"));
            broadcast(message, Capability::MessagesRead);
            break;
        case protocol::TextTarget::User:
            message.insert(QStringLiteral("kind"), QStringLiteral("private"));
            message.insert(QStringLiteral("toUserId"), static_cast<qint64>(m.targetId));
            broadcast(message, Capability::PrivateRead);
            break;
        }
    });

    const client::ClientState &state = connection.serverState();
    const auto presence = [this](const QString &type, const QString &key, const QJsonObject &value) {
        QJsonObject message = typed(type);
        message.insert(key, value);
        broadcast(message, Capability::PresenceRead);
    };
    connect(&state, &client::ClientState::channelAdded, this, [this, presence](const protocol::ChannelInfo &c) {
        presence(QStringLiteral("channelAdded"), QStringLiteral("channel"), channelJson(c));
    });
    connect(&state, &client::ClientState::channelUpdated, this, [this, presence](const protocol::ChannelInfo &c) {
        presence(QStringLiteral("channelUpdated"), QStringLiteral("channel"), channelJson(c));
    });
    connect(&state, &client::ClientState::channelRemoved, this, [this, presence](const protocol::ChannelInfo &c) {
        presence(QStringLiteral("channelRemoved"), QStringLiteral("channel"), channelJson(c));
    });
    connect(&state, &client::ClientState::userJoined, this, [this, presence](const protocol::UserInfo &u) {
        presence(QStringLiteral("userJoined"), QStringLiteral("user"), userJson(u));
    });
    connect(&state, &client::ClientState::userLeft, this, [this, presence](const protocol::UserInfo &u) {
        presence(QStringLiteral("userLeft"), QStringLiteral("user"), userJson(u));
    });
    connect(&state, &client::ClientState::userMoved, this, [this, presence](const protocol::UserInfo &u, quint32) {
        presence(QStringLiteral("userMoved"), QStringLiteral("user"), userJson(u));
    });
    connect(&state, &client::ClientState::userUpdated, this,
            [this, presence](const protocol::UserInfo &u, const protocol::UserInfo &) {
                presence(QStringLiteral("userUpdated"), QStringLiteral("user"), userJson(u));
            });
}

PluginHost::~PluginHost()
{
    QObject::disconnect(&m_connection, nullptr, this, nullptr);
    QObject::disconnect(&m_connection.serverState(), nullptr, this, nullptr);
    for (const Running &r : std::as_const(m_plugins)) {
        r.process->disconnect(this);
    }
}

void PluginHost::startEnabled()
{
    for (const InstalledPlugin &p : m_registry.plugins()) {
        if (p.canRun()) {
            start(p.manifest.id);
        }
    }
}

void PluginHost::stopAll()
{
    for (auto it = m_plugins.cbegin(); it != m_plugins.cend(); ++it) {
        it->process->stop();
    }
}

QString PluginHost::logFileFor(const QString &id) const
{
    return QDir(m_logDir).filePath(id + QStringLiteral(".log"));
}

bool PluginHost::start(const QString &id, QString *error)
{
    const auto plugin = m_registry.plugin(id);
    QString why;
    if (!plugin) {
        why = tr("no such plugin");
    } else if (!plugin->problem.isEmpty()) {
        why = plugin->problem;
    } else if (!plugin->enabled) {
        why = tr("it is turned off");
    } else if (plugin->needsReview) {
        why = tr("it changed since you agreed to it; review it in the plugin manager");
    }
    if (!why.isEmpty()) {
        if (error != nullptr) {
            *error = why;
        }
        return false;
    }
    if (Running *existing = m_plugins.contains(id) ? &m_plugins[id] : nullptr) {
        // Restart with the latest decisions (for example new grants).
        existing->process->disconnect(this);
        existing->process->stop();
        existing->process->deleteLater();
        m_plugins.remove(id);
        emit commandsChanged();
    }
    Running running;
    running.process = new PluginProcess(*plugin, m_registry.dataDirFor(id), logFileFor(id), m_limits.process, this);
    m_plugins.insert(id, running);
    PluginProcess *process = running.process;
    connect(process, &PluginProcess::ready, this, [this, id](const QJsonObject &m) { onReady(id, m); });
    connect(process, &PluginProcess::messageReceived, this, [this, id](const QJsonObject &m) { onMessage(id, m); });
    connect(process, &PluginProcess::problemReported, this,
            [this, id](const QString &text) { emit problem(id, text); });
    connect(process, &PluginProcess::stateChanged, this, [this, id](PluginProcess::State s) {
        if (s != PluginProcess::State::Running && m_plugins.contains(id) && !m_plugins[id].commands.isEmpty()) {
            m_plugins[id].commands.clear();
            emit commandsChanged();
        }
        emit stateChanged(id);
    });
    qCInfo(lcPlugins).noquote() << "Starting plugin" << id;
    process->start();
    return true;
}

void PluginHost::stop(const QString &id)
{
    if (m_plugins.contains(id)) {
        m_plugins[id].process->stop();
    }
}

void PluginHost::stopAndWait(const QString &id)
{
    if (!m_plugins.contains(id)) {
        return;
    }
    PluginProcess *process = m_plugins[id].process;
    process->stop();
    QElapsedTimer waited;
    waited.start();
    while ((process->state() == PluginProcess::State::Stopping || process->state() == PluginProcess::State::Running) &&
           waited.elapsed() < m_limits.process.stopGraceMs + 1000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
}

void PluginHost::forget(const QString &id)
{
    if (!m_plugins.contains(id)) {
        return;
    }
    stopAndWait(id);
    PluginProcess *process = m_plugins.take(id).process;
    process->disconnect(this);
    process->deleteLater();
    emit commandsChanged();
    emit stateChanged(id);
}

PluginProcess::State PluginHost::state(const QString &id) const
{
    const auto it = m_plugins.constFind(id);
    return it != m_plugins.cend() ? it->process->state() : PluginProcess::State::Stopped;
}

QList<PluginCommand> PluginHost::commands() const
{
    QList<PluginCommand> result;
    for (const Running &r : m_plugins) {
        result.append(r.commands);
    }
    return result;
}

void PluginHost::runCommand(const QString &pluginId, const QString &commandId)
{
    const auto it = m_plugins.constFind(pluginId);
    if (it == m_plugins.cend() || std::none_of(it->commands.cbegin(), it->commands.cend(),
                                               [&](const PluginCommand &c) { return c.id == commandId; })) {
        return;
    }
    QJsonObject message = typed(QStringLiteral("command"));
    message.insert(QStringLiteral("id"), commandId);
    it->process->send(message);
}

bool PluginHost::granted(const QString &id, PluginCapability capability) const
{
    const auto it = m_plugins.constFind(id);
    return it != m_plugins.cend() && it->process->plugin().granted.contains(capability);
}

void PluginHost::onReady(const QString &id, const QJsonObject &message)
{
    Running &running = m_plugins[id];
    running.commands.clear();
    const QJsonArray commands = message.value(QLatin1String("commands")).toArray();
    if (!commands.isEmpty() && !granted(id, Capability::CommandsAdd)) {
        running.process->violation(tr("it added menu items without the commands.add permission"));
    } else {
        static const QRegularExpression idPattern(QStringLiteral("^[A-Za-z0-9._-]{1,40}$"));
        for (const QJsonValue &value : commands) {
            const QJsonObject command = value.toObject();
            const QString commandId = command.value(QLatin1String("id")).toString();
            const QString title = command.value(QLatin1String("title")).toString().trimmed();
            if (!idPattern.match(commandId).hasMatch() || !isCleanText(title, 60) ||
                title.contains(QLatin1Char('\n'))) {
                running.process->violation(tr("it added a menu item with an invalid ID or title"));
                continue;
            }
            if (running.commands.size() >= m_limits.maxCommands) {
                running.process->violation(tr("it added more than %1 menu items").arg(m_limits.maxCommands));
                break;
            }
            running.commands.append({id, running.process->plugin().manifest.name, commandId, title});
        }
    }
    emit commandsChanged();
    if (m_connection.state() == client::ConnectionState::Connected) {
        sendSession(id);
    }
}

void PluginHost::sendSession(const QString &id)
{
    const auto it = m_plugins.constFind(id);
    if (it == m_plugins.cend() || it->process->state() != PluginProcess::State::Running) {
        return;
    }
    const client::ClientState &state = m_connection.serverState();
    QJsonObject connected = typed(QStringLiteral("connected"));
    connected.insert(QStringLiteral("server"), m_connection.serverInfo().serverName);
    connected.insert(QStringLiteral("self"), QJsonObject{{QStringLiteral("id"), static_cast<qint64>(state.selfId())},
                                                         {QStringLiteral("name"), state.selfName()}});
    it->process->send(connected);
    if (granted(id, Capability::PresenceRead)) {
        it->process->send(stateJson());
    }
}

void PluginHost::broadcast(const QJsonObject &message, std::optional<PluginCapability> capability)
{
    for (auto it = m_plugins.cbegin(); it != m_plugins.cend(); ++it) {
        if (it->process->state() == PluginProcess::State::Running &&
            (!capability || it->process->plugin().granted.contains(*capability))) {
            it->process->send(message);
        }
    }
}

void PluginHost::reply(const QString &id, const QJsonValue &requestId, bool ok, const QString &error)
{
    if (requestId.isUndefined() || requestId.isNull() || !m_plugins.contains(id)) {
        return;
    }
    QJsonObject message = typed(QStringLiteral("result"));
    message.insert(QStringLiteral("requestId"), requestId);
    message.insert(QStringLiteral("ok"), ok);
    if (!ok) {
        message.insert(QStringLiteral("error"), error);
    }
    m_plugins[id].process->send(message);
}

void PluginHost::refuse(const QString &id, const QJsonValue &requestId, const QString &why)
{
    reply(id, requestId, false, why);
    if (m_plugins.contains(id)) {
        m_plugins[id].process->violation(why);
    }
}

void PluginHost::onMessage(const QString &id, const QJsonObject &message)
{
    Running &running = m_plugins[id];
    const QString type = message.value(QLatin1String("type")).toString();
    const QJsonValue requestId = message.value(QLatin1String("requestId"));
    const auto require = [&](PluginCapability capability) {
        if (granted(id, capability)) {
            return true;
        }
        refuse(id, requestId, tr("not allowed: %1").arg(capabilityName(capability)));
        return false;
    };
    const bool connected = m_connection.state() == client::ConnectionState::Connected;

    if (type == QLatin1String("notify")) {
        if (!require(Capability::NotificationsShow)) {
            return;
        }
        const QString text = message.value(QLatin1String("text")).toString().trimmed();
        if (!isCleanText(text, m_limits.maxNotificationLength)) {
            refuse(id, requestId,
                   tr("notifications need 1 to %1 visible characters").arg(m_limits.maxNotificationLength));
            return;
        }
        if (!running.notifications.take(m_limits.notificationsPerSecond, m_limits.notificationBurst)) {
            refuse(id, requestId, tr("too many notifications"));
            return;
        }
        emit notification(running.process->plugin().manifest.name, text,
                          message.value(QLatin1String("speak")).toBool(true));
        reply(id, requestId, true);
        return;
    }
    if (type == QLatin1String("sendMessage")) {
        if (!require(Capability::MessagesSend)) {
            return;
        }
        const QString to = message.value(QLatin1String("to")).toString(QStringLiteral("channel"));
        const QString text = message.value(QLatin1String("text")).toString();
        if (!connected) {
            reply(id, requestId, false, tr("not connected"));
            return;
        }
        if (!running.sends.take(m_limits.sendsPerSecond, m_limits.sendBurst)) {
            refuse(id, requestId, tr("too many messages; at most %1 a second").arg(m_limits.sendsPerSecond));
            return;
        }
        quint64 sent = 0;
        if (to == QLatin1String("channel")) {
            sent = m_connection.sendText(protocol::TextTarget::Channel, m_connection.serverState().currentChannelId(),
                                         text);
        } else if (to == QLatin1String("user")) {
            const quint32 userId = static_cast<quint32>(message.value(QLatin1String("userId")).toInteger());
            if (!m_connection.serverState().user(userId) || userId == m_connection.serverState().selfId()) {
                reply(id, requestId, false, tr("no such user"));
                return;
            }
            sent = m_connection.sendText(protocol::TextTarget::User, userId, text);
        } else {
            refuse(id, requestId, tr("\"to\" must be \"channel\" or \"user\""));
            return;
        }
        if (sent == 0) {
            refuse(id, requestId, tr("the text is empty, too long or contains characters that cannot be sent"));
            return;
        }
        running.pending.insert(sent, requestId);
        return;
    }
    if (type == QLatin1String("joinChannel")) {
        if (!require(Capability::ChannelsJoin)) {
            return;
        }
        const quint32 channelId = static_cast<quint32>(message.value(QLatin1String("channelId")).toInteger());
        if (!connected || !m_connection.serverState().channel(channelId)) {
            reply(id, requestId, false, connected ? tr("no such channel") : tr("not connected"));
            return;
        }
        const quint64 sent = m_connection.joinChannel(channelId, message.value(QLatin1String("password")).toString());
        running.pending.insert(sent, requestId);
        return;
    }
    if (type == QLatin1String("getState")) {
        if (!require(Capability::PresenceRead)) {
            return;
        }
        QJsonObject state = stateJson();
        state.insert(QStringLiteral("requestId"), requestId);
        running.process->send(state);
        return;
    }
    refuse(id, requestId, tr("unknown message type \"%1\"").arg(type.left(40)));
}

void PluginHost::onRequestFinished(quint64 requestId, const std::optional<protocol::Error> &error)
{
    for (auto it = m_plugins.begin(); it != m_plugins.end(); ++it) {
        const auto pending = it->pending.constFind(requestId);
        if (pending != it->pending.cend()) {
            const QJsonValue pluginRequest = *pending;
            it->pending.erase(pending);
            reply(it.key(), pluginRequest, !error, error ? error->detail : QString());
            return;
        }
    }
}

QJsonObject PluginHost::userJson(const protocol::UserInfo &user) const
{
    const client::ClientState &state = m_connection.serverState();
    return QJsonObject{{QStringLiteral("id"), static_cast<qint64>(user.id)},
                       {QStringLiteral("name"), user.name},
                       {QStringLiteral("channelId"), static_cast<qint64>(user.channelId)},
                       {QStringLiteral("role"), state.roleName(static_cast<quint32>(user.role))},
                       {QStringLiteral("self"), user.id == state.selfId()},
                       {QStringLiteral("bot"), (user.flags & protocol::BotFlag) != 0},
                       {QStringLiteral("voiceMuted"), (user.flags & protocol::VoiceMutedFlag) != 0},
                       {QStringLiteral("textMuted"), (user.flags & protocol::TextMutedFlag) != 0}};
}

QJsonObject PluginHost::channelJson(const protocol::ChannelInfo &channel) const
{
    const bool root = channel.id == protocol::kRootChannelId;
    return QJsonObject{{QStringLiteral("id"), static_cast<qint64>(channel.id)},
                       {QStringLiteral("parentId"), static_cast<qint64>(channel.parentId)},
                       {QStringLiteral("name"), root ? m_connection.serverInfo().serverName : channel.name},
                       {QStringLiteral("topic"), channel.topic},
                       {QStringLiteral("persistent"), channel.persistent},
                       {QStringLiteral("hasPassword"), channel.hasPassword},
                       {QStringLiteral("maxUsers"), static_cast<int>(channel.maxUsers)}};
}

QJsonObject PluginHost::stateJson() const
{
    const client::ClientState &state = m_connection.serverState();
    QJsonArray channels;
    for (const protocol::ChannelInfo &c : state.channels()) {
        channels.append(channelJson(c));
    }
    QJsonArray users;
    for (const protocol::UserInfo &u : state.users()) {
        users.append(userJson(u));
    }
    QJsonObject message = typed(QStringLiteral("state"));
    message.insert(QStringLiteral("channels"), channels);
    message.insert(QStringLiteral("users"), users);
    return message;
}

} // namespace opentalk::plugins
