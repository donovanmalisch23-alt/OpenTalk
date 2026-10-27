// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace opentalk::storage {

/// A saved server. Passwords are never stored here; until secure storage (Windows
/// Credential Manager) is added, the app asks for them when connecting.
struct ServerProfile {
    QString id; ///< Stable random identifier.
    QString name;
    QString host;
    quint16 port = 10350;
    bool guest = true;
    QString userName; ///< Account username, or guest nickname.
    /// Certificate fingerprint the user trusted (empty until the first connection).
    QString pinnedFingerprint;

    friend bool operator==(const ServerProfile &, const ServerProfile &) = default;
};

/// Saved servers in "<data>/config/server-profiles.json": validated on load, saved
/// atomically, and a damaged file is moved aside rather than lost.
class ServerProfileStore {
public:
    explicit ServerProfileStore(QString filePath);

    /// Returns warnings about anything that could not be loaded.
    QStringList load();
    bool save(QString *errorMessage = nullptr);

    const QList<ServerProfile> &profiles() const { return m_profiles; }
    std::optional<ServerProfile> find(const QString &id) const;
    /// Adds or replaces (by id) a profile. A missing id is generated. Returns the id.
    QString upsert(ServerProfile profile);
    bool remove(const QString &id);

    static bool isValid(const ServerProfile &profile);

private:
    QString m_filePath;
    QList<ServerProfile> m_profiles;
};

} // namespace opentalk::storage
