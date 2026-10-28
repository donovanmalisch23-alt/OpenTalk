// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/ui/AdminDialog.h"

#include "RequestTracker.h"
#include "UiHelpers.h"

#include "opentalk/client/ClientState.h"
#include "opentalk/client/Connection.h"
#include "opentalk/moderation/Appeals.h"
#include "opentalk/moderation/Permissions.h"
#include "opentalk/ui/ModerationDialogs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace opentalk::ui {

using namespace detail;
using moderation::Capability;
using protocol::Message;

namespace {

constexpr int kIdRole = Qt::UserRole;

/// A list with columns. Screen readers read the first cell of a row, so addRow() puts
/// the whole row into that cell's accessible text.
QTreeWidget *makeList(const QString &name, const QStringList &headers)
{
    auto *list = new QTreeWidget;
    list->setAccessibleName(name);
    list->setRootIsDecorated(false);
    list->setUniformRowHeights(true);
    list->setAllColumnsShowFocus(true);
    list->setHeaderLabels(headers);
    list->header()->setStretchLastSection(true);
    return list;
}

QTreeWidgetItem *addRow(QTreeWidget *list, const QStringList &cells, const QVariant &id)
{
    auto *item = new QTreeWidgetItem(list, cells);
    item->setData(0, kIdRole, id);
    QStringList spoken;
    for (int i = 0; i < cells.size(); ++i) {
        if (cells[i].isEmpty()) {
            continue;
        }
        spoken << (i == 0 ? cells[i] : QObject::tr("%1: %2").arg(list->headerItem()->text(i), cells[i]));
    }
    item->setData(0, Qt::AccessibleTextRole, spoken.join(QStringLiteral(", ")));
    return item;
}

/// A row saying the list is empty; it has no ID, so actions ignore it.
void addEmptyRow(QTreeWidget *list, const QString &text)
{
    auto *item = new QTreeWidgetItem(list, {text});
    item->setData(0, kIdRole, QVariant());
    item->setData(0, Qt::AccessibleTextRole, text);
}

QVariant selectedId(const QTreeWidget *list)
{
    const QTreeWidgetItem *item = list->currentItem();
    return item != nullptr ? item->data(0, kIdRole) : QVariant();
}

void selectId(QTreeWidget *list, const QVariant &id)
{
    for (int i = 0; i < list->topLevelItemCount(); ++i) {
        if (id.isValid() && list->topLevelItem(i)->data(0, kIdRole) == id) {
            list->setCurrentItem(list->topLevelItem(i));
            return;
        }
    }
    if (list->topLevelItemCount() > 0) {
        list->setCurrentItem(list->topLevelItem(0));
    }
}

QPushButton *button(const QString &text, QWidget *parent)
{
    auto *b = new QPushButton(text, parent);
    b->setAutoDefault(false);
    return b;
}

QString orNone(const QString &text)
{
    return text.isEmpty() ? QObject::tr("(none)") : text;
}

/// The roles `state`'s user may give to others: below their own rank, never guest or owner.
QList<QPair<quint32, QString>> assignableRoles(const client::ClientState &state)
{
    QList<QPair<quint32, QString>> result;
    const int ownRank = state.roleRank(static_cast<quint32>(state.selfRole()));
    const bool owner = state.selfRole() == protocol::Role::Owner;
    QList<protocol::RoleInfo> roles = state.roles();
    std::sort(roles.begin(), roles.end(),
              [](const protocol::RoleInfo &a, const protocol::RoleInfo &b) { return a.rank > b.rank; });
    for (const protocol::RoleInfo &role : roles) {
        if (role.id == moderation::roles::kGuest || role.id == moderation::roles::kOwner) {
            continue;
        }
        if (owner || role.rank < ownRank) {
            result.append({role.id, role.name});
        }
    }
    return result;
}

QList<QPair<quint32, QString>> channelChoices(const client::Connection &connection)
{
    const client::ClientState &state = connection.serverState();
    const QString root = connection.serverInfo().serverName;
    QList<QPair<quint32, QString>> result;
    for (const protocol::ChannelInfo &channel : state.channels()) {
        result.append({channel.id, state.channelPath(channel.id, root)});
    }
    return result;
}

} // namespace

// ------------------------------------------------------------------- page base

/// One tab of the administration dialog.
class AdminPage : public QWidget {
    Q_OBJECT

public:
    struct Context {
        client::Connection *connection = nullptr;
        RequestTracker *tracker = nullptr;
        std::function<void(const QString &)> status;
    };

