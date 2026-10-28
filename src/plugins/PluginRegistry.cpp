// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/plugins/PluginRegistry.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace opentalk::plugins {
namespace {

constexpr qint64 kMaxPluginBytes = 200LL * 1024 * 1024;
constexpr int kMaxPluginFiles = 5000;

bool fail(QString *error, const QString &message)
{
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

/// Copies a folder tree, refusing links and anything too large.
bool copyTree(const QString &from, const QString &to, QString *error)
{
    qint64 bytes = 0;
    int files = 0;
    QDirIterator it(from, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    if (!QDir().mkpath(to)) {
        return fail(error, QStringLiteral("cannot create %1").arg(QDir::toNativeSeparators(to)));
    }
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        const QString relative = QDir(from).relativeFilePath(info.filePath());
        const QString target = QDir(to).filePath(relative);
        if (info.isSymLink()) {
            return fail(error,
                        QStringLiteral("the plugin contains a link (%1); plugins must be plain files").arg(relative));
        }
        if (info.isDir()) {
            if (!QDir().mkpath(target)) {
                return fail(error, QStringLiteral("cannot create %1").arg(QDir::toNativeSeparators(target)));
            }
            continue;
        }
        bytes += info.size();
        if (++files > kMaxPluginFiles || bytes > kMaxPluginBytes) {
            return fail(error, QStringLiteral("the plugin is too large (at most 5,000 files and 200 MB)"));
        }
        // QFile::copy keeps the permissions, so programs stay executable.
        if (!QFile::copy(info.filePath(), target)) {
            return fail(error, QStringLiteral("cannot copy %1").arg(relative));
        }
    }
    return true;
}

QJsonArray namesOf(const QList<PluginCapability> &capabilities)
{
    QJsonArray array;
    for (PluginCapability c : capabilities) {
        array.append(capabilityName(c));
    }
    return array;
}

} // namespace

PluginRegistry::PluginRegistry(QString pluginsDir, QString stateFile)
    : m_pluginsDir(QDir::cleanPath(std::move(pluginsDir)))
    , m_stateFile(std::move(stateFile))
{
}

QString PluginRegistry::dataDirFor(const QString &id) const
{
    return QDir::cleanPath(m_pluginsDir + QStringLiteral("/../plugin-data/") + id);
}

void PluginRegistry::load()
{
    m_decisions.clear();
    QFile file(m_stateFile);
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonObject plugins =
            QJsonDocument::fromJson(file.read(1024 * 1024)).object().value(QLatin1String("plugins")).toObject();
        for (auto it = plugins.constBegin(); it != plugins.constEnd(); ++it) {
            const QJsonObject entry = it.value().toObject();
            Decision d;
            d.enabled = entry.value(QLatin1String("enabled")).toBool();
            d.manifestHash = QByteArray::fromHex(entry.value(QLatin1String("manifestHash")).toString().toLatin1());
            for (const QJsonValue &name : entry.value(QLatin1String("granted")).toArray()) {
                if (const auto c = pluginCapabilityFromName(name.toString())) {
                    d.granted.append(*c);
                }
            }
            if (PluginManifest::isValidId(it.key())) {
                m_decisions.insert(it.key(), d);
            }
        }
    }
    m_plugins.clear();
    const QDir dir(m_pluginsDir);
    for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!name.startsWith(QLatin1Char('.'))) {
            refresh(dir.filePath(name));
        }
    }
}

void PluginRegistry::refresh(const QString &directory)
{
    InstalledPlugin plugin;
    plugin.directory = directory;
    plugin.folderName = QFileInfo(directory).fileName();
    QString error;
    const auto manifest = PluginManifest::load(directory, &error);
    if (!manifest) {
        plugin.problem = error;
        plugin.manifest.name = plugin.folderName;
        m_plugins.insert(QStringLiteral("folder:") + plugin.folderName, plugin);
        return;
    }
    plugin.manifest = *manifest;
    if (plugin.folderName != manifest->id) {
        plugin.problem = QStringLiteral("the plugin's folder must be named %1").arg(manifest->id);
    }
    const auto decision = m_decisions.constFind(manifest->id);
    if (decision != m_decisions.cend()) {
        plugin.enabled = decision->enabled;
        for (PluginCapability c : decision->granted) {
            if (manifest->capabilities.contains(c)) {
                plugin.granted.append(c);
            }
        }
        plugin.needsReview = decision->enabled && decision->manifestHash != manifest->hash;
    }
    m_plugins.insert(manifest->id, plugin);
}

QList<InstalledPlugin> PluginRegistry::plugins() const
{
    return m_plugins.values();
}

std::optional<InstalledPlugin> PluginRegistry::plugin(const QString &id) const
{
    const auto it = m_plugins.constFind(id);
    return it != m_plugins.cend() ? std::optional<InstalledPlugin>(*it) : std::nullopt;
}

