// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/ui/ModerationDialogs.h"

#include "UiHelpers.h"

#include "opentalk/client/Connection.h"
#include "opentalk/moderation/Appeals.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace opentalk::ui {

using namespace detail;
using moderation::SanctionType;

QList<QPair<QString, quint32>> sanctionDurations()
{
    return {
        {QObject::tr("10 minutes"), 10 * 60},     {QObject::tr("1 hour"), 3600},
        {QObject::tr("1 day"), 24 * 3600},        {QObject::tr("1 week"), 7 * 24 * 3600},
        {QObject::tr("30 days"), 30 * 24 * 3600}, {QObject::tr("Until lifted"), 0},
    };
}

QString sanctionTypeName(quint8 type)
{
    switch (static_cast<SanctionType>(type)) {
    case SanctionType::Warning:
        return QObject::tr("Warning");
    case SanctionType::VoiceMute:
        return QObject::tr("Voice mute");
    case SanctionType::TextMute:
        return QObject::tr("Text mute");
    case SanctionType::Mute:
        return QObject::tr("Mute");
    case SanctionType::Kick:
        return QObject::tr("Kick");
    case SanctionType::Ban:
        return QObject::tr("Ban");
    case SanctionType::Restrict:
        return QObject::tr("Restriction");
    }
    return QObject::tr("Sanction");
}

QString sanctionStateName(quint8 state)
{
    switch (static_cast<moderation::SanctionState>(state)) {
    case moderation::SanctionState::Active:
        return QObject::tr("active");
    case moderation::SanctionState::Expired:
        return QObject::tr("ended");
    case moderation::SanctionState::Revoked:
        return QObject::tr("lifted");
    case moderation::SanctionState::Overturned:
        return QObject::tr("overturned on appeal");
    }
    return QString();
}

QString appealStateName(quint8 state)
{
    switch (static_cast<moderation::Appeal::State>(state)) {
    case moderation::Appeal::State::Open:
        return QObject::tr("waiting for a decision");
    case moderation::Appeal::State::Upheld:
        return QObject::tr("upheld");
    case moderation::Appeal::State::Reduced:
        return QObject::tr("reduced");
    case moderation::Appeal::State::Overturned:
        return QObject::tr("overturned");
    }
    return QString();
}

QString reportStateName(quint8 state)
{
    switch (static_cast<moderation::Report::State>(state)) {
    case moderation::Report::State::Open:
        return QObject::tr("open");
    case moderation::Report::State::ActionTaken:
        return QObject::tr("action taken");
    case moderation::Report::State::Dismissed:
        return QObject::tr("dismissed");
    }
    return QString();
}

QString describeSanction(const protocol::SanctionInfo &s, const QString &channelName)
{
    QString text = sanctionTypeName(s.type);
    const auto type = static_cast<SanctionType>(s.type);
    if (type != SanctionType::Warning && type != SanctionType::Kick) {
        text += s.expiresMs == 0 ? QObject::tr(" until lifted")
                                 : QObject::tr(" until %1").arg(localTime(s.expiresMs, QString()));
    }
    if (s.channelId != 0) {
        text += QObject::tr(" in %1").arg(channelName.isEmpty() ? QObject::tr("a channel") : channelName);
    }
    return text;
}

// ----------------------------------------------------------------- SanctionDialog

SanctionDialog::SanctionDialog(const Options &options, QWidget *parent)
    : QDialog(parent)
    , m_options(options)
{
    setWindowTitle(tr("Moderate %1").arg(options.targetName));
    m_type = new QComboBox;
    m_type->setAccessibleName(tr("What to do"));
    for (SanctionType t : {SanctionType::Warning, SanctionType::VoiceMute, SanctionType::TextMute, SanctionType::Mute,
                           SanctionType::Kick, SanctionType::Ban, SanctionType::Restrict}) {
        m_type->addItem(sanctionTypeName(static_cast<quint8>(t)), static_cast<int>(t));
    }
    m_type->setCurrentIndex(m_type->findData(static_cast<int>(options.type)));

    m_duration = new QComboBox;
    m_duration->setAccessibleName(tr("How long"));
    fillDurations();

    m_scope = new QComboBox;
    m_scope->setAccessibleName(tr("Where"));
    m_scope->addItem(tr("The whole server"), 0u);
    if (!options.channelName.isEmpty() && options.channelId != protocol::kRootChannelId) {
        m_scope->addItem(tr("Only in %1").arg(options.channelName), options.channelId);
    }

    m_reason = new QLineEdit;
    m_reason->setMaxLength(static_cast<int>(protocol::kMaxReasonLength));
    m_reason->setAccessibleName(tr("Reason"));
    m_reasonHelp = new QLabel;
    m_reasonHelp->setWordWrap(true);

    m_banAddress = new QCheckBox(tr("Also block their &IP address"));
    m_banAddress->setVisible(options.canBanAddress);
    m_banAddress->setAccessibleDescription(
        tr("Stops new accounts or guests from the same connection. Can affect others who share it."));

    auto *form = new QFormLayout;
    form->addRow(buddyLabel(tr("&What:"), m_type), m_type);
    form->addRow(buddyLabel(tr("How &long:"), m_duration), m_duration);
    form->addRow(buddyLabel(tr("W&here:"), m_scope), m_scope);
    form->addRow(buddyLabel(tr("&Reason:"), m_reason), m_reason);
    form->addRow(QString(), m_reasonHelp);
    form->addRow(QString(), m_banAddress);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    buttons->addButton(tr("&Apply"), QDialogButtonBox::AcceptRole)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &SanctionDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    connect(m_type, &QComboBox::currentIndexChanged, this, [this] {
        fillDurations();
        updateFields();
    });
    connect(m_duration, &QComboBox::currentIndexChanged, this, &SanctionDialog::updateFields);
    updateFields();
    m_type->setFocus();
}

