// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/ui/ConnectDialog.h"

#include "opentalk/protocol/Messages.h"
#include "opentalk/storage/CredentialStore.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QSpinBox>
#include <QVBoxLayout>

namespace opentalk::ui {
namespace {

constexpr int kProfileIdRole = Qt::UserRole;

QLabel *buddyLabel(const QString &text, QWidget *buddy)
{
    auto *label = new QLabel(text);
    label->setBuddy(buddy);
    return label;
}

void complain(QWidget *parent, QWidget *field, const QString &message)
{
    QMessageBox::warning(parent, parent->windowTitle(), message);
    field->setFocus(Qt::OtherFocusReason);
}

} // namespace

ConnectDialog::ConnectDialog(storage::ServerProfileStore &profiles, storage::CredentialStore *credentials,
                             QWidget *parent)
    : QDialog(parent)
    , m_profiles(profiles)
    , m_credentials(credentials != nullptr && credentials->isAvailable() ? credentials : nullptr)
{
    setWindowTitle(tr("Connect to a server"));

    m_saved = new QListWidget;
    m_saved->setAccessibleName(tr("Saved servers"));
    m_name = new QLineEdit;
    m_name->setAccessibleName(tr("Server name"));
    m_name->setAccessibleDescription(tr("A name for your own list, such as \"Book club\"."));
    m_host = new QLineEdit;
    m_host->setAccessibleName(tr("Address"));
    m_host->setAccessibleDescription(tr("Host name or IP address, for example chat.example.org."));
    m_port = new QSpinBox;
    m_port->setRange(1, 65535);
    m_port->setValue(10350);
    m_port->setAccessibleName(tr("Port"));
    m_loginMode = new QComboBox;
    m_loginMode->addItem(tr("Guest (no account)"));
    m_loginMode->addItem(tr("Account"));
    m_loginMode->setAccessibleName(tr("Log in as"));
    m_user = new QLineEdit;
    m_user->setMaxLength(static_cast<int>(protocol::kMaxDisplayNameLength));
    m_password = new QLineEdit;
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setAccessibleName(tr("Password"));
    m_password->setAccessibleDescription(m_credentials != nullptr
                                             ? tr("Not saved unless you tick \"Remember my password\".")
                                             : tr("Not saved. You are asked for it each time you connect."));
    m_save = new QCheckBox(tr("&Save this server in the list"));
    m_save->setChecked(true);
    m_remember = new QCheckBox(tr("&Remember my password"));
    m_remember->setAccessibleDescription(
        m_credentials != nullptr
            ? tr("Kept in %1, protected by your sign-in, on this computer only. Never in OpenTalk's own files.")
                  .arg(m_credentials->name())
            : QString());
    m_remember->setVisible(m_credentials != nullptr);

    auto *form = new QFormLayout;
    form->addRow(buddyLabel(tr("Server &name:"), m_name), m_name);
    form->addRow(buddyLabel(tr("&Address:"), m_host), m_host);
    form->addRow(buddyLabel(tr("P&ort:"), m_port), m_port);
    form->addRow(buddyLabel(tr("&Log in as:"), m_loginMode), m_loginMode);
    m_userLabel = buddyLabel(QString(), m_user);
    form->addRow(m_userLabel, m_user);
    form->addRow(buddyLabel(tr("&Password:"), m_password), m_password);
    form->addRow(QString(), m_save);
    form->addRow(QString(), m_remember);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton *connectButton = buttons->addButton(tr("&Connect"), QDialogButtonBox::AcceptRole);
    connectButton->setDefault(true);
    m_delete = buttons->addButton(tr("&Delete saved server"), QDialogButtonBox::ResetRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &ConnectDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_delete, &QPushButton::clicked, this, &ConnectDialog::deleteSelected);
    connect(new QShortcut(QKeySequence::Delete, m_saved, nullptr, nullptr, Qt::WidgetShortcut), &QShortcut::activated,
            this, &ConnectDialog::deleteSelected);

    auto *savedLabel = buddyLabel(tr("Sa&ved servers:"), m_saved);
    auto *left = new QVBoxLayout;
    left->addWidget(savedLabel);
    left->addWidget(m_saved, 1);
    auto *body = new QHBoxLayout;
    body->addLayout(left, 1);
    body->addLayout(form, 2);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(body, 1);
    layout->addWidget(buttons);

    connect(m_loginMode, &QComboBox::currentIndexChanged, this, &ConnectDialog::updateLoginFields);
    connect(m_save, &QCheckBox::toggled, this, &ConnectDialog::updateLoginFields);
    connect(m_saved, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        const QString id = item != nullptr ? item->data(kProfileIdRole).toString() : QString();
        if (const auto profile = m_profiles.find(id)) {
            fillFrom(*profile);
        } else {
            clearForm();
        }
    });

