// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/plugins/PluginManifest.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

namespace opentalk::plugins {
namespace {

struct CapabilityInfo {
    PluginCapability capability;
    const char *name;
    const char *description;
    bool sensitive;
};

constexpr CapabilityInfo kCapabilities[] = {
    {PluginCapability::PresenceRead, "presence.read", "See the channels and who is in them.", false},
    {PluginCapability::MessagesRead, "messages.read", "Read the messages in your channel and messages to everyone.",
     true},
    {PluginCapability::PrivateRead, "messages.private", "Read your private messages, sent and received.", true},
    {PluginCapability::MessagesSend, "messages.send", "Send messages in your name.", true},
    {PluginCapability::ChannelsJoin, "channels.join", "Move you to another channel.", true},
    {PluginCapability::NotificationsShow, "notifications.show",
     "Add lines to your history and have your screen reader speak them.", false},
    {PluginCapability::CommandsAdd, "commands.add", "Add items to the Plugins menu.", false},
};

constexpr qsizetype kMaxManifestBytes = 64 * 1024;

const CapabilityInfo *infoFor(PluginCapability capability)
{
    for (const CapabilityInfo &info : kCapabilities) {
        if (info.capability == capability) {
            return &info;
        }
    }
    return nullptr;
}

/// Visible text without control or invisible formatting characters.
bool isCleanText(const QString &text, qsizetype maxLength, bool allowEmpty)
{
    if (text.size() > maxLength || (!allowEmpty && text.trimmed().isEmpty())) {
        return false;
    }
    for (const QChar c : text) {
        const QChar::Category category = c.category();
        if (category == QChar::Other_Control || category == QChar::Other_Format) {
            return false;
        }
    }
    return true;
}

QString platformKey()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

/// A relative path that stays inside the plugin's folder.
bool isSafeRelativePath(const QString &path)
{
    if (path.isEmpty() || path.size() > 200 || path.startsWith(QLatin1Char('/')) ||
        path.startsWith(QLatin1Char('\\')) || path.contains(QLatin1Char(':')) || !isCleanText(path, 200, false)) {
        return false;
    }
    const QStringList parts = path.split(QRegularExpression(QStringLiteral("[/\\\\]")));
    return std::none_of(parts.cbegin(), parts.cend(),
                        [](const QString &part) { return part == QLatin1String("..") || part.isEmpty(); });
}

bool fail(QString *error, const QString &message)
{
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

} // namespace

QString capabilityName(PluginCapability capability)
{
    const CapabilityInfo *info = infoFor(capability);
    return info != nullptr ? QString::fromLatin1(info->name) : QString();
}

std::optional<PluginCapability> pluginCapabilityFromName(const QString &name)
{
    for (const CapabilityInfo &info : kCapabilities) {
        if (name == QLatin1String(info.name)) {
            return info.capability;
        }
    }
    return std::nullopt;
}

QList<PluginCapability> allPluginCapabilities()
{
    QList<PluginCapability> result;
    for (const CapabilityInfo &info : kCapabilities) {
        result.append(info.capability);
    }
    return result;
}

QString describe(PluginCapability capability)
{
    const CapabilityInfo *info = infoFor(capability);
    return info != nullptr ? QString::fromLatin1(info->description) : QString();
}

bool isSensitive(PluginCapability capability)
{
    const CapabilityInfo *info = infoFor(capability);
    return info != nullptr && info->sensitive;
}

QStringList allowedInterpreters()
{
    return {QStringLiteral("python3"), QStringLiteral("python"), QStringLiteral("node")};
}

bool PluginManifest::isValidId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9][a-z0-9-]*(\\.[a-z0-9][a-z0-9-]*)+$"));
    return id.size() <= 100 && pattern.match(id).hasMatch();
}

