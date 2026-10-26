// SPDX-License-Identifier: GPL-3.0-only
// Application entry point: parses the command line, decides where data lives, starts
// logging, loads settings and shows the main window. All wiring between components
// happens here so the components themselves stay independent.

#include "opentalk/logging/Logging.h"
#include "opentalk/storage/AppPaths.h"
#include "opentalk/storage/ClientSettings.h"
#include "opentalk/storage/SettingsStore.h"
#include "opentalk/ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QMessageBox>

namespace {

using namespace opentalk;

logging::Level configuredLevel(const storage::SettingsStore &settings)
{
    return logging::levelFromString(settings.value(storage::client::kLogLevel).toString())
        .value_or(logging::Level::Info);
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("OpenTalk"));
    QApplication::setOrganizationName(QStringLiteral("OpenTalk"));
    QApplication::setApplicationVersion(QStringLiteral(OPENTALK_VERSION_STRING));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("OpenTalk voice and text communication client"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption dataDirOption(QStringLiteral("data-dir"),
                                           QStringLiteral("Store settings and logs in <directory>."),
                                           QStringLiteral("directory"));
    parser.addOption(dataDirOption);
    parser.process(app);

    const storage::AppPaths paths =
        storage::AppPaths::resolve(QApplication::applicationDirPath(), parser.value(dataDirOption));
    QString pathError;
    if (!paths.ensureDirectories(&pathError)) {
        QMessageBox::critical(
            nullptr, QStringLiteral("OpenTalk"),
            QStringLiteral("OpenTalk cannot start because its data folder is not usable.\n\n%1").arg(pathError));
        return 1;
    }

    // Start logging at the default level so problems while loading settings are recorded.
    logging::install({paths.clientLogFile(), logging::Level::Info, true});
    qCInfo(lcApp).noquote() << "OpenTalk" << QApplication::applicationVersion() << "starting; data folder"
                            << QDir::toNativeSeparators(paths.dataRoot()) << "(" << storage::toString(paths.mode())
                            << "mode)";

    storage::SettingsStore settings(storage::client::schema(), paths.clientSettingsFile());
    const storage::LoadReport report = settings.load();
    logging::setMinimumLevel(configuredLevel(settings));
    QObject::connect(&settings, &storage::SettingsStore::valueChanged, &settings, [&settings](const QString &key) {
        if (key == storage::client::kLogLevel) {
            logging::setMinimumLevel(configuredLevel(settings));
        }
    });

    ui::MainWindow window(settings, paths);
    window.show();
    if (!report.warnings.isEmpty()) {
        // Shown after the window so focus returns to it when the message is dismissed.
        QMessageBox::warning(&window, QStringLiteral("OpenTalk settings"),
                             report.warnings.join(QStringLiteral("\n\n")));
    }

    const int exitCode = QApplication::exec();
    qCInfo(lcApp) << "OpenTalk exiting with code" << exitCode;
    logging::shutdown();
    return exitCode;
}