    AdminPage(const Context &context, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_context(context)
    {
    }

    virtual void refresh() = 0;

protected:
    client::Connection &connection() const { return *m_context.connection; }
    const client::ClientState &state() const { return m_context.connection->serverState(); }
    void status(const QString &text) const { m_context.status(text); }

    void send(Message request, RequestTracker::Handler handler)
    {
        m_context.tracker->send(std::move(request), std::move(handler));
    }

    /// Sends a change, reports the outcome and reloads the page.
    void change(Message request, const QString &done, const QString &failed)
    {
        send(std::move(request), [this, done, failed](const RequestResult &result) {
            if (result.ok()) {
                status(done);
                refresh();
            } else {
                status(tr("%1: %2.").arg(failed, result.error->detail));
            }
        });
    }

    /// Loads a list; `fill` is called with it.
    void load(protocol::ListKind kind, bool includeClosed, std::function<void(const protocol::ListResult &)> fill)
    {
        send(protocol::ListRequest{0, kind, includeClosed}, [this, fill](const RequestResult &result) {
            if (!result.ok()) {
                status(tr("Could not load the list: %1.").arg(result.error->detail));
                return;
            }
            if (const auto *list = result.data ? std::get_if<protocol::ListResult>(&*result.data) : nullptr) {
                fill(*list);
            }
        });
    }

    QString channelName(quint32 channelId) const
    {
        return channelId == 0 ? tr("the whole server")
                              : state().channelPath(channelId, connection().serverInfo().serverName);
    }

    /// Opens the sanction dialog for an account (not necessarily online) and sends it.
    void sanctionAccount(const QString &account)
    {
        SanctionDialog::Options options;
        options.targetName = account;
        options.canBanAddress = false; // an offline account has no address to block
        options.canBanPermanently = state().can(Capability::UserBanPermanent);
        SanctionDialog dialog(options, this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        protocol::ApplySanction request = dialog.request();
        request.account = account;
        change(request, tr("Done: %1 for %2.").arg(sanctionTypeName(request.type), account),
               tr("Could not sanction %1").arg(account));
    }

private:
    Context m_context;
};

// ------------------------------------------------------------------- sanctions

class SanctionsPage : public AdminPage {
    Q_OBJECT

public:
    explicit SanctionsPage(const Context &context)
        : AdminPage(context)
    {
        m_showEnded = new QCheckBox(tr("Show &ended and lifted sanctions"), this);
        m_list =
            makeList(tr("Sanctions"), {tr("Person"), tr("Sanction"), tr("Reason"), tr("By"), tr("Given"), tr("State")});
        m_lift = button(tr("&Lift sanction..."), this);
        m_sanction = button(tr("Sanction an &account..."), this);
        m_lift->setVisible(state().canSomewhere(Capability::SanctionRevoke));
        auto *buttons = new QHBoxLayout;
        buttons->addWidget(m_lift);
        buttons->addWidget(m_sanction);
        buttons->addStretch();
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(m_showEnded);
        layout->addWidget(buddyLabel(tr("&Sanctions:"), m_list));
        layout->addWidget(m_list, 1);
        layout->addLayout(buttons);
        connect(m_showEnded, &QCheckBox::toggled, this, &SanctionsPage::refresh);
        connect(m_lift, &QPushButton::clicked, this, &SanctionsPage::liftSelected);
        connect(m_sanction, &QPushButton::clicked, this, [this] {
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("Sanction an account"), tr("Account name:"),
                                                       QLineEdit::Normal, QString(), &ok)
                                     .trimmed();
            if (ok && !name.isEmpty()) {
                sanctionAccount(name);
            }
        });
        connect(m_list, &QTreeWidget::currentItemChanged, this, &SanctionsPage::updateButtons);
        updateButtons();
    }

    void refresh() override
    {
        load(protocol::ListKind::Sanctions, m_showEnded->isChecked(), [this](const protocol::ListResult &result) {
            const QVariant selected = selectedId(m_list);
            m_list->clear();
            m_sanctions.clear();
            for (const protocol::SanctionInfo &s : result.sanctions) {
                m_sanctions.insert(s.id, s);
                QString person = s.targetName;
                if (!s.targetAccount.isEmpty() && s.targetAccount.compare(s.targetName, Qt::CaseInsensitive) != 0) {
                    person += tr(" (account %1)").arg(s.targetAccount);
                } else if (s.targetAccount.isEmpty()) {
                    person += tr(" (guest)");
                }
                if (!s.targetAddress.isEmpty()) {
                    person += tr(", address %1").arg(s.targetAddress);
                }
                QString state = sanctionStateName(s.state);
                if (!s.resolvedBy.isEmpty()) {
                    state += tr(" by %1").arg(s.resolvedBy);
                }
                addRow(m_list,
                       {person, describeSanction(s, s.channelId != 0 ? channelName(s.channelId) : QString()),
                        orNone(s.reason), s.actorName, localTime(s.createdMs, QString()), state},
                       QVariant::fromValue(s.id));
            }
            if (result.sanctions.isEmpty()) {
                addEmptyRow(m_list, m_showEnded->isChecked() ? tr("No sanctions") : tr("No active sanctions"));
            }
            selectId(m_list, selected);
            updateButtons();
        });
    }

    QTreeWidget *list() const { return m_list; }

private:
    void updateButtons()
    {
        const QVariant id = selectedId(m_list);
        const auto it = m_sanctions.constFind(id.toULongLong());
        m_lift->setEnabled(id.isValid() && it != m_sanctions.cend() &&
                           it->state == static_cast<quint8>(moderation::SanctionState::Active));
    }

    void liftSelected()
    {
        const auto it = m_sanctions.constFind(selectedId(m_list).toULongLong());
        if (it == m_sanctions.cend()) {
            return;
        }
        const protocol::SanctionInfo s = *it;
        bool ok = false;
        const QString reason =
            QInputDialog::getText(
                this, tr("Lift sanction"),
                tr("Why lift the %1 on %2? (optional)").arg(sanctionTypeName(s.type).toLower(), s.targetName),
                QLineEdit::Normal, QString(), &ok)
                .trimmed();
        if (!ok) {
            return;
        }
        change(protocol::RevokeSanction{0, s.id, reason}, tr("The sanction on %1 was lifted.").arg(s.targetName),
               tr("Could not lift the sanction"));
    }

    QCheckBox *m_showEnded = nullptr;
    QTreeWidget *m_list = nullptr;
    QPushButton *m_lift = nullptr;
    QPushButton *m_sanction = nullptr;
    QHash<quint64, protocol::SanctionInfo> m_sanctions;
};

// --------------------------------------------------------------------- reports

class ReportsPage : public AdminPage {
    Q_OBJECT

public:
    explicit ReportsPage(const Context &context)
        : AdminPage(context)
    {
        m_showHandled = new QCheckBox(tr("Show &handled reports"), this);
        m_list =
            makeList(tr("Reports"), {tr("About"), tr("What happened"), tr("Reported by"), tr("When"), tr("State")});
        m_handled = button(tr("Mark as &handled..."), this);
        m_dismiss = button(tr("&Dismiss..."), this);
        m_sanction = button(tr("&Sanction the reported account..."), this);
        auto *buttons = new QHBoxLayout;
        buttons->addWidget(m_handled);
        buttons->addWidget(m_dismiss);
        buttons->addWidget(m_sanction);
        buttons->addStretch();
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(m_showHandled);
        layout->addWidget(buddyLabel(tr("&Reports:"), m_list));
        layout->addWidget(m_list, 1);
        layout->addLayout(buttons);
        connect(m_showHandled, &QCheckBox::toggled, this, &ReportsPage::refresh);
        connect(m_handled, &QPushButton::clicked, this,
                [this] { resolveSelected(moderation::Report::State::ActionTaken); });
        connect(m_dismiss, &QPushButton::clicked, this,
                [this] { resolveSelected(moderation::Report::State::Dismissed); });
        connect(m_sanction, &QPushButton::clicked, this, [this] {
            const auto it = m_reports.constFind(selectedId(m_list).toULongLong());
            if (it != m_reports.cend() && !it->targetAccount.isEmpty()) {
                sanctionAccount(it->targetAccount);
            }
        });
        connect(m_list, &QTreeWidget::currentItemChanged, this, &ReportsPage::updateButtons);
        updateButtons();
    }

    void refresh() override
    {
        load(protocol::ListKind::Reports, m_showHandled->isChecked(), [this](const protocol::ListResult &result) {
            const QVariant selected = selectedId(m_list);
            m_list->clear();
            m_reports.clear();
            for (const protocol::ReportInfo &r : result.reports) {
                m_reports.insert(r.id, r);
                QString state = reportStateName(r.state);
                if (!r.handledBy.isEmpty()) {
                    state += tr(" by %1").arg(r.handledBy);
                }
                if (!r.resolution.isEmpty()) {
                    state += tr(": %1").arg(r.resolution);
                }
                addRow(m_list, {r.targetName, r.reason, r.reporterName, localTime(r.createdMs, QString()), state},
                       QVariant::fromValue(r.id));
            }
            if (result.reports.isEmpty()) {
                addEmptyRow(m_list, m_showHandled->isChecked() ? tr("No reports") : tr("No open reports"));
            }
            selectId(m_list, selected);
            updateButtons();
        });
    }

private:
    void updateButtons()
    {
        const auto it = m_reports.constFind(selectedId(m_list).toULongLong());
        const bool open = it != m_reports.cend() && it->state == static_cast<quint8>(moderation::Report::State::Open);
        m_handled->setEnabled(open);
        m_dismiss->setEnabled(open);
        m_sanction->setEnabled(it != m_reports.cend() && !it->targetAccount.isEmpty());
    }

    void resolveSelected(moderation::Report::State outcome)
    {
        const auto it = m_reports.constFind(selectedId(m_list).toULongLong());
        if (it == m_reports.cend()) {
            return;
        }
        const bool dismiss = outcome == moderation::Report::State::Dismissed;
        bool ok = false;
        const QString note =
            QInputDialog::getText(this, dismiss ? tr("Dismiss report") : tr("Mark as handled"),
                                  dismiss ? tr("Why is no action needed? (optional)") : tr("What was done? (optional)"),
                                  QLineEdit::Normal, QString(), &ok)
                .trimmed();
        if (!ok) {
            return;
        }
        change(protocol::ResolveReport{0, it->id, static_cast<quint8>(outcome), note},
               dismiss ? tr("The report was dismissed.") : tr("The report was marked as handled."),
               tr("Could not update the report"));
    }