void SanctionDialog::fillDurations()
{
    // "Until lifted" is a permanent ban, which needs its own permission; mutes and
    // restrictions may last until lifted for anyone who may give them.
    const QSignalBlocker blocker(m_duration);
    const QVariant previous = m_duration->currentData();
    m_duration->clear();
    for (const auto &[label, seconds] : sanctionDurations()) {
        if (seconds != 0 || type() != SanctionType::Ban || m_options.canBanPermanently) {
            m_duration->addItem(label, seconds);
        }
    }
    const int index = previous.isValid() ? m_duration->findData(previous) : -1;
    m_duration->setCurrentIndex(index >= 0 ? index : 1); // 1 hour by default
}

SanctionType SanctionDialog::type() const
{
    return static_cast<SanctionType>(m_type->currentData().toInt());
}

void SanctionDialog::updateFields()
{
    const SanctionType t = type();
    const bool lasting = t != SanctionType::Warning && t != SanctionType::Kick;
    m_duration->setEnabled(lasting);
    m_scope->setEnabled(t != SanctionType::Restrict);
    if (t == SanctionType::Restrict) {
        m_scope->setCurrentIndex(0);
    }
    m_banAddress->setEnabled(t == SanctionType::Ban);
    const quint32 seconds = lasting ? m_duration->currentData().toUInt() : 0;
    const moderation::SanctionRequirements req =
        moderation::requirementsFor(t, static_cast<qint64>(seconds) * 1000, false);
    const QString help = req.reasonRequired ? tr("A reason is required. The person is told the reason.")
                                            : tr("Optional. The person is told the reason.");
    m_reasonHelp->setText(help);
    m_reason->setAccessibleDescription(help);
}

protocol::ApplySanction SanctionDialog::request() const
{
    protocol::ApplySanction r;
    const SanctionType t = type();
    r.type = static_cast<quint8>(t);
    const bool lasting = t != SanctionType::Warning && t != SanctionType::Kick;
    r.durationSeconds = lasting ? m_duration->currentData().toUInt() : 0;
    r.channelId = m_scope->currentData().toUInt();
    r.reason = m_reason->text().trimmed();
    r.banAddress = t == SanctionType::Ban && m_banAddress->isVisible() && m_banAddress->isChecked();
    return r;
}

void SanctionDialog::accept()
{
    const protocol::ApplySanction r = request();
    const moderation::SanctionRequirements req = moderation::requirementsFor(
        static_cast<SanctionType>(r.type), static_cast<qint64>(r.durationSeconds) * 1000, r.banAddress);
    if (req.reasonRequired && r.reason.isEmpty()) {
        complain(this, m_reason, tr("Please give a reason."));
        return;
    }
    QDialog::accept();
}

// ------------------------------------------------------------------- ReportDialog

ReportDialog::ReportDialog(const QString &targetName, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Report %1").arg(targetName));
    m_reason = new QLineEdit;
    m_reason->setMaxLength(static_cast<int>(protocol::kMaxReasonLength));
    m_reason->setAccessibleName(tr("What happened"));
    auto *help = helpLabel(tr("Moderators see your report and your name; %1 does not.").arg(targetName), m_reason);
    auto *form = new QFormLayout;
    form->addRow(buddyLabel(tr("&What happened:"), m_reason), m_reason);
    form->addRow(QString(), help);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    buttons->addButton(tr("&Send report"), QDialogButtonBox::AcceptRole)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &ReportDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    m_reason->setFocus();
}

