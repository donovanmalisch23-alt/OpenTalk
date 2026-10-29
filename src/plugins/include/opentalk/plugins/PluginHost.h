// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/plugins/PluginProcess.h"
#include "opentalk/plugins/PluginRegistry.h"
#include "opentalk/protocol/Messages.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>

namespace opentalk::client {
class Connection;
}

namespace opentalk::plugins {

/// An item a plugin added to the Plugins menu.
struct PluginCommand {
    QString pluginId;
    QString pluginName;
    QString id;
    QString title;
    friend bool operator==(const PluginCommand &, const PluginCommand &) = default;
};

/// Runs the enabled plugins and connects them to the app: it tells each plugin what it
/// was granted to know, and carries out only the requests it was granted to make. This
/// is where plugin capabilities are enforced (see docs/plugins/ipc.md).
class PluginHost : public QObject {
    Q_OBJECT

public:
    struct Limits {
        PluginProcess::Limits process;
        double sendsPerSecond = 1.0; ///< Messages a plugin may send in your name.
        double sendBurst = 5.0;
        double notificationsPerSecond = 2.0;
        double notificationBurst = 5.0;
        int maxCommands = 20;
        int maxNotificationLength = 500;
    };

    PluginHost(PluginRegistry &registry, client::Connection &connection, QString logDir, Limits limits,
               QObject *parent = nullptr);
    ~PluginHost() override;

    /// Starts every plugin the user enabled and agreed to.
    void startEnabled();
    void stopAll();
    /// Starts (or restarts) one plugin, if it may run. Returns false with a reason.
    bool start(const QString &id, QString *error = nullptr);
    void stop(const QString &id);
    /// Stops the plugin and waits (running the event loop) until it has exited, at most
    /// the stop grace period plus a second. Use before replacing or deleting its files.
    void stopAndWait(const QString &id);
    /// Forgets a plugin that was removed.
    void forget(const QString &id);

    PluginProcess *process(const QString &id) const { return m_plugins.value(id).process; }
    PluginProcess::State state(const QString &id) const;
    QString logFileFor(const QString &id) const;
    /// Menu items from all running plugins, by plugin then in the plugin's order.
    QList<PluginCommand> commands() const;
    void runCommand(const QString &pluginId, const QString &commandId);

signals:
    /// A plugin asked to tell the user something.
    void notification(const QString &pluginName, const QString &text, bool speak);
    void commandsChanged();
    void stateChanged(const QString &pluginId);
    /// Something went wrong with a plugin (it crashed, misbehaved or was turned off).
    void problem(const QString &pluginId, const QString &text);

private:
    struct Bucket {
        double tokens = 0;
        QElapsedTimer clock;
        bool take(double rate, double burst);
    };
    struct Running {
        PluginProcess *process = nullptr;
        QList<PluginCommand> commands;
        Bucket sends;
        Bucket notifications;
        /// Connection request ID -> the plugin's own request ID.
        QHash<quint64, QJsonValue> pending;
    };

    bool granted(const QString &id, PluginCapability capability) const;
    void onReady(const QString &id, const QJsonObject &message);
    void onMessage(const QString &id, const QJsonObject &message);
    void reply(const QString &id, const QJsonValue &requestId, bool ok, const QString &error = {});
    void refuse(const QString &id, const QJsonValue &requestId, const QString &why);
    /// Sends `message` to every running plugin granted `capability` (or all, if none).
    void broadcast(const QJsonObject &message, std::optional<PluginCapability> capability = std::nullopt);
    void sendSession(const QString &id);
    void onRequestFinished(quint64 requestId, const std::optional<protocol::Error> &error);

    QJsonObject userJson(const protocol::UserInfo &user) const;
    QJsonObject channelJson(const protocol::ChannelInfo &channel) const;
    QJsonObject stateJson() const;

    PluginRegistry &m_registry;
    client::Connection &m_connection;
    QString m_logDir;
    Limits m_limits;
    QMap<QString, Running> m_plugins;
};

} // namespace opentalk::plugins
