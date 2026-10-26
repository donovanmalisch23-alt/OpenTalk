// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/ui/SettingsDialog.h"

#include "opentalk/storage/AppPaths.h"
#include "opentalk/storage/ClientSettings.h"
#include "opentalk/storage/SettingsStore.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace opentalk::ui {
namespace {

/// Explanatory text under a control. The same text becomes the control's accessible
/// description so screen reader users hear it when the control gets focus.
QLabel *helpText(const QString &text, QWidget *describedControl)
{
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    describedControl->setAccessibleDescription(text);
    return label;
}

QWidget *pageWithHeading(const QString &heading, QLayout *body)
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *title = new QLabel(heading);
    QFont font = title->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.2);
    title->setFont(font);
    layout->addWidget(title);
    layout->addLayout(body);
    layout->addStretch(1);
    return page;
}

} // namespace

SettingsDialog::SettingsDialog(storage::SettingsStore &settings, const storage::AppPaths &paths, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_paths(paths)
{
    setWindowTitle(tr("Settings"));
    resize(640, 420);

    m_categories = new QListWidget;
    m_categories->setAccessibleName(tr("Settings categories"));
    m_categories->setMaximumWidth(200);
    m_pages = new QStackedWidget;

    addPage(tr("General"), createGeneralPage());
    addPage(tr("Accessibility"), createAccessibilityPage());
    addPage(tr("Logging and Diagnostics"), createDiagnosticsPage());

    connect(m_categories, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_categories->setCurrentRow(0);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    connect(m_buttons, &QDialogButtonBox::accepted, this, [this] {
        if (apply()) {
            accept();
        }
    });
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &SettingsDialog::apply);

    auto *body = new QHBoxLayout;
    body->addWidget(m_categories);
    body->addWidget(m_pages, 1);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(body, 1);
    layout->addWidget(m_buttons);

    for (const QKeySequence &key : {QKeySequence(Qt::CTRL | Qt::Key_Tab), QKeySequence(Qt::CTRL | Qt::Key_PageDown)}) {
        connect(new QShortcut(key, this), &QShortcut::activated, this, [this] { selectRelativeCategory(1); });
    }
    for (const QKeySequence &key :
         {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), QKeySequence(Qt::CTRL | Qt::Key_PageUp)}) {
        connect(new QShortcut(key, this), &QShortcut::activated, this, [this] { selectRelativeCategory(-1); });
    }

    loadValues();
    connect(m_restoreLayout, &QCheckBox::toggled, this, &SettingsDialog::updateApplyButton);
    connect(m_announceStatus, &QCheckBox::toggled, this, &SettingsDialog::updateApplyButton);
    connect(m_logLevel, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateApplyButton);
    updateApplyButton();

    m_categories->setFocus();
}

void SettingsDialog::addPage(const QString &name, QWidget *page)
{
    m_categories->addItem(name);
    m_pages->addWidget(page);
}

void SettingsDialog::selectRelativeCategory(int step)
{
    const int count = m_categories->count();
    m_categories->setCurrentRow((m_categories->currentRow() + step + count) % count);
}

QWidget *SettingsDialog::createGeneralPage()
{
    m_restoreLayout = new QCheckBox(tr("&Remember window size and layout"));
    auto *form = new QVBoxLayout;
    form->addWidget(m_restoreLayout);
    form->addWidget(helpText(tr("When enabled, OpenTalk reopens with the same window size, position and pane sizes."),
                             m_restoreLayout));
    return pageWithHeading(tr("General"), form);
}

QWidget *SettingsDialog::createAccessibilityPage()
{
    m_announceStatus = new QCheckBox(tr("&Announce status bar changes with the screen reader"));
    auto *form = new QVBoxLayout;
    form->addWidget(m_announceStatus);
    QString help = tr("When enabled, changes such as connection status are spoken by NVDA or Narrator "
                      "without moving keyboard focus.");
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
    help += QLatin1Char(' ') + tr("This build uses a Qt version without announcement support, so this setting "
                                  "currently has no effect.");
#endif
    form->addWidget(helpText(help, m_announceStatus));
    return pageWithHeading(tr("Accessibility"), form);
}

QWidget *SettingsDialog::createDiagnosticsPage()
{
    m_logLevel = new QComboBox;
    m_logLevel->addItem(tr("Warnings and errors only"), QStringLiteral("warning"));
    m_logLevel->addItem(tr("Normal"), QStringLiteral("info"));
    m_logLevel->addItem(tr("Detailed (for troubleshooting)"), QStringLiteral("debug"));

    auto *logLabel = new QLabel(tr("&Log detail:"));
    logLabel->setBuddy(m_logLevel);
    m_logLevel->setAccessibleName(tr("Log detail"));

    auto *dataFolder = new QLineEdit(QDir::toNativeSeparators(m_paths.dataRoot()));
    dataFolder->setReadOnly(true);
    auto *dataLabel = new QLabel(tr("&Data folder (%1 mode):").arg(storage::toString(m_paths.mode())));
    dataLabel->setBuddy(dataFolder);
    dataFolder->setAccessibleName(tr("Data folder"));

    auto *form = new QFormLayout;
    form->addRow(logLabel, m_logLevel);
    form->addRow(QString(), helpText(tr("Logs never contain passwords or message contents. Detailed logging "
                                        "may include server addresses and user names."),
                                     m_logLevel));
    form->addRow(dataLabel, dataFolder);
    return pageWithHeading(tr("Logging and Diagnostics"), form);
}

void SettingsDialog::loadValues()
{
    using namespace storage::client;
    m_restoreLayout->setChecked(m_settings.value(kRestoreWindowLayout).toBool());
    m_announceStatus->setChecked(m_settings.value(kAnnounceStatusChanges).toBool());
    const int index = m_logLevel->findData(m_settings.value(kLogLevel).toString());
    m_logLevel->setCurrentIndex(index >= 0 ? index : 1);
}

bool SettingsDialog::hasChanges() const
{
    using namespace storage::client;
    return m_restoreLayout->isChecked() != m_settings.value(kRestoreWindowLayout).toBool() ||
           m_announceStatus->isChecked() != m_settings.value(kAnnounceStatusChanges).toBool() ||
           m_logLevel->currentData().toString() != m_settings.value(kLogLevel).toString();
}

void SettingsDialog::updateApplyButton()
{
    m_buttons->button(QDialogButtonBox::Apply)->setEnabled(hasChanges());
}

bool SettingsDialog::apply()
{
    using namespace storage::client;
    m_settings.setValue(kRestoreWindowLayout, m_restoreLayout->isChecked());
    m_settings.setValue(kAnnounceStatusChanges, m_announceStatus->isChecked());
    m_settings.setValue(kLogLevel, m_logLevel->currentData().toString());

    QString error;
    if (!m_settings.save(&error)) {
        QMessageBox::warning(this, tr("Settings not saved"),
                             tr("Your changes apply to this session but could not be saved.\n\n%1").arg(error));
        updateApplyButton();
        return false;
    }
    updateApplyButton();
    return true;
}

} // namespace opentalk::ui
