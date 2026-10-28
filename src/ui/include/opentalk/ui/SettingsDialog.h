// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QDialog>
#include <QKeySequence>
#include <QTimer>

#include <functional>
#include <optional>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;

namespace opentalk::storage {
class AppPaths;
class SettingsStore;
} // namespace opentalk::storage

namespace opentalk::ui {

class AudioController;

/// Categorised settings window. A list of categories on the left selects a page on the
/// right. Changes are kept in the dialog until OK or Apply writes them to the store.
///
/// Keyboard: Tab moves between the category list and the page; Up/Down in the list
/// changes page; Ctrl+Tab / Ctrl+Shift+Tab (or Ctrl+PgDown / Ctrl+PgUp) switch category
/// from anywhere in the dialog.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    /// `audio` provides device lists, the microphone level and sound previews; without
    /// it the audio pages still edit the settings but cannot list devices.
    SettingsDialog(storage::SettingsStore &settings, const storage::AppPaths &paths, AudioController *audio = nullptr,
                   QWidget *parent = nullptr);

    /// Writes the edited values to the store and saves the file. Returns false and shows
    /// an error if saving failed.
    bool apply();

    QListWidget *categoryList() const { return m_categories; }
    QStackedWidget *pages() const { return m_pages; }
    /// Selects a page by its category name; returns false if there is none.
    bool selectCategory(const QString &name);

    /// Replaces the key-capture dialog (for tests).
    void setKeyCapture(std::function<std::optional<QKeySequence>()> capture) { m_keyCapture = std::move(capture); }

private:
    /// Ties one control to one setting: how to show it, whether it changed, how to store it.
    struct Binding {
        std::function<void()> load;
        std::function<bool()> changed;
        std::function<void()> store;
    };

    void addPage(const QString &name, QWidget *page);
    void selectRelativeCategory(int step);
    void loadValues();
    void updateApplyButton();
    bool hasChanges() const;
    void bindCheckBox(QCheckBox *box, const QString &key);
    void bindComboData(QComboBox *combo, const QString &key);
    void bindSpinBox(QSpinBox *spin, const QString &key);
    void updatePushToTalkKeyText();
    void updateLevelMeter();

    QWidget *createGeneralPage();
    QWidget *createAccessibilityPage();
    QWidget *createAudioPage();
    QWidget *createSoundsPage();
    QWidget *createDiagnosticsPage();

    storage::SettingsStore &m_settings;
    const storage::AppPaths &m_paths;
    AudioController *m_audio;
    QList<Binding> m_bindings;
    std::function<std::optional<QKeySequence>()> m_keyCapture;

    QListWidget *m_categories = nullptr;
    QStackedWidget *m_pages = nullptr;
    QDialogButtonBox *m_buttons = nullptr;

    QComboBox *m_transmitMode = nullptr;
    QLineEdit *m_pushToTalkKey = nullptr;
    QKeySequence m_pendingPushToTalkKey;
    QSpinBox *m_voiceLevel = nullptr;
    QLabel *m_levelText = nullptr;
    QProgressBar *m_levelBar = nullptr;
    QTimer m_levelTimer;
};

} // namespace opentalk::ui