std::optional<PluginManifest> PluginManifest::parse(const QByteArray &json, QString *error)
{
    if (json.size() > kMaxManifestBytes) {
        fail(error, QStringLiteral("plugin.json is larger than 64 KiB"));
        return std::nullopt;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fail(error, parseError.error != QJsonParseError::NoError
                        ? QStringLiteral("plugin.json is not valid JSON: %1").arg(parseError.errorString())
                        : QStringLiteral("plugin.json must contain a JSON object"));
        return std::nullopt;
    }
    const QJsonObject object = document.object();
    PluginManifest m;
    const auto text = [&object](const char *key) { return object.value(QLatin1String(key)).toString(); };
    const auto isStringOrMissing = [&object](const char *key) {
        const QJsonValue v = object.value(QLatin1String(key));
        return v.isUndefined() || v.isString();
    };
    for (const char *key : {"id", "name", "version", "description", "author", "license", "homepage", "interpreter"}) {
        if (!isStringOrMissing(key)) {
            fail(error, QStringLiteral("\"%1\" must be text").arg(QLatin1String(key)));
            return std::nullopt;
        }
    }

    m.id = text("id");
    if (!isValidId(m.id)) {
        fail(error, QStringLiteral("\"id\" must be a lower-case reverse domain name such as org.example.dice"));
        return std::nullopt;
    }
    m.name = text("name").trimmed();
    if (!isCleanText(m.name, 64, false)) {
        fail(error, QStringLiteral("\"name\" must be 1 to 64 visible characters"));
        return std::nullopt;
    }
    m.version = text("version");
    static const QRegularExpression versionPattern(QStringLiteral("^[0-9A-Za-z.+-]{1,32}$"));
    if (!versionPattern.match(m.version).hasMatch()) {
        fail(error, QStringLiteral("\"version\" must be a version such as 1.0.0"));
        return std::nullopt;
    }
    m.description = text("description").trimmed();
    m.author = text("author").trimmed();
    m.license = text("license").trimmed();
    m.homepage = text("homepage").trimmed();
    if (!isCleanText(m.description, 500, true) || !isCleanText(m.author, 100, true) ||
        !isCleanText(m.license, 64, true) || !isCleanText(m.homepage, 200, true)) {
        fail(error, QStringLiteral("\"description\", \"author\", \"license\" or \"homepage\" is too long or contains "
                                   "invisible characters"));
        return std::nullopt;
    }
    if (!m.homepage.isEmpty() && !m.homepage.startsWith(QLatin1String("https://")) &&
        !m.homepage.startsWith(QLatin1String("http://"))) {
        fail(error, QStringLiteral("\"homepage\" must be a web address"));
        return std::nullopt;
    }

    const QJsonValue api = object.value(QLatin1String("apiVersion"));
    if (!api.isUndefined()) {
        if (!api.isDouble() || api.toDouble() != static_cast<double>(api.toInt()) || api.toInt() < 1) {
            fail(error, QStringLiteral("\"apiVersion\" must be a whole number from 1"));
            return std::nullopt;
        }
        m.apiVersion = api.toInt();
        if (m.apiVersion > kPluginApiVersion) {
            fail(error, QStringLiteral("this plugin needs a newer version of OpenTalk (plugin API %1; this version "
                                       "supports %2)")
                            .arg(m.apiVersion)
                            .arg(kPluginApiVersion));
            return std::nullopt;
        }
    }

    const QJsonValue executable = object.value(QLatin1String("executable"));
    if (executable.isString()) {
        m.executable = executable.toString();
    } else if (executable.isObject()) {
        const QJsonValue forThisSystem = executable.toObject().value(platformKey());
        if (!forThisSystem.isString()) {
            fail(error,
                 QStringLiteral("this plugin does not support %1 (no \"executable\" for it)").arg(platformKey()));
            return std::nullopt;
        }
        m.executable = forThisSystem.toString();
    } else {
        fail(error, QStringLiteral("\"executable\" is missing"));
        return std::nullopt;
    }
    if (!isSafeRelativePath(m.executable)) {
        fail(error, QStringLiteral("\"executable\" must be a path inside the plugin's folder"));
        return std::nullopt;
    }
    m.interpreter = text("interpreter");
    if (!m.interpreter.isEmpty() && !allowedInterpreters().contains(m.interpreter)) {
        fail(
            error,
            QStringLiteral("\"interpreter\" must be one of: %1").arg(allowedInterpreters().join(QStringLiteral(", "))));
        return std::nullopt;
    }

    const QJsonValue arguments = object.value(QLatin1String("arguments"));
    if (!arguments.isUndefined()) {
        if (!arguments.isArray() || arguments.toArray().size() > 20) {
            fail(error, QStringLiteral("\"arguments\" must be a list of at most 20 texts"));
            return std::nullopt;
        }
        for (const QJsonValue &argument : arguments.toArray()) {
            if (!argument.isString() || !isCleanText(argument.toString(), 200, true)) {
                fail(error, QStringLiteral("each argument must be text of at most 200 visible characters"));
                return std::nullopt;
            }
            m.arguments.append(argument.toString());
        }
    }

    const QJsonValue capabilities = object.value(QLatin1String("capabilities"));
    if (!capabilities.isUndefined()) {
        if (!capabilities.isArray()) {
            fail(error, QStringLiteral("\"capabilities\" must be a list"));
            return std::nullopt;
        }
        for (const QJsonValue &value : capabilities.toArray()) {
            const auto capability = pluginCapabilityFromName(value.toString());
            if (!value.isString() || !capability) {
                fail(error, QStringLiteral("unknown capability \"%1\" (the plugin may need a newer OpenTalk)")
                                .arg(value.toString()));
                return std::nullopt;
            }
            if (m.capabilities.contains(*capability)) {
                fail(error, QStringLiteral("capability \"%1\" is listed twice").arg(value.toString()));
                return std::nullopt;
            }
            m.capabilities.append(*capability);
        }
    }
    m.hash = QCryptographicHash::hash(json, QCryptographicHash::Sha256);
    return m;
}

