// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/protocol/Messages.h"

#include <QDialog>
#include <QList>
#include <QMap>
#include <QPair>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTabWidget;

namespace opentalk::client {
class Connection;
}

namespace opentalk::ui {

namespace detail {
class RequestTracker;
}
class AdminPage;

/// Moderation and administration of the connected server, one tab per area: sanctions,
/// reports, appeals, accounts, roles, channel permissions, the audit log, server
/// settings and statistics. Only the areas the user may use are shown, and the server
/// checks every change again.
///
/// Keyboard: Ctrl+Tab / Ctrl+Shift+Tab change tabs, F5 reloads the current tab.
class AdminDialog : public QDialog {
    Q_OBJECT

public:
    enum class Page { Sanctions, Reports, Appeals, Accounts, Roles, Rules, Audit, Settings, Statistics };

    explicit AdminDialog(client::Connection &connection, QWidget *parent = nullptr);
    ~AdminDialog() override;

    /// The pages this user may use, in tab order.
    QList<Page> pages() const;
    bool showPage(Page page);
    QWidget *pageWidget(Page page) const;
    QTabWidget *tabs() const { return m_tabs; }
    /// The last result or problem, as shown in the status line.
    QString statusText() const;
    /// Requests still waiting for an answer (for tests).
    int pendingRequests() const;

public slots:
    /// Reloads the current page.
    void refreshCurrentPage();

signals:
    /// A result or problem to tell the user (the main window announces it).
    void statusChanged(const QString &text);

private:
    void setStatus(const QString &text);
    void pageShown(int index);

    client::Connection &m_connection;
    detail::RequestTracker *m_tracker = nullptr;
    QTabWidget *m_tabs = nullptr;
    QLabel *m_status = nullptr;
    QMap<Page, AdminPage *> m_pages;
    QList<Page> m_order;
    QList<AdminPage *> m_loaded;
};

/// Creates an account: name, password (twice) and role.
class AccountDialog : public QDialog {
    Q_OBJECT

public:
    /// `roles` are the roles that may be given, as (id, name).
    AccountDialog(const QList<QPair<quint32, QString>> &roles, QWidget *parent = nullptr);
    protocol::CreateAccount request() const;
    void accept() override;

private:
    QLineEdit *m_name = nullptr;
    QLineEdit *m_password = nullptr;
    QLineEdit *m_confirm = nullptr;
    QComboBox *m_role = nullptr;
    QCheckBox *m_bot = nullptr;
};

/// Asks for a new password twice.
class PasswordDialog : public QDialog {
    Q_OBJECT

public:
    PasswordDialog(const QString &accountName, QWidget *parent = nullptr);
    QString password() const;
    void accept() override;

private:
    QLineEdit *m_password = nullptr;
    QLineEdit *m_confirm = nullptr;
};

/// Creates or changes a role: name, rank and capabilities (a list of check boxes).
/// Built-in roles keep their name and rank; only their capabilities change.
class RoleDialog : public QDialog {
    Q_OBJECT

public:
    /// `grantable` are the capabilities the user may hand out (their own).
    RoleDialog(const protocol::RoleInfo &role, const QList<quint16> &grantable, int maxRank, QWidget *parent = nullptr);
    protocol::RoleInfo role() const;
    void accept() override;
    QListWidget *capabilityList() const { return m_capabilities; }

private:
    protocol::RoleInfo m_role;
    QLineEdit *m_name = nullptr;
    QSpinBox *m_rank = nullptr;
    QListWidget *m_capabilities = nullptr;
};

/// Adds a permission rule: where, for whom (a role or an account), which capability,
/// and allow or deny.
class RuleDialog : public QDialog {
    Q_OBJECT

public:
    RuleDialog(const QList<QPair<quint32, QString>> &channels, const QList<QPair<quint32, QString>> &roles,
               QWidget *parent = nullptr);
    protocol::RuleInfo rule() const;
    void accept() override;

private:
    void updateFields();

    QComboBox *m_where = nullptr;
    QComboBox *m_subject = nullptr;
    QComboBox *m_role = nullptr;
    QLineEdit *m_account = nullptr;
    QComboBox *m_capability = nullptr;
    QComboBox *m_effect = nullptr;
    QLabel *m_capabilityHelp = nullptr;
};

/// Decides an appeal: uphold, reduce (with a new length) or overturn, with a reason.
class AppealDecisionDialog : public QDialog {
    Q_OBJECT

public:
    /// `canReduce` is false for sanctions that have no length (warnings and kicks).
    AppealDecisionDialog(const QString &summary, bool canReduce, QWidget *parent = nullptr);
    protocol::DecideAppeal request() const;
    void accept() override;

private:
    void updateFields();

    QComboBox *m_decision = nullptr;
    QComboBox *m_duration = nullptr;
    QLineEdit *m_reason = nullptr;
};

} // namespace opentalk::ui