    reloadList({});
    updateLoginFields();
    resize(640, 360);
    (m_saved->count() > 1 ? static_cast<QWidget *>(m_saved) : m_host)->setFocus();
}

void ConnectDialog::reloadList(const QString &selectId)
{
    m_saved->clear();
    auto *fresh = new QListWidgetItem(tr("New server"), m_saved);
    fresh->setData(kProfileIdRole, QString());
    QListWidgetItem *select = fresh;
    for (const storage::ServerProfile &p : m_profiles.profiles()) {
        auto *item = new QListWidgetItem(tr("%1 (%2)").arg(p.name, p.host), m_saved);
        item->setData(kProfileIdRole, p.id);
        if (p.id == selectId || (selectId.isEmpty() && select == fresh)) {
            select = item;
        }
    }
    m_saved->setCurrentItem(select);
}

void ConnectDialog::fillFrom(const storage::ServerProfile &p)
{
    m_editingId = p.id;
    m_name->setText(p.name);
    m_host->setText(p.host);
    m_port->setValue(p.port);
    m_loginMode->setCurrentIndex(p.guest ? 0 : 1);
    m_user->setText(p.userName);
    m_password->clear();
    m_remember->setChecked(p.rememberPassword);
    if (p.rememberPassword && m_credentials != nullptr) {
        if (const auto password = m_credentials->readPassword(p.id)) {
            m_password->setText(*password);
        }
    }
    m_delete->setEnabled(true);
    updateLoginFields();
}

void ConnectDialog::clearForm()
{
    m_editingId.clear();
    m_name->clear();
    m_host->clear();
    m_port->setValue(10350);
    m_loginMode->setCurrentIndex(0);
    m_user->clear();
    m_password->clear();
    m_remember->setChecked(false);
    m_delete->setEnabled(false);
    updateLoginFields();
}

void ConnectDialog::updateLoginFields()
{
    const bool account = m_loginMode->currentIndex() == 1;
    m_userLabel->setText(account ? tr("&Username:") : tr("&Nickname:"));
    m_user->setAccessibleName(account ? tr("Username") : tr("Nickname"));
    m_password->setEnabled(account);
    m_remember->setEnabled(account && m_save->isChecked());
}

void ConnectDialog::deleteSelected()
{
    if (m_editingId.isEmpty()) {
        return;
    }
    const auto profile = m_profiles.find(m_editingId);
    if (!profile ||
        QMessageBox::question(this, tr("Delete saved server"),
                              tr("Remove \"%1\" from your saved servers?").arg(profile->name)) != QMessageBox::Yes) {
        return;
    }
    if (m_credentials != nullptr) {
        m_credentials->removePassword(m_editingId);
    }
    m_profiles.remove(m_editingId);
    m_profiles.save();
    reloadList({});
    m_saved->setFocus();
}

bool ConnectDialog::validate()
{
    if (m_host->text().trimmed().isEmpty()) {
        complain(this, m_host, tr("Enter the server's address."));
        return false;
    }
    if (!protocol::isValidName(m_user->text(), protocol::kMaxDisplayNameLength)) {
        complain(this, m_user,
                 tr("Enter a name of up to %1 characters, without spaces at the start or end.")
                     .arg(protocol::kMaxDisplayNameLength));
        return false;
    }
    if (m_loginMode->currentIndex() == 1 && m_password->text().isEmpty()) {
        complain(this, m_password, tr("Enter your password."));
        return false;
    }
    return true;
}

