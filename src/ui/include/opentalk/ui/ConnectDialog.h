// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/storage/ServerProfiles.h"

namespace opentalk::storage {
class CredentialStore;
}

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace opentalk::ui {

/// Chooses a saved server or enters a new one, and how to log in.
///
/// Keyboard: the saved-server list comes first; Up/Down picks a server and fills the
/// form; Tab moves through the form; Enter connects; Delete removes the selected saved
/// server (after confirmation).
class ConnectDialog : public QDialog {
    Q_OBJECT

public:
    /// `credentials` (may be null) is where remembered passwords are read from.
    ConnectDialog(storage::ServerProfileStore &profiles, storage::CredentialStore *credentials = nullptr,
                  QWidget *parent = nullptr);

    struct Choice {
        storage::ServerProfile profile; ///< `rememberPassword` says whether to keep the password.
        QString password;
        bool save = true;
    };
    Choice choice() const;

    QListWidget *savedServers() const { return m_saved; }

    void accept() override;

private:
    void fillFrom(const storage::ServerProfile &profile);
    void clearForm();
    void updateLoginFields();
    void deleteSelected();
    void reloadList(const QString &selectId);
    bool validate();

    storage::ServerProfileStore &m_profiles;
    storage::CredentialStore *m_credentials = nullptr;
    QString m_editingId;

    QListWidget *m_saved = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_host = nullptr;
    QSpinBox *m_port = nullptr;
    QComboBox *m_loginMode = nullptr;
    QLabel *m_userLabel = nullptr;
    QLineEdit *m_user = nullptr;
    QLineEdit *m_password = nullptr;
    QCheckBox *m_save = nullptr;
    QCheckBox *m_remember = nullptr;
    QPushButton *m_delete = nullptr;
};

/// Asks for the details of a new channel.
class CreateChannelDialog : public QDialog {
    Q_OBJECT

public:
    CreateChannelDialog(const QString &parentPath, bool canCreatePersistent, QWidget *parent = nullptr);

    QString channelName() const;
    QString topic() const;
    QString password() const;
    quint16 maxUsers() const;
    bool persistent() const;

    void accept() override;

private:
    QLineEdit *m_name = nullptr;
    QLineEdit *m_topic = nullptr;
    QLineEdit *m_password = nullptr;
    QSpinBox *m_maxUsers = nullptr;
    QCheckBox *m_persistent = nullptr;
};

/// Settings for hosting a server on this computer.
class LocalServerDialog : public QDialog {
    Q_OBJECT

public:
    struct Choice {
        QString name;
        quint16 port = 10350;
        bool allowOtherComputers = false;
        bool allowGuests = true;
        QString adminName; ///< Empty: join as a guest instead.
        QString adminPassword;
    };

    explicit LocalServerDialog(const Choice &initial, QWidget *parent = nullptr);
    Choice choice() const;
    void accept() override;

private:
    QLineEdit *m_name = nullptr;
    QSpinBox *m_port = nullptr;
    QCheckBox *m_allowOthers = nullptr;
    QCheckBox *m_allowGuests = nullptr;
    QLineEdit *m_adminName = nullptr;
    QLineEdit *m_adminPassword = nullptr;
};

} // namespace opentalk::ui
