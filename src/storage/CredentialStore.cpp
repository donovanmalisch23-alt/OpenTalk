// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/storage/CredentialStore.h"

#ifdef Q_OS_WIN
// <windows.h> must come before <wincred.h>; the blank line keeps clang-format from sorting them.
#include <windows.h>

#include <wincred.h>
#endif

#include <string>
#include <vector>

namespace opentalk::storage {
namespace {

#ifdef Q_OS_WIN

/// Entries are named "OpenTalk/<key>", so they are easy to recognise (and remove) in
/// Control Panel, Credential Manager.
std::wstring targetName(const QString &key)
{
    return (QStringLiteral("OpenTalk/") + key).toStdWString();
}

class WindowsCredentialStore : public CredentialStore {
public:
    bool isAvailable() const override { return true; }
    QString name() const override { return QStringLiteral("Windows Credential Manager"); }

    std::optional<QString> readPassword(const QString &key) const override
    {
        const std::wstring target = targetName(key);
        PCREDENTIALW credential = nullptr;
        if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
            return std::nullopt;
        }
        std::optional<QString> password;
        if (credential->CredentialBlobSize % sizeof(char16_t) == 0) {
            password = QString::fromUtf16(reinterpret_cast<const char16_t *>(credential->CredentialBlob),
                                          static_cast<qsizetype>(credential->CredentialBlobSize / sizeof(char16_t)));
        }
        SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
        CredFree(credential);
        return password;
    }

    bool writePassword(const QString &key, const QString &userName, const QString &password,
                       QString *errorMessage) override
    {
        const std::wstring target = targetName(key);
        std::wstring user = userName.toStdWString();
        std::wstring comment = L"OpenTalk server password";
        const auto *chars = reinterpret_cast<const char16_t *>(password.constData());
        std::vector<char16_t> blob(chars, chars + password.size());
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = const_cast<LPWSTR>(target.c_str());
        credential.Comment = comment.data();
        credential.CredentialBlobSize = static_cast<DWORD>(blob.size() * sizeof(char16_t));
        credential.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE; // this computer only, never roaming
        credential.UserName = user.data();
        const bool ok = CredWriteW(&credential, 0) != FALSE;
        const DWORD error = ok ? 0 : GetLastError();
        SecureZeroMemory(blob.data(), blob.size() * sizeof(char16_t));
        if (!ok && errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Windows Credential Manager refused it (error %1)").arg(error);
        }
        return ok;
    }

    bool removePassword(const QString &key) override
    {
        const std::wstring target = targetName(key);
        return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) != FALSE || GetLastError() == ERROR_NOT_FOUND;
    }
};

#else

class UnavailableCredentialStore : public CredentialStore {
public:
    bool isAvailable() const override { return false; }
    QString name() const override { return {}; }
    std::optional<QString> readPassword(const QString &) const override { return std::nullopt; }
    bool writePassword(const QString &, const QString &, const QString &, QString *errorMessage) override
    {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("remembering passwords is only available on Windows so far");
        }
        return false;
    }
    bool removePassword(const QString &) override { return true; }
};

#endif

} // namespace

std::unique_ptr<CredentialStore> createSystemCredentialStore()
{
#ifdef Q_OS_WIN
    return std::make_unique<WindowsCredentialStore>();
#else
    return std::make_unique<UnavailableCredentialStore>();
#endif
}

std::optional<QString> MemoryCredentialStore::readPassword(const QString &key) const
{
    const auto it = m_passwords.constFind(key);
    return it == m_passwords.cend() ? std::nullopt : std::optional<QString>(*it);
}

bool MemoryCredentialStore::writePassword(const QString &key, const QString &, const QString &password, QString *)
{
    m_passwords.insert(key, password);
    return true;
}

bool MemoryCredentialStore::removePassword(const QString &key)
{
    m_passwords.remove(key);
    return true;
}

} // namespace opentalk::storage
