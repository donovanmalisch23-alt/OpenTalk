// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/ui/SettingsDialog.h"

#include "opentalk/storage/AppPaths.h"
#include "opentalk/storage/ClientSettings.h"
#include "opentalk/storage/SettingsStore.h"
#include "opentalk/ui/AudioController.h"
#include "opentalk/ui/AudioDialogs.h"
#include "opentalk/ui/PushToTalkMonitor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

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

SettingsDialog::SettingsDialog(storage::SettingsStore &settings, const storage::AppPaths &paths, AudioController *audio,
                               QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_paths(paths)
    , m_audio(audio)
{
    setWindowTitle(tr("Settings"));
    resize(720, 520);
    m_keyCapture = [this]() -> std::optional<QKeySequence> {
        KeyCaptureDialog capture(this);
        if (capture.exec() == QDialog::Accepted) {
            return capture.key();
        }
        return std::nullopt;
    };

    m_categories = new QListWidget;
    m_categories->setAccessibleName(tr("Settings categories"));
    m_categories->setMaximumWidth(200);
    m_pages = new QStackedWidget;

    addPage(tr("General"), createGeneralPage());
    addPage(tr("Accessibility"), createAccessibilityPage());
    addPage(tr("Audio"), createAudioPage());
    addPage(tr("Voice Effects and Media"), createEffectsPage());
    addPage(tr("Sounds"), createSoundsPage());
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
    updateApplyButton();

    m_levelTimer.setInterval(250);
    connect(&m_levelTimer, &QTimer::timeout, this, &SettingsDialog::updateLevelMeter);
    connect(m_pages, &QStackedWidget::currentChanged, this, [this] {
        // The meter only runs while the Audio page is shown.
        if (m_audio != nullptr && m_categories->currentItem() != nullptr &&
            m_categories->currentItem()->text() == tr("Audio")) {
            m_levelTimer.start();
        } else {
            m_levelTimer.stop();
        }
    });

    m_categories->setFocus();
}

bool SettingsDialog::selectCategory(const QString &name)
{
    for (int i = 0; i < m_categories->count(); ++i) {
        if (m_categories->item(i)->text() == name) {
            m_categories->setCurrentRow(i);
            return true;
        }
    }
    return false;
}

void SettingsDialog::bindCheckBox(QCheckBox *box, const QString &key)
{
    m_bindings.append({[this, box, key] { box->setChecked(m_settings.value(key).toBool()); },
                       [this, box, key] { return box->isChecked() != m_settings.value(key).toBool(); },
                       [this, box, key] { m_settings.setValue(key, box->isChecked()); }});
    connect(box, &QCheckBox::toggled, this, &SettingsDialog::updateApplyButton);
}

void SettingsDialog::bindComboData(QComboBox *combo, const QString &key)
{
    m_bindings.append({[this, combo, key] {
                           const int index = combo->findData(m_settings.value(key));
                           combo->setCurrentIndex(index >= 0 ? index : 0);
                       },
                       [this, combo, key] { return combo->currentData() != m_settings.value(key); },
                       [this, combo, key] { m_settings.setValue(key, combo->currentData()); }});
    connect(combo, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateApplyButton);
}

void SettingsDialog::bindSpinBox(QSpinBox *spin, const QString &key)
{
    m_bindings.append({[this, spin, key] { spin->setValue(m_settings.value(key).toInt()); },
                       [this, spin, key] { return spin->value() != m_settings.value(key).toInt(); },
                       [this, spin, key] { m_settings.setValue(key, spin->value()); }});
    connect(spin, &QSpinBox::valueChanged, this, &SettingsDialog::updateApplyButton);
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
    auto *restoreLayout = new QCheckBox(tr("&Remember window size and layout"));
    bindCheckBox(restoreLayout, storage::client::kRestoreWindowLayout);
    auto *form = new QVBoxLayout;
    form->addWidget(restoreLayout);
    form->addWidget(helpText(tr("When enabled, OpenTalk reopens with the same window size, position and pane sizes."),
                             restoreLayout));
    return pageWithHeading(tr("General"), form);
}

QWidget *SettingsDialog::createAccessibilityPage()
{
    using namespace storage::client;
    auto *announceStatus = new QCheckBox(tr("&Announce status bar changes with the screen reader"));
    bindCheckBox(announceStatus, kAnnounceStatusChanges);
    auto *form = new QVBoxLayout;
    form->addWidget(announceStatus);
    QString help = tr("When enabled, changes such as connection status are spoken by NVDA or Narrator "
                      "without moving keyboard focus.");
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
    help += QLatin1Char(' ') + tr("This build uses a Qt version without announcement support, so this setting "
                                  "currently has no effect.");
#endif
    form->addWidget(helpText(help, announceStatus));
    auto *announceMessages = new QCheckBox(tr("Announce incoming &messages"));
    bindCheckBox(announceMessages, kAnnounceMessages);
    form->addWidget(announceMessages);
    form->addWidget(helpText(tr("Speaks channel, private and server messages from other people as they arrive."),
                             announceMessages));
    auto *announcePresence = new QCheckBox(tr("Announce people &joining and leaving"));
    bindCheckBox(announcePresence, kAnnouncePresence);
    form->addWidget(announcePresence);
    form->addWidget(helpText(tr("Speaks when people connect, disconnect, or enter or leave your channel. "
                                "Several changes close together are combined into one announcement."),
                             announcePresence));
    auto *announceTalking = new QCheckBox(tr("Announce who starts &talking"));
    bindCheckBox(announceTalking, kAnnounceTalking);
    form->addWidget(announceTalking);
    form->addWidget(helpText(tr("Speaks a person's name when they start talking in your channel. Off by default "
                                "because it can talk over the voices. The channel list always shows who is "
                                "talking."),
                             announceTalking));
    return pageWithHeading(tr("Accessibility"), form);
}

QWidget *SettingsDialog::createAudioPage()
{
    using namespace storage::client;
    auto *form = new QFormLayout;

    const auto deviceCombo = [this](bool input, const QString &key, const QString &name) {
        auto *combo = new QComboBox;
        combo->setAccessibleName(name);
        combo->addItem(tr("System default"), QString());
        if (m_audio != nullptr) {
            const QList<audio::AudioDeviceInfo> devices =
                input ? m_audio->engine().backend().inputDevices() : m_audio->engine().backend().outputDevices();
            for (const audio::AudioDeviceInfo &device : devices) {
                combo->addItem(device.isDefault ? tr("%1 (current default)").arg(device.name) : device.name, device.id);
            }
        }
        // Keep a configured device that is not plugged in right now selectable.
        const QString stored = m_settings.value(key).toString();
        if (!stored.isEmpty() && combo->findData(stored) < 0) {
            combo->addItem(tr("%1 (not connected)").arg(stored), stored);
        }
        bindComboData(combo, key);
        return combo;
    };

    auto *input = deviceCombo(true, kInputDevice, tr("Microphone"));
    auto *inputLabel = new QLabel(tr("&Microphone:"));
    inputLabel->setBuddy(input);
    form->addRow(inputLabel, input);
    auto *output = deviceCombo(false, kOutputDevice, tr("Speakers or headphones"));
    auto *outputLabel = new QLabel(tr("&Speakers or headphones:"));
    outputLabel->setBuddy(output);
    form->addRow(outputLabel, output);

    const auto percentSpin = [this](const QString &key, const QString &name, int maximum) {
        auto *spin = new QSpinBox;
        spin->setRange(0, maximum);
        spin->setSingleStep(5);
        spin->setSuffix(tr(" %"));
        spin->setAccessibleName(name);
        bindSpinBox(spin, key);
        return spin;
    };
    auto *inputVolume = percentSpin(kInputVolume, tr("Microphone volume, in percent"), 400);
    auto *inputVolumeLabel = new QLabel(tr("Microphone &volume:"));
    inputVolumeLabel->setBuddy(inputVolume);
    form->addRow(inputVolumeLabel, inputVolume);
    auto *outputVolume = percentSpin(kOutputVolume, tr("Voice volume, in percent"), 400);
    auto *outputVolumeLabel = new QLabel(tr("V&oice volume:"));
    outputVolumeLabel->setBuddy(outputVolume);
    form->addRow(outputVolumeLabel, outputVolume);
    form->addRow(QString(), helpText(tr("100% leaves the sound unchanged. Voice volume affects everyone you hear; "
                                        "set one person's volume from the channel list."),
                                     outputVolume));

    m_transmitMode = new QComboBox;
    m_transmitMode->setObjectName(QStringLiteral("transmitMode"));
    m_transmitMode->setAccessibleName(tr("How to talk"));
    m_transmitMode->addItem(tr("Push-to-talk: hold a key while talking"), kTransmitPushToTalk);
    m_transmitMode->addItem(tr("Voice activation: talk when the microphone hears you"), kTransmitVoiceActivation);
    m_transmitMode->addItem(tr("Always on: send the microphone all the time"), kTransmitContinuous);
    bindComboData(m_transmitMode, kTransmitMode);
    auto *modeLabel = new QLabel(tr("&How to talk:"));
    modeLabel->setBuddy(m_transmitMode);
    form->addRow(modeLabel, m_transmitMode);
    form->addRow(QString(), helpText(tr("In any mode, Ctrl+M mutes the microphone and Ctrl+Shift+M mutes everything "
                                        "you hear. In push-to-talk mode, Ctrl+T turns talking on until you press it "
                                        "again."),
                                     m_transmitMode));

    m_pushToTalkKey = new QLineEdit;
    m_pushToTalkKey->setReadOnly(true);
    m_pushToTalkKey->setAccessibleName(tr("Push-to-talk key"));
    auto *changeKey = new QPushButton(tr("C&hange key..."));
    auto *clearKey = new QPushButton(tr("Cl&ear key"));
    connect(changeKey, &QPushButton::clicked, this, [this] {
        if (const std::optional<QKeySequence> key = m_keyCapture ? m_keyCapture() : std::nullopt) {
            m_pendingPushToTalkKey = *key;
            updatePushToTalkKeyText();
            updateApplyButton();
        }
    });
    connect(clearKey, &QPushButton::clicked, this, [this] {
        m_pendingPushToTalkKey = QKeySequence();
        updatePushToTalkKeyText();
        updateApplyButton();
    });
    auto *keyRow = new QHBoxLayout;
    keyRow->addWidget(m_pushToTalkKey, 1);
    keyRow->addWidget(changeKey);
    keyRow->addWidget(clearKey);
    auto *keyLabel = new QLabel(tr("&Push-to-talk key:"));
    keyLabel->setBuddy(m_pushToTalkKey);
    form->addRow(keyLabel, keyRow);
#ifdef Q_OS_WIN
    const QString keyHelp = tr("The key works even while another program is in front. It still reaches that "
                               "program too, so choose a key it does not use, such as F12 or Control+Windows.");
#else
    const QString keyHelp = tr("On this system the key works while an OpenTalk window is in front.");
#endif
    form->addRow(QString(), helpText(keyHelp, m_pushToTalkKey));

    m_voiceLevel = new QSpinBox;
    m_voiceLevel->setRange(-80, 0);
    m_voiceLevel->setSuffix(tr(" dB"));
    m_voiceLevel->setAccessibleName(tr("Voice activation level, in decibels"));
    bindSpinBox(m_voiceLevel, kVoiceActivationLevel);
    auto *levelLabel = new QLabel(tr("Voice activation &level:"));
    levelLabel->setBuddy(m_voiceLevel);
    form->addRow(levelLabel, m_voiceLevel);
    m_levelText = new QLabel(m_audio != nullptr ? tr("Microphone level: not measured yet")
                                                : tr("Microphone level is not available here."));
    m_levelBar = new QProgressBar;
    m_levelBar->setRange(-80, 0);
    m_levelBar->setTextVisible(false);
    m_levelBar->setFocusPolicy(Qt::NoFocus);
    m_levelBar->setAccessibleName(tr("Microphone level"));
    form->addRow(QString(), m_levelBar);
    form->addRow(QString(), m_levelText);
    form->addRow(QString(), helpText(tr("Voice activation sends when the microphone is louder than this level. "
                                        "Speak normally and watch the microphone level below it: choose a level a "
                                        "little under your speaking level and above the background noise. A lower "
                                        "number (for example -55) is more sensitive."),
                                     m_voiceLevel));

    auto *bitrate = new QComboBox;
    bitrate->setAccessibleName(tr("Voice quality"));
    for (const auto &[kbps, text] :
         {std::pair{16, tr("Low (16 kbit/s): slow connections")}, std::pair{24, tr("Medium (24 kbit/s)")},
          std::pair{32, tr("Good (32 kbit/s): recommended")}, std::pair{48, tr("High (48 kbit/s)")},
          std::pair{64, tr("Very high (64 kbit/s): music")}, std::pair{96, tr("Maximum (96 kbit/s)")}}) {
        bitrate->addItem(text, kbps);
    }
    const int storedBitrate = m_settings.value(kVoiceBitrate).toInt();
    if (bitrate->findData(storedBitrate) < 0) {
        bitrate->addItem(tr("Custom (%1 kbit/s)").arg(storedBitrate), storedBitrate);
    }
    bindComboData(bitrate, kVoiceBitrate);
    auto *bitrateLabel = new QLabel(tr("Voice &quality:"));
    bitrateLabel->setBuddy(bitrate);
    form->addRow(bitrateLabel, bitrate);

    auto *noise = new QCheckBox(tr("Reduce background &noise"));
    bindCheckBox(noise, kNoiseSuppression);
    form->addRow(QString(), noise);
    form->addRow(QString(), helpText(tr("Lowers steady sounds such as fans and hum in what you send. Turn it off "
                                        "to send music or instruments unchanged."),
                                     noise));
    auto *autoGain = new QCheckBox(tr("Even out my voice level &automatically"));
    bindCheckBox(autoGain, kAutoGain);
    form->addRow(QString(), autoGain);
    form->addRow(QString(),
                 helpText(tr("Makes you louder when you speak quietly or sit far from the microphone."), autoGain));
    auto *echo = new QCheckBox(tr("&Remove echo when using speakers"));
    bindCheckBox(echo, kEchoCancellation);
    form->addRow(QString(), echo);
    form->addRow(QString(), helpText(tr("Stops other people hearing themselves when your microphone picks up your "
                                        "speakers. It cannot remove your screen reader's speech; for that, use "
                                        "headphones or push-to-talk. Works best with background noise reduction on."),
                                     echo));

    auto *udp = new QCheckBox(tr("Use the faster &UDP connection for voice when possible"));
    bindCheckBox(udp, kUseUdp);
    form->addRow(QString(), udp);
    form->addRow(QString(), helpText(tr("Turn this off only if voice keeps breaking up on a network that blocks UDP. "
                                        "Applies from the next connection."),
                                     udp));
    return pageWithHeading(tr("Audio"), form);
}

QWidget *SettingsDialog::createEffectsPage()
{
    using namespace storage::client;
    auto *form = new QFormLayout;
    form->addRow(new QLabel(tr("Effects change your voice before it is sent. They are all off unless you choose "
                               "them.")));

    auto *highPass = new QCheckBox(tr("Remove &rumble (low hum, desk knocks)"));
    bindCheckBox(highPass, kHighPass);
    form->addRow(QString(), highPass);
    form->addRow(QString(), helpText(tr("Cuts sounds below about 100 Hz, which voices do not need."), highPass));

    const auto dbSpin = [this](const QString &key, const QString &name, int minimum, int maximum,
                               const QString &suffix) {
        auto *spin = new QSpinBox;
        spin->setRange(minimum, maximum);
        spin->setSuffix(suffix);
        spin->setAccessibleName(name);
        bindSpinBox(spin, key);
        return spin;
    };
    auto *bass = dbSpin(kBassDb, tr("Bass, in decibels"), -12, 12, tr(" dB"));
    auto *bassLabel = new QLabel(tr("&Bass:"));
    bassLabel->setBuddy(bass);
    form->addRow(bassLabel, bass);
    auto *treble = dbSpin(kTrebleDb, tr("Treble, in decibels"), -12, 12, tr(" dB"));
    auto *trebleLabel = new QLabel(tr("&Treble:"));
    trebleLabel->setBuddy(treble);
    form->addRow(trebleLabel, treble);
    form->addRow(QString(), helpText(tr("0 dB leaves your voice unchanged. More treble can make speech clearer; "
                                        "less bass can reduce boominess."),
                                     treble));
    auto *pitch = dbSpin(kPitchSemitones, tr("Voice pitch, in semitones"), -12, 12, tr(" semitones"));
    auto *pitchLabel = new QLabel(tr("Voice &pitch:"));
    pitchLabel->setBuddy(pitch);
    form->addRow(pitchLabel, pitch);
    form->addRow(QString(), helpText(tr("A voice changer: positive values make your voice higher, negative values "
                                        "lower; 12 is a whole octave. It adds a little delay and roughness."),
                                     pitch));

    auto *mediaQuality = new QComboBox;
    mediaQuality->setAccessibleName(tr("Quality of files and computer audio you stream"));
    for (const auto &[kbps, text] :
         {std::pair{32, tr("Low (32 kbit/s)")}, std::pair{48, tr("Medium (48 kbit/s)")},
          std::pair{64, tr("Good (64 kbit/s): recommended")}, std::pair{96, tr("High (96 kbit/s)")},
          std::pair{128, tr("Maximum (128 kbit/s)")}}) {
        mediaQuality->addItem(text, kbps);
    }
    const int storedMediaBitrate = m_settings.value(kMediaBitrate).toInt();
    if (mediaQuality->findData(storedMediaBitrate) < 0) {
        mediaQuality->addItem(tr("Custom (%1 kbit/s)").arg(storedMediaBitrate), storedMediaBitrate);
    }
    bindComboData(mediaQuality, kMediaBitrate);
    auto *mediaQualityLabel = new QLabel(tr("Streaming &quality:"));
    mediaQualityLabel->setBuddy(mediaQuality);
    form->addRow(mediaQualityLabel, mediaQuality);
    form->addRow(QString(), helpText(tr("For files and computer audio you play into the channel. The server may "
                                        "set a lower limit."),
                                     mediaQuality));
    auto *mediaVolume = dbSpin(kMediaVolume, tr("Streaming volume, in percent"), 0, 200, tr(" %"));
    mediaVolume->setSingleStep(5);
    auto *mediaVolumeLabel = new QLabel(tr("Streaming &volume:"));
    mediaVolumeLabel->setBuddy(mediaVolume);
    form->addRow(mediaVolumeLabel, mediaVolume);
    auto *monitor = new QCheckBox(tr("&Hear files I stream"));
    bindCheckBox(monitor, kMediaMonitor);
    form->addRow(QString(), monitor);
    auto *muteMedia = new QCheckBox(tr("&Mute files other people stream"));
    bindCheckBox(muteMedia, kMuteIncomingMedia);
    form->addRow(QString(), muteMedia);
    auto *muteSystem = new QCheckBox(tr("Mute &computer audio other people share"));
    bindCheckBox(muteSystem, kMuteIncomingSystemAudio);
    form->addRow(QString(), muteSystem);
    form->addRow(QString(),
                 helpText(tr("Voices are never muted by these; use a person's volume for that."), muteSystem));
    return pageWithHeading(tr("Voice Effects and Media"), form);
}

QWidget *SettingsDialog::createSoundsPage()
{
    using namespace storage::client;
    auto *form = new QFormLayout;
    auto *enabled = new QCheckBox(tr("&Play sounds for events"));
    bindCheckBox(enabled, kSoundsEnabled);
    form->addRow(QString(), enabled);
    form->addRow(QString(), helpText(tr("Short sounds for connecting, people joining and leaving, messages and "
                                        "errors. Every event is also written to the history."),
                                     enabled));

    auto *volume = new QSpinBox;
    volume->setRange(0, 100);
    volume->setSingleStep(5);
    volume->setSuffix(tr(" %"));
    volume->setAccessibleName(tr("Sound volume, in percent"));
    bindSpinBox(volume, kSoundsVolume);
    auto *volumeLabel = new QLabel(tr("Sound &volume:"));
    volumeLabel->setBuddy(volume);
    form->addRow(volumeLabel, volume);

    auto *pushToTalk = new QCheckBox(tr("Play a &click when talking starts and stops"));
    bindCheckBox(pushToTalk, kPushToTalkSounds);
    form->addRow(QString(), pushToTalk);

    auto *pack = new QComboBox;
    pack->setAccessibleName(tr("Soundpack"));
    pack->addItem(tr("OpenTalk built-in sounds"), QString());
    if (m_audio != nullptr) {
        for (const QString &name : m_audio->availableSoundPacks()) {
            pack->addItem(name, name);
        }
    }
    const QString storedPack = m_settings.value(kSoundPack).toString();
    if (!storedPack.isEmpty() && pack->findData(storedPack) < 0) {
        pack->addItem(tr("%1 (not found)").arg(storedPack), storedPack);
    }
    bindComboData(pack, kSoundPack);
    auto *packLabel = new QLabel(tr("Sound&pack:"));
    packLabel->setBuddy(pack);
    form->addRow(packLabel, pack);
    const QString folder = QDir::toNativeSeparators(m_paths.dataRoot() + QStringLiteral("/soundpacks"));
    form->addRow(QString(), helpText(tr("A soundpack is a folder of WAV, FLAC or MP3 files inside %1, named after "
                                        "the events (for example user-joined-server.wav). Missing files use the "
                                        "built-in sound. See the user guide for the list of names.")
                                         .arg(folder),
                                     pack));
    auto *openFolder = new QPushButton(tr("&Open soundpacks folder"));
    connect(openFolder, &QPushButton::clicked, this, [folder] {
        QDir().mkpath(folder);
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    });
    auto *preview = new QPushButton(tr("P&lay a test sound"));
    preview->setEnabled(m_audio != nullptr);
    connect(preview, &QPushButton::clicked, this, [this] {
        if (m_audio != nullptr) {
            m_audio->playSound(audio::EventSound::UserJoinedServer);
        }
    });
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(openFolder);
    buttons->addWidget(preview);
    buttons->addStretch(1);
    form->addRow(QString(), buttons);
    return pageWithHeading(tr("Sounds"), form);
}

void SettingsDialog::updatePushToTalkKeyText()
{
    m_pushToTalkKey->setText(m_pendingPushToTalkKey.isEmpty() ? tr("None (use Ctrl+T to talk)")
                                                              : PushToTalkMonitor::describe(m_pendingPushToTalkKey));
}

void SettingsDialog::updateLevelMeter()
{
    if (m_audio == nullptr) {
        return;
    }
    const float level = m_audio->engine().inputLevelDb();
    const int rounded = static_cast<int>(std::lround(std::max(-80.0f, level)));
    m_levelBar->setValue(rounded);
    m_levelText->setText(
        level <= -80.0f
            ? tr("Microphone level: silent")
            : tr("Microphone level: %1 dB (voice activation at %2 dB)").arg(rounded).arg(m_voiceLevel->value()));
}

QWidget *SettingsDialog::createDiagnosticsPage()
{
    auto *logLevel = new QComboBox;
    logLevel->setObjectName(QStringLiteral("logLevel"));
    logLevel->addItem(tr("Warnings and errors only"), QStringLiteral("warning"));
    logLevel->addItem(tr("Normal"), QStringLiteral("info"));
    logLevel->addItem(tr("Detailed (for troubleshooting)"), QStringLiteral("debug"));
    bindComboData(logLevel, storage::client::kLogLevel);

    auto *logLabel = new QLabel(tr("&Log detail:"));
    logLabel->setBuddy(logLevel);
    logLevel->setAccessibleName(tr("Log detail"));

    auto *dataFolder = new QLineEdit(QDir::toNativeSeparators(m_paths.dataRoot()));
    dataFolder->setReadOnly(true);
    auto *dataLabel = new QLabel(tr("&Data folder (%1 mode):").arg(storage::toString(m_paths.mode())));
    dataLabel->setBuddy(dataFolder);
    dataFolder->setAccessibleName(tr("Data folder"));

    auto *form = new QFormLayout;
    form->addRow(logLabel, logLevel);
    form->addRow(QString(), helpText(tr("Logs never contain passwords or message contents. Detailed logging "
                                        "may include server addresses and user names."),
                                     logLevel));
    form->addRow(dataLabel, dataFolder);
    return pageWithHeading(tr("Logging and Diagnostics"), form);
}

void SettingsDialog::loadValues()
{
    for (const Binding &binding : std::as_const(m_bindings)) {
        binding.load();
    }
    m_pendingPushToTalkKey = QKeySequence::fromString(m_settings.value(storage::client::kPushToTalkKey).toString(),
                                                      QKeySequence::PortableText);
    updatePushToTalkKeyText();
}

bool SettingsDialog::hasChanges() const
{
    const QString storedKey = m_settings.value(storage::client::kPushToTalkKey).toString();
    return std::any_of(m_bindings.cbegin(), m_bindings.cend(), [](const Binding &b) { return b.changed(); }) ||
           m_pendingPushToTalkKey.toString(QKeySequence::PortableText) != storedKey;
}

void SettingsDialog::updateApplyButton()
{
    m_buttons->button(QDialogButtonBox::Apply)->setEnabled(hasChanges());
}

bool SettingsDialog::apply()
{
    for (const Binding &binding : std::as_const(m_bindings)) {
        binding.store();
    }
    m_settings.setValue(storage::client::kPushToTalkKey, m_pendingPushToTalkKey.toString(QKeySequence::PortableText));

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
