// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/ui/PluginDialogs.h"

#include "UiHelpers.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace opentalk::ui {

using namespace detail;
using plugins::InstalledPlugin;
using plugins::PluginCapability;
using State = plugins::PluginProcess::State;

namespace {

constexpr int kIdRole = Qt::UserRole;

QPushButton *button(const QString &text, QWidget *parent)
{
    auto *b = new QPushButton(text, parent);
    b->setAutoDefault(false);
    return b;
}

QString permissionsText(const QList<PluginCapability> &capabilities)
{
    QStringList names;
    for (PluginCapability c : capabilities) {
        names << plugins::capabilityName(c);
    }
    return names.isEmpty() ? QObject::tr("none") : names.join(QStringLiteral(", "));
}

} // namespace

// --------------------------------------------------------- PluginConsentDialog

PluginConsentDialog::PluginConsentDialog(const InstalledPlugin &plugin, QWidget *parent)
    : QDialog(parent)
{
    const plugins::PluginManifest &m = plugin.manifest;
    setWindowTitle(tr("Turn on %1?").arg(m.name));
    QString about = tr("%1, version %2").arg(m.name, m.version);
    if (!m.author.isEmpty()) {
        about += tr(", by %1").arg(m.author);
    }
    if (!m.license.isEmpty()) {
        about += tr(". Licence: %1").arg(m.license);
    }
    about += QLatin1Char('.');
    if (!m.description.isEmpty()) {
        about += QLatin1Char('\n') + m.description;
    }
    auto *aboutLabel = new QLabel(about);
    aboutLabel->setWordWrap(true);
    aboutLabel->setTextFormat(Qt::PlainText);
    aboutLabel->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    auto *warning = new QLabel(tr("A plugin is a separate program on your computer. OpenTalk lets it do only what "
                                  "you allow below, but it cannot stop a harmful program from damaging your "
                                  "computer. Turn on only plugins from people you trust."));
    warning->setWordWrap(true);

    const bool reviewing = plugin.enabled && plugin.needsReview;
    m_permissions = new QListWidget;
    m_permissions->setAccessibleName(tr("Permissions"));
    m_permissions->setAccessibleDescription(warning->text());
    for (PluginCapability c : m.capabilities) {
        QString text = plugins::describe(c);
        if (plugins::isSensitive(c)) {
            text = tr("Sensitive: %1").arg(text);
        }
        const bool isNew = reviewing && !plugin.granted.contains(c);
        if (isNew) {
            text += tr(" (new)");
        }
        auto *item = new QListWidgetItem(text, m_permissions);
        item->setData(kIdRole, static_cast<int>(c));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        // First time: everything it asks for. On review, new permissions start unticked.
        item->setCheckState(isNew ? Qt::Unchecked : Qt::Checked);
        item->setToolTip(plugins::capabilityName(c));
    }
    if (m_permissions->count() > 0) {
        m_permissions->setCurrentRow(0);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    buttons->addButton(tr("&Turn on"), QDialogButtonBox::AcceptRole)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(aboutLabel);
    if (reviewing) {
        auto *changed = new QLabel(tr("This plugin changed since you last turned it on. Check its permissions again."));
        changed->setWordWrap(true);
        layout->addWidget(changed);
    }
    layout->addWidget(warning);
    if (m.capabilities.isEmpty()) {
        layout->addWidget(new QLabel(tr("It asks for no permissions: it cannot see or do anything in OpenTalk.")));
        m_permissions->hide();
    } else {
        layout->addWidget(buddyLabel(tr("It will be able to (Space ticks or unticks):"), m_permissions));
    }
    layout->addWidget(m_permissions, 1);
    layout->addWidget(buttons);
    if (m.capabilities.isEmpty()) {
        buttons->setFocus();
    } else {
        m_permissions->setFocus();
    }
}

QList<PluginCapability> PluginConsentDialog::granted() const
{
    QList<PluginCapability> result;
    for (int i = 0; i < m_permissions->count(); ++i) {
        const QListWidgetItem *item = m_permissions->item(i);
        if (item->checkState() == Qt::Checked) {
            result.append(static_cast<PluginCapability>(item->data(kIdRole).toInt()));
        }
    }
    return result;
}

// --------------------------------------------------------- PluginManagerDialog

PluginManagerDialog::PluginManagerDialog(plugins::PluginRegistry &registry, plugins::PluginHost &host, QWidget *parent)
    : QDialog(parent)
    , m_registry(registry)
    , m_host(host)
{
    setWindowTitle(tr("Plugins"));
    resize(720, 480);
    m_list = new QTreeWidget;
    m_list->setAccessibleName(tr("Installed plugins"));
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setAllColumnsShowFocus(true);
    m_list->setHeaderLabels({tr("Plugin"), tr("Version"), tr("State")});
    m_list->header()->setStretchLastSection(true);
    m_details = new QLabel;
    m_details->setWordWrap(true);
    m_details->setTextFormat(Qt::PlainText);
    m_details->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    m_details->setAccessibleName(tr("Plugin details"));
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setAccessibleName(tr("Result"));
    m_install = button(tr("&Install from folder..."), this);
    m_turnOn = button(tr("Turn &on..."), this);
    m_turnOff = button(tr("Turn o&ff"), this);
    m_restart = button(tr("&Restart"), this);
    m_log = button(tr("Show &log"), this);
    m_remove = button(tr("Re&move..."), this);
    auto *buttons = new QHBoxLayout;
    for (QPushButton *b : {m_install, m_turnOn, m_turnOff, m_restart, m_log, m_remove}) {
        buttons->addWidget(b);
    }
    buttons->addStretch();
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(buddyLabel(tr("&Installed plugins:"), m_list));
    layout->addWidget(m_list, 2);
    layout->addWidget(m_details, 1);
    layout->addLayout(buttons);
    layout->addWidget(m_status);
    layout->addWidget(close);

    connect(m_install, &QPushButton::clicked, this, [this] {
        const QString folder = QFileDialog::getExistingDirectory(this, tr("Choose the plugin's folder"));
        if (!folder.isEmpty()) {
            installFrom(folder);
        }
    });
    connect(m_turnOn, &QPushButton::clicked, this, &PluginManagerDialog::turnOn);
    connect(m_turnOff, &QPushButton::clicked, this, &PluginManagerDialog::turnOff);
    connect(m_restart, &QPushButton::clicked, this, &PluginManagerDialog::restart);
    connect(m_log, &QPushButton::clicked, this, &PluginManagerDialog::showLog);
    connect(m_remove, &QPushButton::clicked, this, &PluginManagerDialog::remove);
    connect(m_list, &QTreeWidget::currentItemChanged, this, &PluginManagerDialog::updateDetails);
    connect(&m_host, &plugins::PluginHost::stateChanged, this, &PluginManagerDialog::refresh);
    refresh();
    m_list->setFocus();
}

QString PluginManagerDialog::statusText() const
{
    return m_status->text();
}

void PluginManagerDialog::setStatus(const QString &text)
{
    m_status->setText(text);
    emit statusChanged(text);
}

QString PluginManagerDialog::stateText(const InstalledPlugin &plugin) const
{
    if (!plugin.problem.isEmpty()) {
        return tr("cannot be used: %1").arg(plugin.problem);
    }
    if (!plugin.enabled) {
        return tr("off");
    }
    if (plugin.needsReview) {
        return tr("changed; turn it on again to review its permissions");
    }
    switch (m_host.state(plugin.manifest.id)) {
    case State::Running:
        return tr("running");
    case State::Starting:
        return tr("starting");
    case State::Stopping:
        return tr("stopping");
    case State::Failed:
        return tr("stopped after a problem: %1").arg(m_host.process(plugin.manifest.id)->problem());
    case State::Stopped:
        break;
    }
    return tr("on, not running");
}

void PluginManagerDialog::refresh()
{
    const QString previous =
        m_list->currentItem() != nullptr ? m_list->currentItem()->data(0, kIdRole).toString() : QString();
    m_list->clear();
    QList<InstalledPlugin> plugins = m_registry.plugins();
    std::sort(plugins.begin(), plugins.end(), [](const InstalledPlugin &a, const InstalledPlugin &b) {
        return a.manifest.name.compare(b.manifest.name, Qt::CaseInsensitive) < 0;
    });
    for (const InstalledPlugin &p : plugins) {
        const QString key = p.manifest.id.isEmpty() ? QStringLiteral("folder:") + p.folderName : p.manifest.id;
        const QString state = stateText(p);
        auto *item = new QTreeWidgetItem(m_list, {p.manifest.name, p.manifest.version, state});
        item->setData(0, kIdRole, key);
        item->setData(0, Qt::AccessibleTextRole,
                      p.manifest.version.isEmpty()
                          ? tr("%1, %2").arg(p.manifest.name, state)
                          : tr("%1, version %2, %3").arg(p.manifest.name, p.manifest.version, state));
        if (key == previous) {
            m_list->setCurrentItem(item);
        }
    }
    if (plugins.isEmpty()) {
        auto *item = new QTreeWidgetItem(m_list, {tr("No plugins installed")});
        item->setData(0, Qt::AccessibleTextRole, item->text(0));
    }
    if (m_list->currentItem() == nullptr) {
        m_list->setCurrentItem(m_list->topLevelItem(0));
    }
    updateDetails();
}

std::optional<InstalledPlugin> PluginManagerDialog::selected() const
{
    const QTreeWidgetItem *item = m_list->currentItem();
    if (item == nullptr) {
        return std::nullopt;
    }
    return m_registry.plugin(item->data(0, kIdRole).toString());
}

void PluginManagerDialog::updateDetails()
{
    const auto plugin = selected();
    m_turnOn->setEnabled(plugin && plugin->problem.isEmpty() && (!plugin->enabled || plugin->needsReview));
    m_turnOff->setEnabled(plugin && plugin->enabled);
    m_restart->setEnabled(plugin && plugin->canRun());
    m_log->setEnabled(plugin && QFile::exists(m_host.logFileFor(plugin->manifest.id)));
    m_remove->setEnabled(plugin.has_value());
    if (!plugin) {
        m_details->setText(tr("Install a plugin from its folder. Plugins are programs that add features to "
                              "OpenTalk."));
        return;
    }
    const plugins::PluginManifest &m = plugin->manifest;
    QStringList lines;
    if (!m.description.isEmpty()) {
        lines << m.description;
    }
    if (!m.author.isEmpty()) {
        lines << tr("By: %1").arg(m.author);
    }
    if (!m.license.isEmpty()) {
        lines << tr("Licence: %1").arg(m.license);
    }
    if (!m.homepage.isEmpty()) {
        lines << tr("Web page: %1").arg(m.homepage);
    }
    lines << tr("Asks for: %1").arg(permissionsText(m.capabilities));
    if (plugin->enabled) {
        lines << tr("Allowed: %1").arg(permissionsText(plugin->granted));
    }
    lines << tr("Folder: %1").arg(QDir::toNativeSeparators(plugin->directory));
    if (!plugin->problem.isEmpty()) {
        lines << tr("Problem: %1").arg(plugin->problem);
    }
    m_details->setText(lines.join(QLatin1Char('\n')));
}

bool PluginManagerDialog::installFrom(const QString &folder)
{
    QString error;
    const auto manifest = plugins::PluginManifest::load(folder, &error);
    if (!manifest) {
        setStatus(tr("Not installed: %1.").arg(error));
        return false;
    }
    const auto before = m_registry.plugin(manifest->id);
    const bool wasRunning = before && before->canRun();
    // Its files cannot be replaced while it runs (Windows locks running programs).
    m_host.stopAndWait(manifest->id);
    const auto id = m_registry.install(folder, &error);
    if (!id) {
        setStatus(tr("Not installed: %1.").arg(error));
        if (wasRunning) {
            m_host.start(manifest->id);
        }
        refresh();
        return false;
    }
    const auto installed = m_registry.plugin(*id);
    QString text;
    if (!before) {
        text = tr("Installed %1. Turn it on to use it.").arg(installed->manifest.name);
    } else if (installed->needsReview) {
        text =
            tr("Updated %1. It changed, so turn it on again to review its permissions.").arg(installed->manifest.name);
    } else {
        text = tr("Updated %1.").arg(installed->manifest.name);
        if (installed->canRun()) {
            m_host.start(*id);
        }
    }
    refresh();
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        if (m_list->topLevelItem(i)->data(0, kIdRole).toString() == *id) {
            m_list->setCurrentItem(m_list->topLevelItem(i));
        }
    }
    setStatus(text);
    return true;
}