std::optional<QString> PluginRegistry::install(const QString &sourceDir, QString *error)
{
    const auto manifest = PluginManifest::load(sourceDir, error);
    if (!manifest) {
        return std::nullopt;
    }
    const QString source = QFileInfo(sourceDir).canonicalFilePath();
    const QString target = QDir(m_pluginsDir).filePath(manifest->id);
    const QString root = QFileInfo(m_pluginsDir).canonicalFilePath();
    if (!root.isEmpty() &&
        (source == root || source.startsWith(root + QLatin1Char('/')) || root.startsWith(source + QLatin1Char('/')))) {
        fail(error, QStringLiteral("choose a plugin folder outside OpenTalk's own plugins folder"));
        return std::nullopt;
    }
    // Copy next to the destination first, so a failed update leaves the old version.
    const QString staging = QDir(m_pluginsDir).filePath(QStringLiteral(".installing-") + manifest->id);
    QDir(staging).removeRecursively();
    if (!copyTree(sourceDir, staging, error)) {
        QDir(staging).removeRecursively();
        return std::nullopt;
    }
    if (QFileInfo::exists(target) && !QDir(target).removeRecursively()) {
        QDir(staging).removeRecursively();
        fail(error, QStringLiteral("cannot replace the installed version; is it running?"));
        return std::nullopt;
    }
    if (!QDir().rename(staging, target)) {
        QDir(staging).removeRecursively();
        fail(error, QStringLiteral("cannot install into %1").arg(QDir::toNativeSeparators(target)));
        return std::nullopt;
    }
    refresh(target);
    return manifest->id;
}

bool PluginRegistry::remove(const QString &id, bool deleteData, QString *error)
{
    const auto it = m_plugins.constFind(id);
    if (it == m_plugins.cend()) {
        return fail(error, QStringLiteral("no plugin %1").arg(id));
    }
    const QString directory = it->directory;
    const QString root = QFileInfo(m_pluginsDir).canonicalFilePath();
    const QString canonical = QFileInfo(directory).canonicalFilePath();
    if (root.isEmpty() || !canonical.startsWith(root + QLatin1Char('/'))) {
        return fail(error, QStringLiteral("the plugin is not in the plugins folder"));
    }
    if (!QDir(canonical).removeRecursively()) {
        return fail(error, QStringLiteral("cannot delete %1; is it running?").arg(QDir::toNativeSeparators(directory)));
    }
    if (deleteData && PluginManifest::isValidId(id)) {
        QDir(dataDirFor(id)).removeRecursively();
    }
    m_plugins.remove(id);
    m_decisions.remove(id);
    return saveDecisions(error);
}

bool PluginRegistry::enable(const QString &id, const QList<PluginCapability> &granted, QString *error)
{
    const auto it = m_plugins.constFind(id);
    if (it == m_plugins.cend() || !it->problem.isEmpty()) {
        return fail(error, it == m_plugins.cend() ? QStringLiteral("no plugin %1").arg(id) : it->problem);
    }
    Decision d;
    d.enabled = true;
    d.manifestHash = it->manifest.hash;
    for (PluginCapability c : granted) {
        if (it->manifest.capabilities.contains(c) && !d.granted.contains(c)) {
            d.granted.append(c);
        }
    }
    m_decisions.insert(id, d);
    if (!saveDecisions(error)) {
        return false;
    }
    refresh(it->directory);
    return true;
}

bool PluginRegistry::disable(const QString &id, QString *error)
{
    const auto it = m_plugins.constFind(id);
    if (it == m_plugins.cend()) {
        return fail(error, QStringLiteral("no plugin %1").arg(id));
    }
    m_decisions[id].enabled = false;
    if (!saveDecisions(error)) {
        return false;
    }
    refresh(it->directory);
    return true;
}

bool PluginRegistry::saveDecisions(QString *error)
{
    QJsonObject plugins;
    for (auto it = m_decisions.constBegin(); it != m_decisions.constEnd(); ++it) {
        plugins.insert(it.key(),
                       QJsonObject{{QStringLiteral("enabled"), it->enabled},
                                   {QStringLiteral("granted"), namesOf(it->granted)},
                                   {QStringLiteral("manifestHash"), QString::fromLatin1(it->manifestHash.toHex())}});
    }
    QDir().mkpath(QFileInfo(m_stateFile).absolutePath());
    QSaveFile file(m_stateFile);
    if (!file.open(QIODevice::WriteOnly)) {
        return fail(error, QStringLiteral("cannot write %1").arg(QDir::toNativeSeparators(m_stateFile)));
    }
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("plugins"), plugins}})
                   .toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        return fail(error, QStringLiteral("cannot write %1").arg(QDir::toNativeSeparators(m_stateFile)));
    }
    return true;
}

} // namespace opentalk::plugins
