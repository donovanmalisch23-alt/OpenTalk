// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QListWidget;
class QStackedWidget;

namespace opentalk::storage {
class AppPaths;
class SettingsStore;
} // namespace opentalk::storage

namespace opentalk::ui {

/// Categorised settings window. A list of categories on the left selects a page on the
/// right. Changes are kept in the dialog until OK or Apply writes them to the store.
///
/// Keyboard: Tab moves between the category list and the page; Up/Down in the list
/// changes page; Ctrl+Tab / Ctrl+Shift+Tab (or Ctrl+PgDown / Ctrl+PgUp) switch category
/// from anywhere in the dialog.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(storage::SettingsStore &settings, const storage::AppPaths &paths, QWidget *parent = nullptr);

    /// Writes the edited values to the store and saves the file. Returns false and shows
    /// an error if saving failed.
    bool apply();

    QListWidget *categoryList() const { return m_categories; }
    QStackedWidget *pages() const { return m_pages; }

private:
    void addPage(const QString &name, QWidget *page);
    void selectRelativeCategory(int step);
    void loadValues();
    void updateApplyButton();
    bool hasChanges() const;

    QWidget *createGeneralPage();
    QWidget *createAccessibilityPage();
    QWidget *createDiagnosticsPage();

    storage::SettingsStore &m_settings;
    const storage::AppPaths &m_paths;

    QListWidget *m_categories = nullptr;
    QStackedWidget *m_pages = nullptr;
    QDialogButtonBox *m_buttons = nullptr;

    QCheckBox *m_restoreLayout = nullptr;
    QCheckBox *m_announceStatus = nullptr;
    QComboBox *m_logLevel = nullptr;
};

} // namespace opentalk::ui