void PluginManagerDialog::turnOn()
{
    const auto plugin = selected();
    if (!plugin) {
        return;
    }
    PluginConsentDialog consent(*plugin, this);
    if (consent.exec() != QDialog::Accepted) {
        return;
    }
    QString error;
    if (!m_registry.enable(plugin->manifest.id, consent.granted(), &error) ||
        !m_host.start(plugin->manifest.id, &error)) {
        setStatus(tr("%1 was not turned on: %2.").arg(plugin->manifest.name, error));
        refresh();
        return;
    }
    setStatus(tr("%1 is on.").arg(plugin->manifest.name));
    refresh();
}

void PluginManagerDialog::turnOff()
{
    const auto plugin = selected();
    if (!plugin) {
        return;
    }
    m_host.stop(plugin->manifest.id);
    QString error;
    if (!m_registry.disable(plugin->manifest.id, &error)) {
        setStatus(error);
    } else {
        setStatus(tr("%1 is off.").arg(plugin->manifest.name));
    }
    refresh();
}

void PluginManagerDialog::restart()
{
    const auto plugin = selected();
    QString error;
    if (!plugin || !m_host.start(plugin->manifest.id, &error)) {
        setStatus(tr("Could not restart: %1.").arg(error));
        return;
    }
    setStatus(tr("Restarting %1.").arg(plugin->manifest.name));
    refresh();
}