void ConnectDialog::accept()
{
    if (validate()) {
        QDialog::accept();
    }
}

ConnectDialog::Choice ConnectDialog::choice() const
{
    Choice c;
    c.profile.id = m_editingId;
    const QString host = m_host->text().trimmed();
    c.profile.name = m_name->text().trimmed().isEmpty() ? host : m_name->text().trimmed();
    c.profile.host = host;
    c.profile.port = static_cast<quint16>(m_port->value());
    c.profile.guest = m_loginMode->currentIndex() == 0;
    c.profile.userName = m_user->text();
    if (const auto existing = m_profiles.find(m_editingId);
        existing && existing->host == c.profile.host && existing->port == c.profile.port) {
        c.profile.pinnedFingerprint = existing->pinnedFingerprint; // same server: keep the trust
    }
    c.password = c.profile.guest ? QString() : m_password->text();
    c.save = m_save->isChecked();
    c.profile.rememberPassword = m_credentials != nullptr && c.save && !c.profile.guest && m_remember->isChecked();
    return c;
}

// ------------------------------------------------------------- CreateChannelDialog

CreateChannelDialog::CreateChannelDialog(const QString &parentPath, bool canCreatePersistent, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Create channel"));
    m_name = new QLineEdit;
    m_name->setMaxLength(static_cast<int>(protocol::kMaxChannelNameLength));
    m_name->setAccessibleName(tr("Channel name"));
    m_topic = new QLineEdit;
    m_topic->setMaxLength(static_cast<int>(protocol::kMaxTopicLength));
    m_topic->setAccessibleName(tr("Topic"));
    m_password = new QLineEdit;
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setAccessibleName(tr("Password"));
    m_password->setAccessibleDescription(tr("Optional. Leave empty for a channel anyone can join."));
    m_maxUsers = new QSpinBox;
    m_maxUsers->setRange(0, 1000);
    m_maxUsers->setSpecialValueText(tr("No limit"));
    m_maxUsers->setAccessibleName(tr("Maximum users"));
    m_persistent = new QCheckBox(tr("&Keep the channel when it is empty (persistent)"));
    m_persistent->setVisible(canCreatePersistent);
    m_persistent->setEnabled(canCreatePersistent);

    auto *where = new QLabel(tr("Inside: %1").arg(parentPath));
    where->setWordWrap(true);
    auto *form = new QFormLayout;
    form->addRow(where);
    form->addRow(buddyLabel(tr("&Name:"), m_name), m_name);
    form->addRow(buddyLabel(tr("&Topic:"), m_topic), m_topic);
    form->addRow(buddyLabel(tr("&Password:"), m_password), m_password);
    form->addRow(buddyLabel(tr("&Maximum users:"), m_maxUsers), m_maxUsers);
    form->addRow(QString(), m_persistent);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    buttons->addButton(tr("C&reate"), QDialogButtonBox::AcceptRole)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &CreateChannelDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    m_name->setFocus();
}

QString CreateChannelDialog::channelName() const
{
    return m_name->text().trimmed();
}

QString CreateChannelDialog::topic() const
{
    return m_topic->text().trimmed();
}

QString CreateChannelDialog::password() const
{
    return m_password->text();
}

quint16 CreateChannelDialog::maxUsers() const
{
    return static_cast<quint16>(m_maxUsers->value());
}

bool CreateChannelDialog::persistent() const
{
    return m_persistent->isEnabled() && m_persistent->isChecked();
}

void CreateChannelDialog::accept()
{
    if (!protocol::isValidName(channelName(), protocol::kMaxChannelNameLength)) {
        complain(this, m_name, tr("Enter a channel name."));
        return;
    }
    QDialog::accept();
}

