// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/ui/SupportFileDialog.h"

#include <QAccessible>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QGuiApplication>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QVBoxLayout>

namespace opentalk::ui {

SupportFileDialog::SupportFileDialog(QString text, QString suggestedPath, QWidget *parent)
    : QDialog(parent)
    , m_text(std::move(text))
    , m_suggestedPath(std::move(suggestedPath))
{
    setWindowTitle(tr("Create a support file"));
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(tr("This is what the support file contains. It has no passwords, messages or server "
                                "addresses, but it does show your settings, sound devices, plugins and the end of "
                                "the log. Read it, then save it or copy it, and attach it to your problem report."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *viewLabel = new QLabel(tr("Support &information:"), this);
    layout->addWidget(viewLabel);
    m_view = new QPlainTextEdit(this);
    m_view->setObjectName(QStringLiteral("supportText"));
    m_view->setReadOnly(true);
    // Keyboard selection keeps the caret, so a screen reader can read it line by line.
    m_view->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_view->setPlainText(m_text);
    m_view->setAccessibleName(tr("Support information"));
    viewLabel->setBuddy(m_view);
    layout->addWidget(m_view, 1);

    m_result = new QLabel(this);
    m_result->setObjectName(QStringLiteral("supportResult"));
    m_result->setAccessibleName(tr("Result"));
    m_result->setWordWrap(true);
    layout->addWidget(m_result);

    auto *buttons = new QDialogButtonBox(this);
    QPushButton *saveButton = buttons->addButton(tr("&Save to a file..."), QDialogButtonBox::ActionRole);
    saveButton->setObjectName(QStringLiteral("saveSupportFile"));
    QPushButton *copyButton = buttons->addButton(tr("&Copy all"), QDialogButtonBox::ActionRole);
    copyButton->setObjectName(QStringLiteral("copySupportFile"));
    buttons->addButton(QDialogButtonBox::Close);
    connect(saveButton, &QPushButton::clicked, this, &SupportFileDialog::save);
    connect(copyButton, &QPushButton::clicked, this, &SupportFileDialog::copyToClipboard);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    resize(700, 500);
    m_view->setFocus();
}

QString SupportFileDialog::resultText() const
{
    return m_result->text();
}

void SupportFileDialog::save()
{
    const QString path = m_chooser ? m_chooser(m_suggestedPath)
                                   : QFileDialog::getSaveFileName(this, tr("Save the support file"), m_suggestedPath,
                                                                  tr("Text files (*.txt)"));
    if (path.isEmpty()) {
        return;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(m_text.toUtf8()) < 0 || !file.commit()) {
        showResult(tr("Could not save the support file: %1").arg(file.errorString()));
        return;
    }
    showResult(tr("Saved as %1").arg(QDir::toNativeSeparators(path)));
}

void SupportFileDialog::copyToClipboard()
{
    QGuiApplication::clipboard()->setText(m_text);
    showResult(tr("Copied. Paste it into your problem report."));
}

void SupportFileDialog::showResult(const QString &text)
{
    m_result->setText(text);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QAccessibleAnnouncementEvent event(m_result, text);
    QAccessible::updateAccessibility(&event);
#endif
}

} // namespace opentalk::ui
