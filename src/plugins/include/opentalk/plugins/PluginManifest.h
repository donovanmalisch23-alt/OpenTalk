// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace opentalk::plugins {

/// The plugin API version this OpenTalk speaks. A manifest asking for a newer one is
/// refused with a clear message instead of misbehaving.
inline constexpr int kPluginApiVersion = 1;

/// What a plugin may do. The user grants these when enabling a plugin, and the app
/// enforces them on every message the plugin sends or receives. Names are part of the
/// manifest format: never rename them.
enum class PluginCapability {
    PresenceRead,      ///< "presence.read": channels and who is in them.
    MessagesRead,      ///< "messages.read": messages in your channel and to everyone.
    PrivateRead,       ///< "messages.private": private messages to and from you.
    MessagesSend,      ///< "messages.send": send messages as you.
    ChannelsJoin,      ///< "channels.join": move you to another channel.
    NotificationsShow, ///< "notifications.show": lines in your history, spoken by your screen reader.
    CommandsAdd,       ///< "commands.add": items in the Plugins menu.
};

QString capabilityName(PluginCapability capability);
std::optional<PluginCapability> pluginCapabilityFromName(const QString &name);
QList<PluginCapability> allPluginCapabilities();
/// Plain-English sentence for the consent dialog, for example "Read the messages in
/// your channel and messages to everyone."
QString describe(PluginCapability capability);
/// Capabilities that can act in your name or read private conversations; the consent
/// dialog points them out.
bool isSensitive(PluginCapability capability);

/// A plugin's `plugin.json`: who made it, what to run and what it needs.
struct PluginManifest {
    QString id; ///< Reverse domain name, for example "org.example.dice".
    QString name;
    QString version;
    QString description;
    QString author;
    QString license; ///< SPDX identifier, shown to the user.
    QString homepage;
    int apiVersion = kPluginApiVersion;
    /// Program to run, relative to the plugin's folder (for this operating system).
    QString executable;
    /// Optional script interpreter found on the system: "python3", "python" or "node".
    /// `executable` is then the script.
    QString interpreter;
    QStringList arguments;
    QList<PluginCapability> capabilities;
    /// SHA-256 of the manifest file; a change means the user must review it again.
    QByteArray hash;

    /// Parses and validates a manifest. Errors name the field and say what is wrong.
    static std::optional<PluginManifest> parse(const QByteArray &json, QString *error = nullptr);
    /// Reads `<directory>/plugin.json`.
    static std::optional<PluginManifest> load(const QString &directory, QString *error = nullptr);

    static bool isValidId(const QString &id);
};

/// Interpreters a manifest may name; looked up on the system's PATH.
QStringList allowedInterpreters();

/// The program and arguments that start the plugin in `directory`, or an error if the
/// executable is missing or would be outside the plugin's folder.
struct LaunchCommand {
    QString program;
    QStringList arguments;
};
std::optional<LaunchCommand> launchCommand(const PluginManifest &manifest, const QString &directory,
                                           QString *error = nullptr);

} // namespace opentalk::plugins