    QCheckBox *m_showHandled = nullptr;
    QTreeWidget *m_list = nullptr;
    QPushButton *m_handled = nullptr;
    QPushButton *m_dismiss = nullptr;
    QPushButton *m_sanction = nullptr;
    QHash<quint64, protocol::ReportInfo> m_reports;
};

// --------------------------------------------------------------------- appeals

class AppealsPage : public AdminPage {
    Q_OBJECT

public:
    explicit AppealsPage(const Context &context)
        : AdminPage(context)
    {
        m_showDecided = new QCheckBox(tr("Show &decided appeals"), this);
        m_list = makeList(tr("Appeals"), {tr("From"), tr("Appeal"), tr("Sanction"), tr("When"), tr("State")});
        m_decide = button(tr("&Decide..."), this);
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(m_showDecided);
        layout->addWidget(buddyLabel(tr("&Appeals:"), m_list));
        layout->addWidget(m_list, 1);
        layout->addWidget(m_decide, 0, Qt::AlignLeft);
        layout->addWidget(helpLabel(tr("The moderator who gave a sanction cannot decide its appeal while someone "
                                       "else can."),
                                    m_decide));
        connect(m_showDecided, &QCheckBox::toggled, this, &AppealsPage::refresh);
        connect(m_decide, &QPushButton::clicked, this, &AppealsPage::decideSelected);
        connect(m_list, &QTreeWidget::currentItemChanged, this, &AppealsPage::updateButtons);
        updateButtons();
    }

    void refresh() override
    {
        // The sanctions give each appeal its context.
        load(protocol::ListKind::Sanctions, true, [this](const protocol::ListResult &sanctions) {
            m_sanctions.clear();
            for (const protocol::SanctionInfo &s : sanctions.sanctions) {
                m_sanctions.insert(s.id, s);
            }
            load(protocol::ListKind::Appeals, m_showDecided->isChecked(),
                 [this](const protocol::ListResult &result) { fill(result.appeals); });
        });
    }

private:
    void fill(const QList<protocol::AppealInfo> &appeals)
    {
        const QVariant selected = selectedId(m_list);
        m_list->clear();
        m_appeals.clear();
        for (const protocol::AppealInfo &a : appeals) {
            m_appeals.insert(a.id, a);
            QString state = appealStateName(a.state);
            if (!a.decidedBy.isEmpty()) {
                state += tr(" by %1").arg(a.decidedBy);
            }
            if (!a.decisionReason.isEmpty()) {
                state += tr(": %1").arg(a.decisionReason);
            }
            addRow(m_list,
                   {a.appellantName, a.text, sanctionSummary(a.sanctionId), localTime(a.createdMs, QString()), state},
                   QVariant::fromValue(a.id));
        }
        if (appeals.isEmpty()) {
            addEmptyRow(m_list, m_showDecided->isChecked() ? tr("No appeals") : tr("No appeals waiting"));
        }
        selectId(m_list, selected);
        updateButtons();
    }

    QString sanctionSummary(quint64 sanctionId) const
    {
        const auto it = m_sanctions.constFind(sanctionId);
        if (it == m_sanctions.cend()) {
            return tr("sanction %1").arg(sanctionId);
        }
        QString text = tr("%1 by %2").arg(describeSanction(*it, channelName(it->channelId)), it->actorName);
        if (!it->reason.isEmpty()) {
            text += tr(", reason: %1").arg(it->reason);
        }
        return text;
    }

    void updateButtons()
    {
        const auto it = m_appeals.constFind(selectedId(m_list).toULongLong());
        m_decide->setEnabled(it != m_appeals.cend() &&
                             it->state == static_cast<quint8>(moderation::Appeal::State::Open));
    }

    void decideSelected()
    {
        const auto it = m_appeals.constFind(selectedId(m_list).toULongLong());
        if (it == m_appeals.cend()) {
            return;
        }
        const auto sanction = m_sanctions.constFind(it->sanctionId);
        const bool lasting =
            sanction != m_sanctions.cend() &&
            static_cast<moderation::SanctionType>(sanction->type) != moderation::SanctionType::Warning &&
            static_cast<moderation::SanctionType>(sanction->type) != moderation::SanctionType::Kick;
        AppealDecisionDialog dialog(
            tr("%1 appeals: %2\n\nSanction: %3").arg(it->appellantName, it->text, sanctionSummary(it->sanctionId)),
            lasting, this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        protocol::DecideAppeal request = dialog.request();
        request.appealId = it->id;
        change(request, tr("The appeal from %1 was decided. They are told the outcome.").arg(it->appellantName),
               tr("Could not decide the appeal"));
    }

    QCheckBox *m_showDecided = nullptr;
    QTreeWidget *m_list = nullptr;
    QPushButton *m_decide = nullptr;
    QHash<quint64, protocol::AppealInfo> m_appeals;
    QHash<quint64, protocol::SanctionInfo> m_sanctions;
};

// -------------------------------------------------------------------- accounts

class AccountsPage : public AdminPage {
    Q_OBJECT

public:
    explicit AccountsPage(const Context &context)
        : AdminPage(context)
    {
        m_list = makeList(tr("Accounts"), {tr("Name"), tr("Role"), tr("State")});
        m_create = button(tr("&New account..."), this);
        m_role = button(tr("Change &role..."), this);
        m_password = button(tr("Reset &password..."), this);
        m_delete = button(tr("&Delete account..."), this);
        m_transfer = button(tr("&Transfer ownership..."), this);
        const bool manage = state().can(Capability::AccountManage);
        m_create->setVisible(manage);
        m_password->setVisible(manage);
        m_delete->setVisible(manage);
        m_role->setVisible(state().can(Capability::RoleAssign));
        m_transfer->setVisible(state().can(Capability::OwnerTransfer));
        auto *buttons = new QHBoxLayout;
        for (QPushButton *b : {m_create, m_role, m_password, m_delete, m_transfer}) {
            buttons->addWidget(b);
        }
        buttons->addStretch();
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(buddyLabel(tr("A&ccounts:"), m_list));
        layout->addWidget(m_list, 1);
        layout->addLayout(buttons);
        connect(m_create, &QPushButton::clicked, this, &AccountsPage::createAccount);
        connect(m_role, &QPushButton::clicked, this, &AccountsPage::changeRole);
        connect(m_password, &QPushButton::clicked, this, &AccountsPage::resetPassword);
        connect(m_delete, &QPushButton::clicked, this, &AccountsPage::deleteAccount);
        connect(m_transfer, &QPushButton::clicked, this, &AccountsPage::transferOwnership);
        connect(m_list, &QTreeWidget::currentItemChanged, this, &AccountsPage::updateButtons);
        updateButtons();
    }