// --------------------------------------------------------------- LocalServerDialog

LocalServerDialog::LocalServerDialog(const Choice &initial, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Host a server on this computer"));
    m_name = new QLineEdit(initial.name);
    m_name->setMaxLength(static_cast<int>(protocol::kMaxNameLength));
    m_name->setAccessibleName(tr("Server name"));
    m_port = new QSpinBox;
    m_port->setRange(1, 65535);
    m_port->setValue(initial.port);
    m_port->setAccessibleName(tr("Port"));
    m_allowOthers = new QCheckBox(tr("Let &other computers connect"));
    m_allowOthers->setChecked(initial.allowOtherComputers);
    const QString othersHelp = tr("Off: only this computer can connect. On: people on your network, or on the "
                                  "internet if your router forwards the port, can connect. Windows may ask you to "
                                  "allow opentalk-server through the firewall.");
    m_allowOthers->setAccessibleDescription(othersHelp);
    m_allowGuests = new QCheckBox(tr("Let people join as &guests without an account"));
    m_allowGuests->setChecked(initial.allowGuests);
    m_adminName = new QLineEdit(initial.adminName);
    m_adminName->setMaxLength(32);
    m_adminName->setAccessibleName(tr("Administrator username"));
    m_adminPassword = new QLineEdit;
    m_adminPassword->setEchoMode(QLineEdit::Password);
    m_adminPassword->setAccessibleName(tr("Administrator password"));
    const QString adminHelp = tr("The first time, this administrator account is created (password of at least 8 "
                                 "characters). Later, enter the same details to log in as administrator. Leave "
                                 "both empty to join your server as a guest.");
    m_adminName->setAccessibleDescription(adminHelp);
    m_adminPassword->setAccessibleDescription(adminHelp);

    auto *othersLabel = new QLabel(othersHelp);
    othersLabel->setWordWrap(true);
    auto *adminLabel = new QLabel(adminHelp);
    adminLabel->setWordWrap(true);
    auto *form = new QFormLayout;
    form->addRow(buddyLabel(tr("Server &name:"), m_name), m_name);
    form->addRow(buddyLabel(tr("P&ort:"), m_port), m_port);
    form->addRow(QString(), m_allowOthers);
    form->addRow(QString(), othersLabel);
    form->addRow(QString(), m_allowGuests);
    form->addRow(buddyLabel(tr("Administrator &username:"), m_adminName), m_adminName);
    form->addRow(buddyLabel(tr("Administrator &password:"), m_adminPassword), m_adminPassword);
    form->addRow(QString(), adminLabel);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    buttons->addButton(tr("&Start server"), QDialogButtonBox::AcceptRole)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &LocalServerDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    m_name->setFocus();
}

LocalServerDialog::Choice LocalServerDialog::choice() const
{
    return {m_name->text().trimmed(),   static_cast<quint16>(m_port->value()), m_allowOthers->isChecked(),
            m_allowGuests->isChecked(), m_adminName->text().trimmed(),         m_adminPassword->text()};
}

void LocalServerDialog::accept()
{
    const Choice c = choice();
    if (c.name.isEmpty()) {
        complain(this, m_name, tr("Enter a name for the server."));
        return;
    }
    static const QRegularExpression username(QStringLiteral("^[A-Za-z0-9._-]{1,32}$"));
    if (!c.adminName.isEmpty() && !username.match(c.adminName).hasMatch()) {
        complain(this, m_adminName,
                 tr("Usernames are 1 to 32 characters: letters A to Z, digits, dot, underscore or hyphen."));
        return;
    }
    if (!c.adminName.isEmpty() && c.adminPassword.size() < 8) {
        complain(this, m_adminPassword, tr("The administrator password must be at least 8 characters."));
        return;
    }
    if (c.adminName.isEmpty() && !c.allowGuests) {
        complain(this, m_adminName,
                 tr("Enter an administrator account, or allow guests, so that you can join your server."));
        return;
    }
    QDialog::accept();
}

} // namespace opentalk::ui