void PluginManagerDialog::remove()
{
    const auto plugin = selected();
    if (!plugin) {
        return;
    }
    QMessageBox box(
        QMessageBox::Question, tr("Remove plugin"),
        tr("Remove %1? You can keep the settings it saved, in case you install it again.").arg(plugin->manifest.name),
        QMessageBox::Cancel, this);
    QPushButton *keep = box.addButton(tr("Remove, &keep its settings"), QMessageBox::AcceptRole);
    QPushButton *all = box.addButton(tr("Remove &everything"), QMessageBox::DestructiveRole);
    box.setDefaultButton(keep);
    box.exec();
    if (box.clickedButton() != keep && box.clickedButton() != all) {
        return;
    }
    const QString key = m_list->currentItem()->data(0, kIdRole).toString();
    m_host.forget(key);
    QString error;
    if (!m_registry.remove(key, box.clickedButton() == all, &error)) {
        setStatus(tr("Could not remove %1: %2.").arg(plugin->manifest.name, error));
    } else {
        setStatus(tr("Removed %1.").arg(plugin->manifest.name));
    }
    refresh();
}

void PluginManagerDialog::showLog()
{
    const auto plugin = selected();
    if (!plugin) {
        return;
    }
    QFile file(m_host.logFileFor(plugin->manifest.id));
    QString text;
    if (file.open(QIODevice::ReadOnly)) {
        const qint64 size = file.size();
        file.seek(std::max<qint64>(0, size - 200 * 1024)); // the end is what matters
        text = QString::fromUtf8(file.readAll());
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Log of %1").arg(plugin->manifest.name));
    dialog.resize(640, 420);
    auto *view = new QPlainTextEdit(text.isEmpty() ? tr("The log is empty.") : text);
    view->setReadOnly(true);
    view->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    view->setAccessibleName(tr("Log of %1").arg(plugin->manifest.name));
    view->moveCursor(QTextCursor::End);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(view);
    layout->addWidget(buttons);
    view->setFocus();
    dialog.exec();
}

} // namespace opentalk::ui
