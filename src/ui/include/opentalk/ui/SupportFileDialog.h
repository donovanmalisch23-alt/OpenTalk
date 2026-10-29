// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QDialog>

#include <functional>

class QLabel;
class QPlainTextEdit;

namespace opentalk::ui {

/// Help, "Create a support file": shows what the file contains, so it can be read first,
/// then saves it or copies it to the clipboard.
class SupportFileDialog : public QDialog {
    Q_OBJECT

public:
    SupportFileDialog(QString text, QString suggestedPath, QWidget *parent = nullptr);

    QString text() const { return m_text; }
    /// What happened after saving or copying (also announced to screen readers).
    QString resultText() const;

    /// Replaces the save dialog (tests): returns the path to save to, or empty to cancel.
    void setFileChooser(std::function<QString(const QString &suggestedPath)> chooser)
    {
        m_chooser = std::move(chooser);
    }

    /// Asks where to save (unless a chooser is set) and saves there.
    void save();
    void copyToClipboard();

private:
    void showResult(const QString &text);

    QString m_text;
    QString m_suggestedPath;
    QPlainTextEdit *m_view = nullptr;
    QLabel *m_result = nullptr;
    std::function<QString(const QString &)> m_chooser;
};

} // namespace opentalk::ui
