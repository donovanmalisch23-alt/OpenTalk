// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/plugins/PluginManifest.h"

#include <QList>
#include <QMap>
#include <QString>

#include <optional>

namespace opentalk::plugins {

/// A plugin found in the plugins folder, with what the user decided about it.
struct InstalledPlugin {
    PluginManifest manifest;
    QString directory;
    bool enabled = false;
    /// What the user allowed; may be fewer than the manifest asks for.
    QList<PluginCapability> granted;
    /// True when the plugin changed since the user agreed (a new version, or new
    /// capabilities): it does not run until the user reviews it again.
    bool needsReview = false;
    /// Why the plugin cannot be used (a broken manifest), or empty.
    QString problem;
    QString folderName; ///< For plugins whose manifest could not be read.

    bool canRun() const { return enabled && !needsReview && problem.isEmpty(); }
};

/// The installed plugins (`<data>/plugins/<id>/`) and the user's decisions about them
/// (`<data>/config/plugins.json`). No plugin runs unless the user enabled it and agreed
/// to its current manifest.
///
/// No I/O happens except in the methods that say so; one thread.
class PluginRegistry {
public:
    PluginRegistry(QString pluginsDir, QString stateFile);

    QString pluginsDir() const { return m_pluginsDir; }
    /// Each plugin's own writable folder, given to it at start-up.
    QString dataDirFor(const QString &id) const;

    /// Reads the decisions file and every plugin folder.
    void load();
    QList<InstalledPlugin> plugins() const;
    std::optional<InstalledPlugin> plugin(const QString &id) const;

    /// Copies a plugin folder into the plugins folder. An existing plugin with the same
    /// ID is replaced (an update); if its manifest changed, it needs review again.
    std::optional<QString> install(const QString &sourceDir, QString *error = nullptr);
    /// Deletes the plugin's folder and the user's decisions about it. Its data folder is
    /// kept unless `deleteData`.
    bool remove(const QString &id, bool deleteData, QString *error = nullptr);
    /// Records the user's consent to the plugin's current manifest.
    bool enable(const QString &id, const QList<PluginCapability> &granted, QString *error = nullptr);
    bool disable(const QString &id, QString *error = nullptr);

private:
    struct Decision {
        bool enabled = false;
        QList<PluginCapability> granted;
        QByteArray manifestHash;
    };
    bool saveDecisions(QString *error);
    void refresh(const QString &directory);

    QString m_pluginsDir;
    QString m_stateFile;
    QMap<QString, Decision> m_decisions;
    QMap<QString, InstalledPlugin> m_plugins; ///< By ID (or folder name when broken)
};

} // namespace opentalk::plugins