QString ReportDialog::reason() const
{
    return m_reason->text().trimmed();
}

void ReportDialog::accept()
{
    if (reason().isEmpty()) {
        complain(this, m_reason, tr("Please say what happened."));
        return;
    }
    QDialog::accept();
}

// ------------------------------------------------------------ ChannelPickerDialog

ChannelPickerDialog::ChannelPickerDialog(const QString &title, const QString &label,
                                         const QList<QPair<quint32, QString>> &channels, quint32 current,
                                         QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    m_list = new QListWidget;
    m_list->setAccessibleName(label);
    for (const auto &[id, path] : channels) {
        auto *item = new QListWidgetItem(path, m_list);
        item->setData(Qt::UserRole, id);
        if (id == current) {
            m_list->setCurrentItem(item);
        }
    }
    if (m_list->currentItem() == nullptr && m_list->count() > 0) {
        m_list->setCurrentRow(0);
    }
    connect(m_list, &QListWidget::itemActivated, this, &QDialog::accept);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(buddyLabel(label, m_list));
    layout->addWidget(m_list);
    layout->addWidget(buttons);
    m_list->setFocus();
}

quint32 ChannelPickerDialog::channelId() const
{
    return m_list->currentItem() != nullptr ? m_list->currentItem()->data(Qt::UserRole).toUInt() : 0;
}

// -------------------------------------------------------------- EditChannelDialog

EditChannelDialog::EditChannelDialog(const protocol::ChannelInfo &channel, QWidget *parent)
    : QDialog(parent)
    , m_channelId(channel.id)
{
    setWindowTitle(tr("Edit %1").arg(channel.name));
    m_name = new QLineEdit(channel.name);
    m_name->setMaxLength(static_cast<int>(protocol::kMaxChannelNameLength));
    m_name->setAccessibleName(tr("Channel name"));
    m_topic = new QLineEdit(channel.topic);
    m_topic->setMaxLength(static_cast<int>(protocol::kMaxTopicLength));
    m_topic->setAccessibleName(tr("Topic"));
    m_maxUsers = new QSpinBox;
    m_maxUsers->setRange(0, 1000);
    m_maxUsers->setSpecialValueText(tr("No limit"));
    m_maxUsers->setValue(channel.maxUsers);
    m_maxUsers->setAccessibleName(tr("Maximum users"));
    m_passwordChoice = new QComboBox;
    m_passwordChoice->setAccessibleName(tr("Password"));
    m_passwordChoice->addItem(channel.hasPassword ? tr("Keep the current password") : tr("No password"), 0);
    m_passwordChoice->addItem(tr("Set a new password"), 1);
    if (channel.hasPassword) {
        m_passwordChoice->addItem(tr("Remove the password"), 2);
    }
    m_password = new QLineEdit;
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setAccessibleName(tr("New password"));
    m_password->setEnabled(false);
    connect(m_passwordChoice, &QComboBox::currentIndexChanged, this,
            [this] { m_password->setEnabled(m_passwordChoice->currentData().toInt() == 1); });
    auto *form = new QFormLayout;
    form->addRow(buddyLabel(tr("&Name:"), m_name), m_name);
    form->addRow(buddyLabel(tr("&Topic:"), m_topic), m_topic);
    form->addRow(buddyLabel(tr("&Maximum users:"), m_maxUsers), m_maxUsers);
    form->addRow(buddyLabel(tr("&Password:"), m_passwordChoice), m_passwordChoice);
    form->addRow(buddyLabel(tr("New pass&word:"), m_password), m_password);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &EditChannelDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    m_name->setFocus();
}

protocol::EditChannel EditChannelDialog::request() const
{
    protocol::EditChannel r;
    r.channelId = m_channelId;
    r.name = m_name->text().trimmed();
    r.topic = m_topic->text().trimmed();
    r.maxUsers = static_cast<quint16>(m_maxUsers->value());
    const int choice = m_passwordChoice->currentData().toInt();
    r.changePassword = choice != 0;
    r.password = choice == 1 ? m_password->text() : QString();
    return r;
}

void EditChannelDialog::accept()
{
    if (!protocol::isValidName(m_name->text().trimmed(), protocol::kMaxChannelNameLength)) {
        complain(this, m_name, tr("Enter a channel name."));
        return;
    }
    if (m_passwordChoice->currentData().toInt() == 1 && m_password->text().isEmpty()) {
        complain(this, m_password, tr("Enter the new password."));
        return;
    }
    QDialog::accept();
}

// -------------------------------------------------------------- MySanctionsDialog