    void refresh() override
    {
        load(protocol::ListKind::Accounts, true, [this](const protocol::ListResult &result) {
            const QVariant selected = selectedId(m_list);
            m_list->clear();
            m_accounts.clear();
            QList<protocol::AccountInfo> accounts = result.accounts;
            std::sort(accounts.begin(), accounts.end(),
                      [](const auto &a, const auto &b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
            for (const protocol::AccountInfo &a : accounts) {
                m_accounts.insert(a.name, a);
                addRow(m_list, {a.name, state().roleName(a.roleId), a.disabled ? tr("disabled") : tr("active")},
                       a.name);
            }
            if (accounts.isEmpty()) {
                addEmptyRow(m_list, tr("No accounts"));
            }
            selectId(m_list, selected);
            updateButtons();
        });
    }

private:
    std::optional<protocol::AccountInfo> selected() const
    {
        const auto it = m_accounts.constFind(selectedId(m_list).toString());
        return it != m_accounts.cend() ? std::optional<protocol::AccountInfo>(*it) : std::nullopt;
    }

    void updateButtons()
    {
        const auto account = selected();
        const bool other = account && account->name.compare(state().selfName(), Qt::CaseInsensitive) != 0;
        m_role->setEnabled(other && account->roleId != moderation::roles::kOwner);
        m_password->setEnabled(account.has_value());
        m_delete->setEnabled(other && account->roleId != moderation::roles::kOwner);
        m_transfer->setEnabled(other);
    }

    void createAccount()
    {
        AccountDialog dialog(assignableRoles(state()), this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        const protocol::CreateAccount request = dialog.request();
        change(request, tr("Account %1 was created.").arg(request.name), tr("Could not create %1").arg(request.name));
    }

    void changeRole()
    {
        const auto account = selected();
        if (!account) {
            return;
        }
        const QList<QPair<quint32, QString>> roles = assignableRoles(state());
        QStringList names;
        int current = 0;
        for (const auto &[id, name] : roles) {
            if (id == account->roleId) {
                current = static_cast<int>(names.size());
            }
            names << name;
        }
        if (names.isEmpty()) {
            status(tr("There are no roles you can give."));
            return;
        }
        bool ok = false;
        const QString choice = QInputDialog::getItem(this, tr("Change role"), tr("New role for %1:").arg(account->name),
                                                     names, current, false, &ok);
        if (!ok) {
            return;
        }
        const quint32 roleId = roles.value(static_cast<int>(names.indexOf(choice))).first;
        change(protocol::SetUserRole{0, account->name, roleId}, tr("%1 is now %2.").arg(account->name, choice),
               tr("Could not change the role of %1").arg(account->name));
    }

    void resetPassword()
    {
        const auto account = selected();
        if (!account) {
            return;
        }
        PasswordDialog dialog(account->name, this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        change(protocol::SetAccountPassword{0, account->name, dialog.password()},
               tr("The password of %1 was changed.").arg(account->name),
               tr("Could not change the password of %1").arg(account->name));
    }

    void deleteAccount()
    {
        const auto account = selected();
        if (!account) {
            return;
        }
        if (QMessageBox::question(this, tr("Delete account"),
                                  tr("Delete the account %1? If they are online they are disconnected. This cannot "
                                     "be undone.")
                                      .arg(account->name)) != QMessageBox::Yes) {
            return;
        }
        change(protocol::DeleteAccount{0, account->name}, tr("Account %1 was deleted.").arg(account->name),
               tr("Could not delete %1").arg(account->name));
    }

    void transferOwnership()
    {
        const auto account = selected();
        if (!account) {
            return;
        }
        if (QMessageBox::warning(this, tr("Transfer ownership"),
                                 tr("Make %1 the owner of this server? You become an administrator, and only the "
                                    "new owner can give ownership back.")
                                     .arg(account->name),
                                 QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
            return;
        }
        change(protocol::TransferOwnership{0, account->name}, tr("%1 now owns this server.").arg(account->name),
               tr("Could not transfer ownership"));
    }

    QTreeWidget *m_list = nullptr;
    QPushButton *m_create = nullptr;
    QPushButton *m_role = nullptr;
    QPushButton *m_password = nullptr;
    QPushButton *m_delete = nullptr;
    QPushButton *m_transfer = nullptr;
    QMap<QString, protocol::AccountInfo> m_accounts;
};

// ----------------------------------------------------------------------- roles

class RolesPage : public AdminPage {
    Q_OBJECT

public:
    explicit RolesPage(const Context &context)
        : AdminPage(context)
    {
        m_list = makeList(tr("Roles"), {tr("Name"), tr("Rank"), tr("Can do")});
        m_create = button(tr("&New role..."), this);
        m_edit = button(tr("&Edit role..."), this);
        m_delete = button(tr("&Delete role..."), this);
        auto *buttons = new QHBoxLayout;
        for (QPushButton *b : {m_create, m_edit, m_delete}) {
            buttons->addWidget(b);
        }
        buttons->addStretch();
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(buddyLabel(tr("&Roles (highest rank first):"), m_list));
        layout->addWidget(m_list, 1);
        layout->addLayout(buttons);
        layout->addWidget(helpLabel(tr("People can only act on, and give roles to, people and roles ranked below "
                                       "their own."),
                                    m_list));
        connect(m_create, &QPushButton::clicked, this, [this] { editRole(protocol::RoleInfo{}); });
        connect(m_edit, &QPushButton::clicked, this, [this] {
            if (const auto role = selected()) {
                editRole(*role);
            }
        });
        connect(m_delete, &QPushButton::clicked, this, &RolesPage::deleteSelected);
        connect(m_list, &QTreeWidget::currentItemChanged, this, &RolesPage::updateButtons);
        connect(&state(), &client::ClientState::rolesChanged, this, &RolesPage::refresh);
    }

    void refresh() override
    {
        const QVariant selectedRole = selectedId(m_list);
        m_list->clear();
        QList<protocol::RoleInfo> roles = state().roles();
        std::sort(roles.begin(), roles.end(), [](const auto &a, const auto &b) { return a.rank > b.rank; });
        for (const protocol::RoleInfo &role : roles) {
            QStringList names;
            for (quint16 value : role.capabilities) {
                if (const auto c = moderation::capabilityFromValue(value)) {
                    names << moderation::capabilityName(*c);
                }
            }
            QString can = role.id == moderation::roles::kOwner ? tr("everything") : names.join(QStringLiteral(", "));
            if (role.builtIn) {
                can = tr("built in; %1").arg(can.isEmpty() ? tr("nothing") : can);
            }
            addRow(m_list, {role.name, QString::number(role.rank), can}, role.id);
        }
        selectId(m_list, selectedRole);
        updateButtons();
    }

private:
    int ownRank() const { return state().roleRank(static_cast<quint32>(state().selfRole())); }

    std::optional<protocol::RoleInfo> selected() const
    {
        const QVariant id = selectedId(m_list);
        for (const protocol::RoleInfo &role : state().roles()) {
            if (id.isValid() && role.id == id.toUInt()) {
                return role;
            }
        }
        return std::nullopt;
    }

    void updateButtons()
    {
        const auto role = selected();
        const bool owner = state().selfRole() == protocol::Role::Owner;
        const bool below = role && role->id != moderation::roles::kOwner && (owner || role->rank < ownRank());
        m_edit->setEnabled(below);
        m_delete->setEnabled(below && !role->builtIn);
    }

    void editRole(const protocol::RoleInfo &role)
    {
        QList<quint16> grantable;
        for (Capability c : moderation::allCapabilities()) {
            if (!moderation::isOwnerOnly(c) && state().can(c)) {
                grantable.append(static_cast<quint16>(c));
            }
        }
        const int maxRank = std::min(ownRank(), moderation::roles::kAdministratorRank) - 1;
        RoleDialog dialog(role, grantable, maxRank, this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        const protocol::RoleInfo changed = dialog.role();
        change(protocol::SaveRole{0, changed},
               role.id == 0 ? tr("Role %1 was created.").arg(changed.name) : tr("Role %1 was saved.").arg(changed.name),
               tr("Could not save role %1").arg(changed.name));
    }

    void deleteSelected()
    {
        const auto role = selected();
        if (!role || QMessageBox::question(this, tr("Delete role"), tr("Delete the role %1?").arg(role->name)) !=
                         QMessageBox::Yes) {
            return;
        }
        change(protocol::DeleteRole{0, role->id}, tr("Role %1 was deleted.").arg(role->name),
               tr("Could not delete role %1").arg(role->name));
    }

    QTreeWidget *m_list = nullptr;
    QPushButton *m_create = nullptr;
    QPushButton *m_edit = nullptr;
    QPushButton *m_delete = nullptr;
};

// ----------------------------------------------------------------------- rules

class RulesPage : public AdminPage {
    Q_OBJECT

public:
    explicit RulesPage(const Context &context)
        : AdminPage(context)
    {
        m_list = makeList(tr("Permission rules"), {tr("Where"), tr("For"), tr("Permission"), tr("Effect")});
        m_add = button(tr("&Add rule..."), this);
        m_delete = button(tr("&Delete rule"), this);
        auto *buttons = new QHBoxLayout;
        buttons->addWidget(m_add);
        buttons->addWidget(m_delete);
        buttons->addStretch();
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(buddyLabel(tr("Permission &rules:"), m_list));
        layout->addWidget(m_list, 1);
        layout->addLayout(buttons);
        layout->addWidget(helpLabel(tr("A rule on a channel also covers its sub-channels. The most specific rule "
                                       "wins: a channel before its parents, an account before its role, and deny "
                                       "before allow."),
                                    m_list));
        connect(m_add, &QPushButton::clicked, this, &RulesPage::addRule);
        connect(m_delete, &QPushButton::clicked, this, &RulesPage::deleteSelected);
        connect(m_list, &QTreeWidget::currentItemChanged, this,
                [this] { m_delete->setEnabled(selectedId(m_list).isValid()); });
        m_delete->setEnabled(false);
    }

    void refresh() override
    {
        load(protocol::ListKind::Rules, true, [this](const protocol::ListResult &result) {
            const QVariant selected = selectedId(m_list);
            m_list->clear();
            for (const protocol::RuleInfo &rule : result.rules) {
                const auto c = moderation::capabilityFromValue(rule.capability);
                addRow(m_list,
                       {channelName(rule.channelId),
                        rule.forAccount ? tr("account %1").arg(rule.account)
                                        : tr("role %1").arg(state().roleName(rule.roleId)),
                        c ? moderation::capabilityName(*c) : QString::number(rule.capability),
                        rule.allow ? tr("allow") : tr("deny")},
                       QVariant::fromValue(rule.id));
            }
            if (result.rules.isEmpty()) {
                addEmptyRow(m_list, tr("No rules: everyone has their roles' permissions everywhere"));
            }
            selectId(m_list, selected);
            m_delete->setEnabled(selectedId(m_list).isValid());
        });
    }

private:
    void addRule()
    {
        QList<QPair<quint32, QString>> roles;
        for (const protocol::RoleInfo &role : state().roles()) {
            if (role.id != moderation::roles::kOwner) {
                roles.append({role.id, role.name});
            }
        }
        RuleDialog dialog(channelChoices(connection()), roles, this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        change(protocol::AddRule{0, dialog.rule()}, tr("The rule was added."), tr("Could not add the rule"));
    }

    void deleteSelected()
    {
        const QVariant id = selectedId(m_list);
        if (!id.isValid()) {
            return;
        }
        change(protocol::DeleteRule{0, id.toULongLong()}, tr("The rule was deleted."), tr("Could not delete the rule"));
    }

    QTreeWidget *m_list = nullptr;
    QPushButton *m_add = nullptr;
    QPushButton *m_delete = nullptr;
};

// ------------------------------------------------------------------- audit log

class AuditPageWidget : public AdminPage {
    Q_OBJECT

public:
    explicit AuditPageWidget(const Context &context)
        : AdminPage(context)
    {
        m_chain = new QLabel(this);
        m_chain->setWordWrap(true);
        m_chain->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
        m_list = makeList(tr("Audit log"), {tr("Entry"), tr("Time"), tr("Who"), tr("Action"), tr("Target"),
                                            tr("Reason"), tr("Details")});
        m_newest = button(tr("&Newest entries"), this);
        m_older = button(tr("&Older entries"), this);
        auto *buttons = new QHBoxLayout;
        buttons->addWidget(m_newest);
        buttons->addWidget(m_older);
        buttons->addStretch();
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(m_chain);
        layout->addWidget(buddyLabel(tr("Audit &log (newest first):"), m_list));
        layout->addWidget(m_list, 1);
        layout->addLayout(buttons);
        connect(m_newest, &QPushButton::clicked, this, [this] { loadPage(0); });
        connect(m_older, &QPushButton::clicked, this, [this] {
            if (m_oldest > 1) {
                loadPage(m_oldest);
            }
        });
        m_older->setEnabled(false);
    }

    void refresh() override { loadPage(0); }

private:
    void loadPage(quint64 before)
    {
        send(protocol::ReadAudit{0, before, 100}, [this, before](const RequestResult &result) {
            if (!result.ok()) {
                status(tr("Could not read the audit log: %1.").arg(result.error->detail));
                return;
            }
            const auto *page = result.data ? std::get_if<protocol::AuditPage>(&*result.data) : nullptr;
            if (page == nullptr) {
                return;
            }
            m_list->clear();
            for (const protocol::AuditEntryInfo &e : page->entries) {
                addRow(m_list,
                       {QString::number(e.sequence), localTime(e.timeMs, QString()),
                        e.actorName.isEmpty() ? tr("the server") : e.actorName, e.action, e.target, e.reason,
                        e.details},
                       QVariant::fromValue(e.sequence));
            }
            if (page->entries.isEmpty()) {
                addEmptyRow(m_list, before == 0 ? tr("The audit log is empty") : tr("No older entries"));
            }
            m_oldest = page->entries.isEmpty() ? before : page->entries.last().sequence;
            m_older->setEnabled(m_oldest > 1 && !page->entries.isEmpty());
            m_chain->setText(page->chainIntact
                                 ? tr("The audit log is intact: no entry was changed or removed outside OpenTalk.")
                                 : tr("Warning: the audit log was changed outside OpenTalk, starting at entry %1. "
                                      "Entries from there on cannot be trusted.")
                                       .arg(page->firstBadSequence));
            if (m_list->topLevelItemCount() > 0) {
                m_list->setCurrentItem(m_list->topLevelItem(0));
            }
            if (!page->chainIntact) {
                status(m_chain->text());
            }
        });
    }

    QLabel *m_chain = nullptr;
    QTreeWidget *m_list = nullptr;
    QPushButton *m_newest = nullptr;
    QPushButton *m_older = nullptr;
    quint64 m_oldest = 0;
};

// -------------------------------------------------------------------- settings

class SettingsPage : public AdminPage {
    Q_OBJECT

public:
    explicit SettingsPage(const Context &context)
        : AdminPage(context)
    {
        m_name = new QLineEdit(this);
        m_name->setMaxLength(static_cast<int>(protocol::kMaxNameLength));
        m_name->setAccessibleName(tr("Server name"));
        m_welcome = new QLineEdit(this);
        m_welcome->setMaxLength(static_cast<int>(protocol::kMaxTextLength));
        m_welcome->setAccessibleName(tr("Welcome message"));
        m_guests = new QCheckBox(tr("Let &guests join without an account"), this);
        m_maxUsers = new QSpinBox(this);
        m_maxUsers->setRange(1, 65535);
        m_maxUsers->setAccessibleName(tr("Most people online at once"));
        m_bitrate = new QSpinBox(this);
        m_bitrate->setRange(6, 510);
        m_bitrate->setSuffix(tr(" kbit/s"));
        m_bitrate->setAccessibleName(tr("Highest voice quality"));
        m_banDays = new QSpinBox(this);
        m_banDays->setRange(1, 3650);
        m_banDays->setSuffix(tr(" days"));
        m_banDays->setAccessibleName(tr("Longest temporary ban"));
        m_contact = new QLineEdit(this);
        m_contact->setMaxLength(256);
        m_contact->setAccessibleName(tr("Contact for banned people"));
        m_save = button(tr("&Save settings"), this);
        auto *form = new QFormLayout;
        form->addRow(buddyLabel(tr("Server &name:"), m_name), m_name);
        form->addRow(buddyLabel(tr("&Welcome message:"), m_welcome), m_welcome);
        form->addRow(QString(), helpLabel(tr("Shown to everyone who logs in. Leave empty for none."), m_welcome));
        form->addRow(QString(), m_guests);
        form->addRow(buddyLabel(tr("Most people &online:"), m_maxUsers), m_maxUsers);
        form->addRow(buddyLabel(tr("Highest &voice quality:"), m_bitrate), m_bitrate);
        form->addRow(buddyLabel(tr("Longest temporary &ban:"), m_banDays), m_banDays);
        form->addRow(buddyLabel(tr("&Contact:"), m_contact), m_contact);
        form->addRow(QString(), helpLabel(tr("Banned people are told this, so they know how to reach you (an email "
                                             "address or web page, for example)."),
                                          m_contact));
        auto *layout = new QVBoxLayout(this);
        layout->addLayout(form);
        layout->addWidget(m_save, 0, Qt::AlignLeft);
        layout->addStretch();
        connect(m_save, &QPushButton::clicked, this, &SettingsPage::save);
        setFieldsEnabled(false);
    }

    void refresh() override
    {
        send(protocol::ServerSettingsRequest{0, std::nullopt}, [this](const RequestResult &result) { show(result); });
    }

    QPushButton *saveButton() const { return m_save; }

private:
    void setFieldsEnabled(bool enabled)
    {
        for (QWidget *w : std::initializer_list<QWidget *>{m_name, m_welcome, m_guests, m_maxUsers, m_bitrate,
                                                           m_banDays, m_contact, m_save}) {
            w->setEnabled(enabled);
        }
    }

    void show(const RequestResult &result)
    {
        if (!result.ok()) {
            status(tr("Could not load the settings: %1.").arg(result.error->detail));
            return;
        }
        const auto *settings = result.data ? std::get_if<protocol::ServerSettingsResult>(&*result.data) : nullptr;
        if (settings == nullptr) {
            return;
        }
        const protocol::ServerSettingsInfo &s = settings->settings;
        m_name->setText(s.name);
        m_welcome->setText(s.welcomeMessage);
        m_guests->setChecked(s.allowGuests);
        m_maxUsers->setValue(s.maxUsers);
        m_bitrate->setValue(static_cast<int>(s.maxVoiceBitrate / 1000));
        m_banDays->setValue(s.maxTemporaryBanDays);
        m_contact->setText(s.contact);
        setFieldsEnabled(true);
    }

    void save()
    {
        if (m_name->text().trimmed().isEmpty()) {
            complain(this, m_name, tr("Enter a name for the server."));
            return;
        }
        protocol::ServerSettingsInfo s;
        s.name = m_name->text().trimmed();
        s.welcomeMessage = m_welcome->text().trimmed();
        s.allowGuests = m_guests->isChecked();
        s.maxUsers = static_cast<quint16>(m_maxUsers->value());
        s.maxVoiceBitrate = static_cast<quint32>(m_bitrate->value()) * 1000;
        s.maxTemporaryBanDays = static_cast<quint16>(m_banDays->value());
        s.contact = m_contact->text().trimmed();
        send(protocol::ServerSettingsRequest{0, s}, [this](const RequestResult &result) {
            if (result.ok()) {
                status(tr("The server settings were saved."));
            } else {
                status(tr("Could not save the settings: %1.").arg(result.error->detail));
            }
            show(result);
        });
    }

    QLineEdit *m_name = nullptr;
    QLineEdit *m_welcome = nullptr;
    QCheckBox *m_guests = nullptr;
    QSpinBox *m_maxUsers = nullptr;
    QSpinBox *m_bitrate = nullptr;
    QSpinBox *m_banDays = nullptr;
    QLineEdit *m_contact = nullptr;
    QPushButton *m_save = nullptr;
};

// ------------------------------------------------------------------ statistics

class StatisticsPage : public AdminPage {
    Q_OBJECT

public:
    explicit StatisticsPage(const Context &context)
        : AdminPage(context)
    {
        m_list = new QListWidget(this);
        m_list->setAccessibleName(tr("Server statistics"));
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(buddyLabel(tr("Server s&tatistics:"), m_list));
        layout->addWidget(m_list, 1);
    }

    void refresh() override
    {
        send(protocol::GetServerStats{0}, [this](const RequestResult &result) {
            if (!result.ok()) {
                status(tr("Could not load the statistics: %1.").arg(result.error->detail));
                return;
            }
            const auto *stats = result.data ? std::get_if<protocol::ServerStatsResult>(&*result.data) : nullptr;
            if (stats == nullptr) {
                return;
            }
            const protocol::ServerStatsInfo &s = stats->stats;
            const int row = std::max(0, m_list->currentRow());
            m_list->clear();
            const qint64 days = s.uptimeSeconds / 86400;
            const QString uptime = tr("%1 days, %2 hours, %3 minutes")
                                       .arg(days)
                                       .arg((s.uptimeSeconds / 3600) % 24)
                                       .arg((s.uptimeSeconds / 60) % 60);
            m_list->addItems({
                tr("Running for: %1").arg(uptime),
                tr("People online: %1").arg(s.onlineUsers),
                tr("Connections: %1").arg(s.connections),
                tr("Channels: %1").arg(s.channels),
                tr("Voice packets received: %1").arg(s.voicePacketsIn),
                tr("Voice packets sent: %1").arg(s.voicePacketsOut),
                tr("Voice packets refused: %1").arg(s.voicePacketsRejected),
                tr("Active sanctions: %1").arg(s.activeSanctions),
                tr("Open reports: %1").arg(s.openReports),
                tr("Appeals waiting: %1").arg(s.openAppeals),
                tr("Audit log entries: %1").arg(s.auditEntries),
                s.auditIntact ? tr("Audit log: intact") : tr("Audit log: CHANGED OUTSIDE OPENTALK"),
            });
            m_list->setCurrentRow(std::min(row, m_list->count() - 1));
        });
    }

private:
    QListWidget *m_list = nullptr;
};

// ---------------------------------------------------------------- AdminDialog

AdminDialog::AdminDialog(client::Connection &connection, QWidget *parent)
    : QDialog(parent)
    , m_connection(connection)
{
    setWindowTitle(tr("Administration - %1").arg(connection.serverInfo().serverName));
    resize(860, 560);
    m_tracker = new RequestTracker(connection, this);
    m_tabs = new QTabWidget(this);
    m_tabs->setAccessibleName(tr("Administration areas"));
    m_tabs->tabBar()->setAccessibleName(tr("Administration areas"));
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setAccessibleName(tr("Result"));
    m_status->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);

    const AdminPage::Context context{&connection, m_tracker, [this](const QString &text) { setStatus(text); }};
    const client::ClientState &state = connection.serverState();
    using C = Capability;
    const auto anyOf = [&state](std::initializer_list<C> caps) {
        return std::any_of(caps.begin(), caps.end(), [&state](C c) { return state.canSomewhere(c); });
    };
    const auto add = [this](Page page, AdminPage *widget, const QString &title) {
        m_pages.insert(page, widget);
        m_order.append(page);
        m_tabs->addTab(widget, title);
    };
    if (anyOf({C::UserWarn, C::UserMute, C::UserKick, C::UserBanTemporary, C::UserBanPermanent, C::SanctionRevoke})) {
        add(Page::Sanctions, new SanctionsPage(context), tr("Sanctions"));
    }
    if (state.can(C::ReportReview)) {
        add(Page::Reports, new ReportsPage(context), tr("Reports"));
    }
    if (state.can(C::AppealDecide)) {
        add(Page::Appeals, new AppealsPage(context), tr("Appeals"));
    }
    if (state.can(C::AccountManage) || state.can(C::RoleAssign)) {
        add(Page::Accounts, new AccountsPage(context), tr("Accounts"));
    }
    if (state.can(C::RoleManage)) {
        add(Page::Roles, new RolesPage(context), tr("Roles"));
        add(Page::Rules, new RulesPage(context), tr("Channel permissions"));
    }
    if (state.can(C::AuditRead)) {
        add(Page::Audit, new AuditPageWidget(context), tr("Audit log"));
    }
    if (state.can(C::ServerConfigure)) {
        add(Page::Settings, new SettingsPage(context), tr("Server settings"));
    }
    if (state.can(C::ServerMonitor)) {
        add(Page::Statistics, new StatisticsPage(context), tr("Statistics"));
    }
    if (m_order.isEmpty()) {
        auto *nothing = new QLabel(tr("You have no moderation or administration permissions on this server."));
        nothing->setWordWrap(true);
        m_tabs->addTab(nothing, tr("Nothing to do"));
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton *refresh = buttons->addButton(tr("Re&load"), QDialogButtonBox::ActionRole);
    refresh->setAutoDefault(false);
    refresh->setToolTip(tr("Reload this tab (F5)"));
    connect(refresh, &QPushButton::clicked, this, &AdminDialog::refreshCurrentPage);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *f5 = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(f5, &QShortcut::activated, this, &AdminDialog::refreshCurrentPage);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs, 1);
    layout->addWidget(m_status);
    layout->addWidget(buttons);
    connect(m_tabs, &QTabWidget::currentChanged, this, &AdminDialog::pageShown);
    // The dialog closes by itself when the connection does: its data would be stale.
    connect(&connection, &client::Connection::stateChanged, this, [this](client::ConnectionState s) {
        if (s != client::ConnectionState::Connected) {
            reject();
        }
    });
    pageShown(m_tabs->currentIndex());
    m_tabs->setFocus();
}

AdminDialog::~AdminDialog() = default;

QList<AdminDialog::Page> AdminDialog::pages() const
{
    return m_order;
}

bool AdminDialog::showPage(Page page)
{
    AdminPage *widget = m_pages.value(page);
    if (widget == nullptr) {
        return false;
    }
    m_tabs->setCurrentWidget(widget);
    return true;
}

QWidget *AdminDialog::pageWidget(Page page) const
{
    return m_pages.value(page);
}

QString AdminDialog::statusText() const
{
    return m_status->text();
}

int AdminDialog::pendingRequests() const
{
    return m_tracker->pendingCount();
}

void AdminDialog::setStatus(const QString &text)
{
    m_status->setText(text);
    emit statusChanged(text);
}

void AdminDialog::pageShown(int index)
{
    auto *page = qobject_cast<AdminPage *>(m_tabs->widget(index));
    if (page != nullptr && !m_loaded.contains(page)) {
        m_loaded.append(page);
        page->refresh();
    }
}

void AdminDialog::refreshCurrentPage()
{
    if (auto *page = qobject_cast<AdminPage *>(m_tabs->currentWidget())) {
        page->refresh();
    }
}

// --------------------------------------------------------------- AccountDialog

AccountDialog::AccountDialog(const QList<QPair<quint32, QString>> &roles, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("New account"));
    m_name = new QLineEdit;
    m_name->setMaxLength(static_cast<int>(protocol::kMaxDisplayNameLength));
    m_name->setAccessibleName(tr("Account name"));
    m_password = new QLineEdit;
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setAccessibleName(tr("Password"));
    m_confirm = new QLineEdit;
    m_confirm->setEchoMode(QLineEdit::Password);
    m_confirm->setAccessibleName(tr("Password again"));
    m_role = new QComboBox;
    m_role->setAccessibleName(tr("Role"));
    for (const auto &[id, name] : roles) {
        m_role->addItem(name, id);
    }
    const int user = m_role->findData(static_cast<quint32>(moderation::roles::kUser));
    m_role->setCurrentIndex(user >= 0 ? user : 0);
    auto *form = new QFormLayout;
    form->addRow(buddyLabel(tr("&Name:"), m_name), m_name);
    form->addRow(QString(), helpLabel(tr("Letters, digits, dots, dashes and underscores; up to 32."), m_name));
    form->addRow(buddyLabel(tr("&Password:"), m_password), m_password);
    form->addRow(buddyLabel(tr("Password &again:"), m_confirm), m_confirm);
    form->addRow(buddyLabel(tr("&Role:"), m_role), m_role);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    buttons->addButton(tr("&Create"), QDialogButtonBox::AcceptRole)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &AccountDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    m_name->setFocus();
}

protocol::CreateAccount AccountDialog::request() const
{
    return protocol::CreateAccount{0, m_name->text().trimmed(), m_password->text(), m_role->currentData().toUInt()};
}

void AccountDialog::accept()
{
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9._-]{1,32}$"));
    if (!valid.match(m_name->text().trimmed()).hasMatch()) {
        complain(this, m_name, tr("Use 1 to 32 letters, digits, dots, dashes or underscores."));
        return;
    }
    if (m_password->text().isEmpty()) {
        complain(this, m_password, tr("Enter a password."));
        return;
    }
    if (m_password->text() != m_confirm->text()) {
        complain(this, m_confirm, tr("The passwords are different."));
        return;
    }
    if (m_role->count() == 0) {
        complain(this, m_role, tr("There is no role you can give."));
        return;
    }
    QDialog::accept();
}

// -------------------------------------------------------------- PasswordDialog

PasswordDialog::PasswordDialog(const QString &accountName, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("New password for %1").arg(accountName));
    m_password = new QLineEdit;
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setAccessibleName(tr("New password"));
    m_confirm = new QLineEdit;
    m_confirm->setEchoMode(QLineEdit::Password);
    m_confirm->setAccessibleName(tr("New password again"));
    auto *form = new QFormLayout;
    form->addRow(buddyLabel(tr("&New password:"), m_password), m_password);
    form->addRow(buddyLabel(tr("Password &again:"), m_confirm), m_confirm);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &PasswordDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    m_password->setFocus();
}

QString PasswordDialog::password() const
{
    return m_password->text();
}

void PasswordDialog::accept()
{
    if (m_password->text().isEmpty()) {
        complain(this, m_password, tr("Enter a password."));
        return;
    }
    if (m_password->text() != m_confirm->text()) {
        complain(this, m_confirm, tr("The passwords are different."));
        return;
    }
    QDialog::accept();
}

// ------------------------------------------------------------------ RoleDialog

RoleDialog::RoleDialog(const protocol::RoleInfo &role, const QList<quint16> &grantable, int maxRank, QWidget *parent)
    : QDialog(parent)
    , m_role(role)
{
    setWindowTitle(role.id == 0 && !role.builtIn ? tr("New role") : tr("Edit role %1").arg(role.name));
    resize(520, 560);
    m_name = new QLineEdit(role.name);
    m_name->setMaxLength(static_cast<int>(protocol::kMaxRoleNameLength));
    m_name->setAccessibleName(tr("Role name"));
    m_name->setEnabled(!role.builtIn);
    m_rank = new QSpinBox;
    m_rank->setRange(2, std::max(2, maxRank));
    m_rank->setValue(role.id == 0 ? std::min(20, m_rank->maximum()) : role.rank);
    m_rank->setAccessibleName(tr("Rank"));
    m_rank->setEnabled(!role.builtIn);
    m_capabilities = new QListWidget;
    m_capabilities->setAccessibleName(tr("Permissions"));
    for (Capability c : moderation::allCapabilities()) {
        const auto value = static_cast<quint16>(c);
        const bool has = role.capabilities.contains(value);
        if (moderation::isOwnerOnly(c) || (!grantable.contains(value) && !has)) {
            continue;
        }
        auto *item = new QListWidgetItem(
            tr("%1 (%2)").arg(moderation::capabilityDescription(c), moderation::capabilityName(c)), m_capabilities);
        item->setData(Qt::UserRole, value);
        item->setFlags(grantable.contains(value) ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable)
                                                 : (Qt::ItemIsSelectable | Qt::ItemIsUserCheckable));
        item->setCheckState(has ? Qt::Checked : Qt::Unchecked);
    }
    if (m_capabilities->count() > 0) {
        m_capabilities->setCurrentRow(0);
    }
    auto *form = new QFormLayout;
    form->addRow(buddyLabel(tr("&Name:"), m_name), m_name);
    form->addRow(buddyLabel(tr("&Rank:"), m_rank), m_rank);
    form->addRow(QString(), helpLabel(tr("Higher ranks can act on lower ones. Administrators are %1; registered "
                                         "users %2.")
                                          .arg(moderation::roles::kAdministratorRank)
                                          .arg(10),
                                      m_rank));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &RoleDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buddyLabel(tr("&Permissions (Space checks or unchecks):"), m_capabilities));
    layout->addWidget(m_capabilities, 1);
    layout->addWidget(buttons);
    (role.builtIn ? static_cast<QWidget *>(m_capabilities) : m_name)->setFocus();
}

protocol::RoleInfo RoleDialog::role() const
{
    protocol::RoleInfo r = m_role;
    if (!r.builtIn) {
        r.name = m_name->text().trimmed();
        r.rank = m_rank->value();
    }
    r.capabilities.clear();
    for (int i = 0; i < m_capabilities->count(); ++i) {
        const QListWidgetItem *item = m_capabilities->item(i);
        if (item->checkState() == Qt::Checked) {
            r.capabilities.append(static_cast<quint16>(item->data(Qt::UserRole).toUInt()));
        }
    }
    return r;
}

void RoleDialog::accept()
{
    if (!m_role.builtIn && !protocol::isValidName(m_name->text().trimmed(), protocol::kMaxRoleNameLength)) {
        complain(this, m_name, tr("Enter a name for the role."));
        return;
    }
    QDialog::accept();
}

// ------------------------------------------------------------------ RuleDialog

RuleDialog::RuleDialog(const QList<QPair<quint32, QString>> &channels, const QList<QPair<quint32, QString>> &roles,
                       QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Add permission rule"));
    m_where = new QComboBox;
    m_where->setAccessibleName(tr("Where"));
    m_where->addItem(tr("The whole server"), 0u);
    for (const auto &[id, path] : channels) {
        m_where->addItem(path, id);
    }
    m_subject = new QComboBox;
    m_subject->setAccessibleName(tr("For"));
    m_subject->addItem(tr("Everyone with a role"), false);
    m_subject->addItem(tr("One account"), true);
    m_role = new QComboBox;
    m_role->setAccessibleName(tr("Role"));
    for (const auto &[id, name] : roles) {
        m_role->addItem(name, id);
    }
    m_account = new QLineEdit;
    m_account->setMaxLength(static_cast<int>(protocol::kMaxDisplayNameLength));
    m_account->setAccessibleName(tr("Account name"));
    m_capability = new QComboBox;
    m_capability->setAccessibleName(tr("Permission"));
    for (Capability c : moderation::allCapabilities()) {
        if (!moderation::isOwnerOnly(c)) {
            m_capability->addItem(moderation::capabilityName(c), static_cast<quint16>(c));
        }
    }
    m_capabilityHelp = new QLabel;
    m_capabilityHelp->setWordWrap(true);
    m_effect = new QComboBox;
    m_effect->setAccessibleName(tr("Effect"));
    m_effect->addItem(tr("Allow"), true);
    m_effect->addItem(tr("Deny"), false);
    auto *form = new QFormLayout;
    form->addRow(buddyLabel(tr("&Where:"), m_where), m_where);
    form->addRow(buddyLabel(tr("&For:"), m_subject), m_subject);
    form->addRow(buddyLabel(tr("&Role:"), m_role), m_role);
    form->addRow(buddyLabel(tr("&Account:"), m_account), m_account);
    form->addRow(buddyLabel(tr("&Permission:"), m_capability), m_capability);
    form->addRow(QString(), m_capabilityHelp);
    form->addRow(buddyLabel(tr("&Effect:"), m_effect), m_effect);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    buttons->addButton(tr("A&dd rule"), QDialogButtonBox::AcceptRole)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &RuleDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    connect(m_subject, &QComboBox::currentIndexChanged, this, &RuleDialog::updateFields);
    connect(m_capability, &QComboBox::currentIndexChanged, this, &RuleDialog::updateFields);
    updateFields();
    m_where->setFocus();
}

void RuleDialog::updateFields()
{
    const bool account = m_subject->currentData().toBool();
    m_role->setEnabled(!account);
    m_account->setEnabled(account);
    if (const auto c = moderation::capabilityFromValue(static_cast<quint16>(m_capability->currentData().toUInt()))) {
        const QString description = moderation::capabilityDescription(*c);
        m_capabilityHelp->setText(description);
        m_capability->setAccessibleDescription(description);
    }
}

protocol::RuleInfo RuleDialog::rule() const
{
    protocol::RuleInfo r;
    r.channelId = m_where->currentData().toUInt();
    r.forAccount = m_subject->currentData().toBool();
    r.account = r.forAccount ? m_account->text().trimmed() : QString();
    r.roleId = r.forAccount ? 0 : m_role->currentData().toUInt();
    r.capability = static_cast<quint16>(m_capability->currentData().toUInt());
    r.allow = m_effect->currentData().toBool();
    return r;
}

void RuleDialog::accept()
{
    if (m_subject->currentData().toBool() && m_account->text().trimmed().isEmpty()) {
        complain(this, m_account, tr("Enter the account name."));
        return;
    }
    QDialog::accept();
}

// -------------------------------------------------------- AppealDecisionDialog

AppealDecisionDialog::AppealDecisionDialog(const QString &summary, bool canReduce, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Decide appeal"));
    auto *text = new QLabel(summary);
    text->setWordWrap(true);
    text->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    m_decision = new QComboBox;
    m_decision->setAccessibleName(tr("Decision"));
    m_decision->setAccessibleDescription(summary);
    m_decision->addItem(tr("Uphold: the sanction stays"), static_cast<int>(moderation::Appeal::State::Upheld));
    if (canReduce) {
        m_decision->addItem(tr("Reduce: make it shorter"), static_cast<int>(moderation::Appeal::State::Reduced));
    }
    m_decision->addItem(tr("Overturn: lift it"), static_cast<int>(moderation::Appeal::State::Overturned));
    m_duration = new QComboBox;
    m_duration->setAccessibleName(tr("New length, from when it was given"));
    for (const auto &[label, seconds] : sanctionDurations()) {
        if (seconds != 0) {
            m_duration->addItem(label, seconds);
        }
    }
    m_reason = new QLineEdit;
    m_reason->setMaxLength(static_cast<int>(protocol::kMaxReasonLength));
    m_reason->setAccessibleName(tr("Reason"));
    auto *form = new QFormLayout;
    form->addRow(text);
    form->addRow(buddyLabel(tr("&Decision:"), m_decision), m_decision);
    form->addRow(buddyLabel(tr("New &length:"), m_duration), m_duration);
    form->addRow(buddyLabel(tr("&Reason:"), m_reason), m_reason);
    form->addRow(QString(), helpLabel(tr("The person who appealed is told the decision and the reason."), m_reason));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    buttons->addButton(tr("&Decide"), QDialogButtonBox::AcceptRole)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &AppealDecisionDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    connect(m_decision, &QComboBox::currentIndexChanged, this, &AppealDecisionDialog::updateFields);
    updateFields();
    m_decision->setFocus();
}

void AppealDecisionDialog::updateFields()
{
    m_duration->setEnabled(m_decision->currentData().toInt() == static_cast<int>(moderation::Appeal::State::Reduced));
}

protocol::DecideAppeal AppealDecisionDialog::request() const
{
    protocol::DecideAppeal r;
    r.decision = static_cast<quint8>(m_decision->currentData().toInt());
    r.reason = m_reason->text().trimmed();
    r.newDurationSeconds = m_duration->isEnabled() ? m_duration->currentData().toUInt() : 0;
    return r;
}

void AppealDecisionDialog::accept()
{
    if (m_reason->text().trimmed().isEmpty()) {
        complain(this, m_reason, tr("Please give a reason. The person who appealed is told it."));
        return;
    }
    QDialog::accept();
}

} // namespace opentalk::ui

#include "AdminDialog.moc"
