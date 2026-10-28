// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/moderation/Sanctions.h"
#include "opentalk/protocol/Messages.h"

#include <QDialog>
#include <QList>
#include <QPair>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTreeWidget;

namespace opentalk::client {
class Connection;
}

namespace opentalk::ui {

/// Durations offered for sanctions: (label, seconds; 0 = until lifted).
QList<QPair<QString, quint32>> sanctionDurations();
/// "Warning", "Voice mute", "Ban" ... (moderation::SanctionType values).
QString sanctionTypeName(quint8 type);
/// "active", "ended", "lifted", "overturned on appeal".
QString sanctionStateName(quint8 state);
QString appealStateName(quint8 state);
QString reportStateName(quint8 state);
/// "Mute until 2026-11-02 14:30", "Ban until lifted", "Warning" ... for history lines.
QString describeSanction(const protocol::SanctionInfo &sanction, const QString &channelName = {});

/// Warn, mute, kick, ban or restrict someone. Fields that do not apply to the chosen
/// kind are disabled, and reasons are required where the server requires them.
class SanctionDialog : public QDialog {
    Q_OBJECT

public:
    struct Options {
        QString targetName;
        moderation::SanctionType type = moderation::SanctionType::Warning;
        quint32 channelId = 0; ///< The channel the target is in (offered as a scope).
        QString channelName;   ///< Empty: only server-wide.
        bool canBanAddress = false;
        bool canBanPermanently = false;
    };

    explicit SanctionDialog(const Options &options, QWidget *parent = nullptr);
    /// The request (requestId and the target are filled in by the caller).
    protocol::ApplySanction request() const;
    void accept() override;

private:
    void fillDurations();
    void updateFields();
    moderation::SanctionType type() const;

    Options m_options;
    QComboBox *m_type = nullptr;
    QComboBox *m_duration = nullptr;
    QComboBox *m_scope = nullptr;
    QLineEdit *m_reason = nullptr;
    QCheckBox *m_banAddress = nullptr;
    QLabel *m_reasonHelp = nullptr;
};

/// Tells moderators about someone, with a reason.
class ReportDialog : public QDialog {
    Q_OBJECT

public:
    explicit ReportDialog(const QString &targetName, QWidget *parent = nullptr);
    QString reason() const;
    void accept() override;

private:
    QLineEdit *m_reason = nullptr;
};

/// Picks a channel from a list of (id, path).
class ChannelPickerDialog : public QDialog {
    Q_OBJECT

public:
    ChannelPickerDialog(const QString &title, const QString &label, const QList<QPair<quint32, QString>> &channels,
                        quint32 current, QWidget *parent = nullptr);
    quint32 channelId() const;

private:
    QListWidget *m_list = nullptr;
};

/// Changes a channel's name, topic, password and user limit.
class EditChannelDialog : public QDialog {
    Q_OBJECT

public:
    EditChannelDialog(const protocol::ChannelInfo &channel, QWidget *parent = nullptr);
    protocol::EditChannel request() const;
    void accept() override;

private:
    quint32 m_channelId = 0;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_topic = nullptr;
    QSpinBox *m_maxUsers = nullptr;
    QComboBox *m_passwordChoice = nullptr;
    QLineEdit *m_password = nullptr;
};

/// The user's own sanctions (current and past) and appeals, with "Appeal...".
class MySanctionsDialog : public QDialog {
    Q_OBJECT

public:
    explicit MySanctionsDialog(client::Connection &connection, QWidget *parent = nullptr);

private:
    void refresh();
    void appealSelected();
    void showResult(quint64 requestId, const protocol::Message &response);
    void requestFinished(quint64 requestId, const std::optional<protocol::Error> &error);

    client::Connection &m_connection;
    QTreeWidget *m_sanctions = nullptr;
    QTreeWidget *m_appeals = nullptr;
    QPushButton *m_appeal = nullptr;
    QLabel *m_status = nullptr;
    quint64 m_sanctionsRequest = 0;
    quint64 m_appealsRequest = 0;
    quint64 m_submitRequest = 0;
};

} // namespace opentalk::ui
