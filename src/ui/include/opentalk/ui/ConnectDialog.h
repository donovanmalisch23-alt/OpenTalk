// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/storage/ServerProfiles.h"

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
    ConnectDialog(storage::ServerProfileStore &profiles, QWidget *parent = nullptr);

    struct Choice {
        storage::ServerProfile profile;
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

} // namespace opentalk::ui
