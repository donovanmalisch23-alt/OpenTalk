// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QString>

#include <memory>
#include <optional>

namespace opentalk::storage {

/// Where remembered passwords are kept: the operating system's protected store, never
/// OpenTalk's own files. On Windows that is Credential Manager (encrypted for the
/// signed-in Windows user, on this computer only). Elsewhere there is none yet, and
/// isAvailable() is false.
class CredentialStore {
public:
    virtual ~CredentialStore() = default;

    virtual bool isAvailable() const = 0;
    /// What the store is called, for the user ("Windows Credential Manager").
    virtual QString name() const = 0;
    /// `key` identifies the entry (OpenTalk uses the saved server's ID).
    virtual std::optional<QString> readPassword(const QString &key) const = 0;
    virtual bool writePassword(const QString &key, const QString &userName, const QString &password,
                               QString *errorMessage) = 0;
    /// Removing an entry that does not exist succeeds.
    virtual bool removePassword(const QString &key) = 0;
};

/// The store for this operating system (Credential Manager on Windows; unavailable elsewhere).
std::unique_ptr<CredentialStore> createSystemCredentialStore();

/// Keeps passwords in memory only, for tests.
class MemoryCredentialStore : public CredentialStore {
public:
    bool isAvailable() const override { return true; }
    QString name() const override { return QStringLiteral("test store"); }
    std::optional<QString> readPassword(const QString &key) const override;
    bool writePassword(const QString &key, const QString &userName, const QString &password,
                       QString *errorMessage) override;
    bool removePassword(const QString &key) override;
    int size() const { return static_cast<int>(m_passwords.size()); }

private:
    QHash<QString, QString> m_passwords;
};

} // namespace opentalk::storage
