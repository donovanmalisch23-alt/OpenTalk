// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/plugins/PluginHost.h"
#include "opentalk/plugins/PluginRegistry.h"

#include <QDialog>
#include <QList>

class QLabel;
class QListWidget;
class QPushButton;
class QTreeWidget;

namespace opentalk::ui {

/// Asks the user to allow a plugin to run, listing what it will be able to do. Each
/// permission can be unticked; the plugin then runs without it.
class PluginConsentDialog : public QDialog {
    Q_OBJECT

public:
    PluginConsentDialog(const plugins::InstalledPlugin &plugin, QWidget *parent = nullptr);
    QList<plugins::PluginCapability> granted() const;
    QListWidget *permissionList() const { return m_permissions; }

private:
    QListWidget *m_permissions = nullptr;
};

/// Lists installed plugins and lets the user install, turn on (with consent), turn
/// off, restart, review, read the log of, and remove them.
class PluginManagerDialog : public QDialog {
    Q_OBJECT

public:
    PluginManagerDialog(plugins::PluginRegistry &registry, plugins::PluginHost &host, QWidget *parent = nullptr);

    QTreeWidget *pluginList() const { return m_list; }
    /// Installs from a folder (what "Install from folder..." does after asking).
    bool installFrom(const QString &folder);
    QString statusText() const;

signals:
    void statusChanged(const QString &text);

private:
    void refresh();
    void updateDetails();
    std::optional<plugins::InstalledPlugin> selected() const;
    void turnOn();
    void turnOff();
    void restart();
    void remove();
    void showLog();
    void setStatus(const QString &text);
    QString stateText(const plugins::InstalledPlugin &plugin) const;

    plugins::PluginRegistry &m_registry;
    plugins::PluginHost &m_host;
    QTreeWidget *m_list = nullptr;
    QLabel *m_details = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_install = nullptr;
    QPushButton *m_turnOn = nullptr;
    QPushButton *m_turnOff = nullptr;
    QPushButton *m_restart = nullptr;
    QPushButton *m_log = nullptr;
    QPushButton *m_remove = nullptr;
};

} // namespace opentalk::ui