MySanctionsDialog::MySanctionsDialog(client::Connection &connection, QWidget *parent)
    : QDialog(parent)
    , m_connection(connection)
{
    setWindowTitle(tr("My sanctions and appeals"));
    resize(640, 420);
    m_sanctions = new QTreeWidget;
    m_sanctions->setRootIsDecorated(false);
    m_sanctions->setAccessibleName(tr("Sanctions on you"));
    m_sanctions->setHeaderLabels({tr("What"), tr("Reason"), tr("By"), tr("State")});
    m_appeals = new QTreeWidget;
    m_appeals->setRootIsDecorated(false);
    m_appeals->setAccessibleName(tr("Your appeals"));
    m_appeals->setHeaderLabels({tr("Appeal"), tr("State"), tr("Decided by"), tr("Reason")});
    m_appeal = new QPushButton(tr("&Appeal the selected sanction..."));
    m_status = new QLabel;
    m_status->setWordWrap(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_appeal, &QPushButton::clicked, this, &MySanctionsDialog::appealSelected);
    connect(&m_connection, &client::Connection::responseReceived, this, &MySanctionsDialog::showResult);
    connect(&m_connection, &client::Connection::requestFinished, this,
            [this](quint64 id, const std::optional<protocol::Error> &error, std::optional<quint32>) {
                requestFinished(id, error);
            });
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(buddyLabel(tr("&Sanctions on you:"), m_sanctions));
    layout->addWidget(m_sanctions, 2);
    layout->addWidget(m_appeal);
    layout->addWidget(buddyLabel(tr("Your a&ppeals:"), m_appeals));
    layout->addWidget(m_appeals, 1);
    layout->addWidget(m_status);
    layout->addWidget(buttons);
    refresh();
    m_sanctions->setFocus();
}

void MySanctionsDialog::refresh()
{
    m_sanctionsRequest = m_connection.sendRequest(protocol::ListRequest{0, protocol::ListKind::Sanctions, true});
    m_appealsRequest = m_connection.sendRequest(protocol::ListRequest{0, protocol::ListKind::Appeals, true});
}

void MySanctionsDialog::showResult(quint64 requestId, const protocol::Message &response)
{
    const auto *list = std::get_if<protocol::ListResult>(&response);
    if (list == nullptr) {
        return;
    }
    if (requestId == m_sanctionsRequest) {
        m_sanctions->clear();
        for (const protocol::SanctionInfo &s : list->sanctions) {
            auto *item = new QTreeWidgetItem(m_sanctions, {describeSanction(s),
                                                           s.reason.isEmpty() ? tr("(no reason given)") : s.reason,
                                                           s.actorName, sanctionStateName(s.state)});
            item->setData(0, Qt::UserRole, QVariant::fromValue(s.id));
        }
        if (m_sanctions->topLevelItemCount() == 0) {
            new QTreeWidgetItem(m_sanctions, {tr("No sanctions")});
        }
        m_sanctions->setCurrentItem(m_sanctions->topLevelItem(0));
    } else if (requestId == m_appealsRequest) {
        m_appeals->clear();
        for (const protocol::AppealInfo &a : list->appeals) {
            new QTreeWidgetItem(m_appeals, {a.text, appealStateName(a.state), a.decidedBy, a.decisionReason});
        }
        if (m_appeals->topLevelItemCount() == 0) {
            new QTreeWidgetItem(m_appeals, {tr("No appeals")});
        }
    }
}

void MySanctionsDialog::appealSelected()
{
    const QTreeWidgetItem *item = m_sanctions->currentItem();
    const quint64 id = item != nullptr ? item->data(0, Qt::UserRole).toULongLong() : 0;
    if (id == 0) {
        m_status->setText(tr("Select a sanction first."));
        return;
    }
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(this, tr("Appeal"),
                                                        tr("Why should this sanction be reconsidered? Someone other "
                                                           "than the moderator who issued it will decide."),
                                                        QString(), &ok)
                             .trimmed();
    if (!ok || text.isEmpty()) {
        return;
    }
    if (!protocol::isValidChatText(text)) {
        m_status->setText(tr("The appeal contains characters that cannot be sent."));
        return;
    }
    m_submitRequest = m_connection.sendRequest(protocol::SubmitAppeal{0, id, text});
}

void MySanctionsDialog::requestFinished(quint64 requestId, const std::optional<protocol::Error> &error)
{
    if (requestId != m_submitRequest) {
        return;
    }
    m_status->setText(error ? tr("The appeal was not sent: %1.").arg(error->detail)
                            : tr("Your appeal was sent. You will be told the decision."));
    refresh();
}

} // namespace opentalk::ui