std::optional<PluginManifest> PluginManifest::load(const QString &directory, QString *error)
{
    QFile file(QDir(directory).filePath(QStringLiteral("plugin.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        fail(error, QStringLiteral("no plugin.json in %1").arg(QDir::toNativeSeparators(directory)));
        return std::nullopt;
    }
    return parse(file.read(kMaxManifestBytes + 1), error);
}

std::optional<LaunchCommand> launchCommand(const PluginManifest &manifest, const QString &directory, QString *error)
{
    const QString root = QFileInfo(directory).canonicalFilePath();
    const QFileInfo target(QDir(directory).filePath(manifest.executable));
    const QString resolved = target.canonicalFilePath();
    // canonicalFilePath() resolves symbolic links, so a link pointing elsewhere is caught.
    if (root.isEmpty() || resolved.isEmpty() || !target.isFile()) {
        fail(error, QStringLiteral("the plugin's program %1 is missing").arg(manifest.executable));
        return std::nullopt;
    }
    if (!resolved.startsWith(root + QLatin1Char('/'))) {
        fail(error, QStringLiteral("the plugin's program is outside its folder"));
        return std::nullopt;
    }
    if (manifest.interpreter.isEmpty()) {
        if (!target.isExecutable()) {
            fail(error, QStringLiteral("the plugin's program %1 cannot be run").arg(manifest.executable));
            return std::nullopt;
        }
        return LaunchCommand{resolved, manifest.arguments};
    }
    QString program = QStandardPaths::findExecutable(manifest.interpreter);
#ifdef Q_OS_WIN
    if (program.isEmpty() && manifest.interpreter == QLatin1String("python3")) {
        program = QStandardPaths::findExecutable(QStringLiteral("python")); // the usual name on Windows
    }
#endif
    if (program.isEmpty()) {
        fail(error, QStringLiteral("this plugin needs %1, which is not installed").arg(manifest.interpreter));
        return std::nullopt;
    }
    return LaunchCommand{program, QStringList{resolved} + manifest.arguments};
}

} // namespace opentalk::plugins
