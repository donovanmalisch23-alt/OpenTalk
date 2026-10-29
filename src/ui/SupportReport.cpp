// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/ui/SupportReport.h"

#include "opentalk/storage/SettingsStore.h"

#include <QDir>
#include <QFile>
#include <QVariantMap>

#include <algorithm>

namespace opentalk::ui {
namespace {

constexpr qint64 kMaxLogBytes = 256 * 1024;

QString hideHomeFolder(QString text, const QString &home)
{
    if (home.isEmpty() || home == QStringLiteral("/")) {
        return text;
    }
#ifdef Q_OS_WIN
    const QString placeholder = QStringLiteral("%USERPROFILE%");
    const Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    const QString placeholder = QStringLiteral("~");
    const Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    // Both spellings: Qt's forward slashes and Windows' backslashes.
    text.replace(QDir::toNativeSeparators(home), placeholder, sensitivity);
    text.replace(QDir::fromNativeSeparators(home), placeholder, sensitivity);
    return text;
}

void addList(QStringList &lines, const QString &title, const QStringList &items)
{
    if (items.isEmpty()) {
        lines.append(title + QStringLiteral(": none"));
        return;
    }
    lines.append(title + QStringLiteral(":"));
    for (const QString &item : items) {
        lines.append(QStringLiteral("- ") + item);
    }
}

} // namespace

QString buildSupportReport(const SupportReportInput &input, const QDateTime &createdUtc)
{
    QStringList lines;
    lines << QStringLiteral("OpenTalk support file")
          << QStringLiteral("Created: %1 UTC").arg(createdUtc.toString(QStringLiteral("yyyy-MM-dd HH:mm")))
          << QStringLiteral("It contains no passwords, messages or server addresses, but it does show your "
                            "settings, sound devices, plugins and the end of the log. Read it before you share it.")
          << QString();

    lines << QStringLiteral("== OpenTalk ==") << QStringLiteral("Version: ") + input.version
          << QStringLiteral("Qt: ") + input.qtVersion << QStringLiteral("TLS library: ") + input.tlsLibrary
          << QStringLiteral("Operating system: ") + input.operatingSystem
          << QStringLiteral("Data folder (%1 mode): %2")
                 .arg(input.dataMode, hideHomeFolder(input.dataFolder, input.homeFolder))
          << QString();

    lines << QStringLiteral("== Connection and sound ==") << QStringLiteral("Connection: ") + input.connection
          << QStringLiteral("Voice: ") + input.voiceStatus;
    addList(lines, QStringLiteral("Microphones"), input.inputDevices);
    addList(lines, QStringLiteral("Speakers and headphones"), input.outputDevices);
    lines << QString();

    lines << QStringLiteral("== Plugins ==");
    addList(lines, QStringLiteral("Installed"), input.plugins);
    lines << QString();

    lines << QStringLiteral("== Settings ==");
    for (const auto &[key, value] : input.settings) {
        lines << QStringLiteral("%1 = %2").arg(key, hideHomeFolder(value, input.homeFolder));
    }
    lines << QString();

    lines << QStringLiteral("== End of the log ==");
    lines << (input.log.isEmpty() ? QStringLiteral("(the log could not be read)")
                                  : hideHomeFolder(input.log, input.homeFolder));
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QList<QPair<QString, QString>> describeSettings(const storage::SettingsStore &settings, const QStringList &privateKeys)
{
    QList<QPair<QString, QString>> result;
    for (const storage::SettingDefinition &definition : settings.schema().definitions()) {
        const QVariant value = settings.value(definition.key);
        QString shown;
        if (privateKeys.contains(definition.key)) {
            shown = QStringLiteral("(left out)");
        } else {
            switch (definition.type) {
            case storage::SettingType::Bool:
                shown = value.toBool() ? QStringLiteral("on") : QStringLiteral("off");
                break;
            case storage::SettingType::ByteArray:
                shown = QStringLiteral("(%1 bytes)").arg(value.toByteArray().size());
                break;
            case storage::SettingType::IntMap:
                shown = QStringLiteral("(%1 entries)").arg(value.toMap().size());
                break;
            case storage::SettingType::Int:
            case storage::SettingType::String:
                shown = value.toString();
                if (shown.isEmpty()) {
                    shown = QStringLiteral("(empty)");
                }
                break;
            }
        }
        result.append({definition.key, shown});
    }
    std::sort(result.begin(), result.end());
    return result;
}

QString readLogTail(const QString &path, int maxLines)
{
    QFile file(path);
    if (maxLines <= 0 || !file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const qint64 start = std::max<qint64>(0, file.size() - kMaxLogBytes);
    file.seek(start);
    QString text = QString::fromUtf8(file.readAll());
    if (start > 0) {
        // The first line is probably cut off in the middle.
        text = text.mid(text.indexOf(QLatin1Char('\n')) + 1);
    }
    QStringList lines = text.split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.constLast().trimmed().isEmpty()) {
        lines.removeLast();
    }
    if (lines.size() > maxLines) {
        lines = lines.mid(lines.size() - maxLines);
    }
    for (QString &line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace opentalk::ui
