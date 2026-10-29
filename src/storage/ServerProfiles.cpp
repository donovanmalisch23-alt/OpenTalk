// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/storage/ServerProfiles.h"

#include "opentalk/logging/Logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

namespace opentalk::storage {
namespace {

constexpr int kFormatVersion = 1;
constexpr qsizetype kMaxProfiles = 500;

QJsonObject toJson(const ServerProfile &p)
{
    return {{QStringLiteral("id"), p.id},
            {QStringLiteral("name"), p.name},
            {QStringLiteral("host"), p.host},
            {QStringLiteral("port"), p.port},
            {QStringLiteral("guest"), p.guest},
            {QStringLiteral("userName"), p.userName},
            {QStringLiteral("pinnedFingerprint"), p.pinnedFingerprint},
            {QStringLiteral("rememberPassword"), p.rememberPassword}};
}

std::optional<ServerProfile> fromJson(const QJsonValue &value)
{
    const QJsonObject o = value.toObject();
    const int port = o.value(QStringLiteral("port")).toInt(-1);
    if (!value.isObject() || port < 1 || port > 65535) {
        return std::nullopt;
    }
    ServerProfile p;
    p.id = o.value(QStringLiteral("id")).toString();
    p.name = o.value(QStringLiteral("name")).toString();
    p.host = o.value(QStringLiteral("host")).toString();
    p.port = static_cast<quint16>(port);
    p.guest = o.value(QStringLiteral("guest")).toBool(true);
    p.userName = o.value(QStringLiteral("userName")).toString();
    p.pinnedFingerprint = o.value(QStringLiteral("pinnedFingerprint")).toString();
    p.rememberPassword = o.value(QStringLiteral("rememberPassword")).toBool(false);
    if (!ServerProfileStore::isValid(p)) {
        return std::nullopt;
    }
    return p;
}

} // namespace

ServerProfileStore::ServerProfileStore(QString filePath)
    : m_filePath(std::move(filePath))
{
}

bool ServerProfileStore::isValid(const ServerProfile &p)
{
    return !p.id.isEmpty() && p.id.size() <= 64 && !p.name.trimmed().isEmpty() && p.name.size() <= 128 &&
           !p.host.trimmed().isEmpty() && p.host.size() <= 253 && p.port != 0 && p.userName.size() <= 32 &&
           p.pinnedFingerprint.size() <= 128;
}

QStringList ServerProfileStore::load()
{
    m_profiles.clear();
    QFile file(m_filePath);
    if (!file.exists()) {
        return {};
    }
    if (file.size() > 4 * 1024 * 1024 || !file.open(QIODevice::ReadOnly)) {
        return {QStringLiteral("Saved servers could not be read.")};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    const QJsonObject root = doc.object();
    if (!doc.isObject() || root.value(QStringLiteral("format")).toString() != QLatin1String("opentalk-servers") ||
        !root.value(QStringLiteral("profiles")).isArray()) {
        const QString backup = m_filePath + QStringLiteral(".corrupt-") +
                               QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        QFile::rename(m_filePath, backup);
        qCWarning(lcStorage) << "Server list was damaged; moved to" << backup;
        return {QStringLiteral("The saved server list was damaged and has been set aside as %1.")
                    .arg(QDir::toNativeSeparators(backup))};
    }
    QStringList warnings;
    for (const QJsonValue &entry : root.value(QStringLiteral("profiles")).toArray()) {
        if (m_profiles.size() >= kMaxProfiles) {
            break;
        }
        if (const auto profile = fromJson(entry)) {
            m_profiles.append(*profile);
        } else {
            warnings.append(QStringLiteral("A saved server entry was invalid and was skipped."));
        }
    }
    return warnings;
}

bool ServerProfileStore::save(QString *errorMessage)
{
    QJsonArray profiles;
    for (const ServerProfile &p : std::as_const(m_profiles)) {
        profiles.append(toJson(p));
    }
    const QJsonObject root{{QStringLiteral("format"), QStringLiteral("opentalk-servers")},
                           {QStringLiteral("formatVersion"), kFormatVersion},
                           {QStringLiteral("profiles"), profiles}};
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    return true;
}

std::optional<ServerProfile> ServerProfileStore::find(const QString &id) const
{
    for (const ServerProfile &p : m_profiles) {
        if (p.id == id) {
            return p;
        }
    }
    return std::nullopt;
}

QString ServerProfileStore::upsert(ServerProfile profile)
{
    if (profile.id.isEmpty()) {
        profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    for (ServerProfile &existing : m_profiles) {
        if (existing.id == profile.id) {
            existing = profile;
            return profile.id;
        }
    }
    m_profiles.append(profile);
    return profile.id;
}

bool ServerProfileStore::remove(const QString &id)
{
    return m_profiles.removeIf([&id](const ServerProfile &p) { return p.id == id; }) > 0;
}

} // namespace opentalk::storage
