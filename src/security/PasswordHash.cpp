// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/security/PasswordHash.h"

#include <sodium.h>

#include <array>

namespace opentalk::security {
namespace {

QByteArray preparedPassword(const QString &password)
{
    return password.normalized(QString::NormalizationForm_C).toUtf8();
}

} // namespace

PasswordHashCost PasswordHashCost::interactive()
{
    return {crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE};
}

PasswordHashCost PasswordHashCost::minimumForTests()
{
    return {crypto_pwhash_OPSLIMIT_MIN, crypto_pwhash_MEMLIMIT_MIN};
}

bool initializePasswordHashing()
{
    return sodium_init() >= 0; // 0 = initialised now, 1 = already initialised
}

std::optional<QByteArray> hashPassword(const QString &password, PasswordHashCost cost)
{
    if (password.isEmpty() || password.size() > kMaxPasswordLength || !initializePasswordHashing()) {
        return std::nullopt;
    }
    const QByteArray utf8 = preparedPassword(password);
    std::array<char, crypto_pwhash_STRBYTES> out{};
    if (crypto_pwhash_str_alg(out.data(), utf8.constData(), static_cast<unsigned long long>(utf8.size()),
                              cost.operations, cost.memoryBytes, crypto_pwhash_ALG_ARGON2ID13) != 0) {
        return std::nullopt;
    }
    return QByteArray(out.data());
}

bool verifyPassword(const QByteArray &storedHash, const QString &password)
{
    if (password.isEmpty() || password.size() > kMaxPasswordLength || storedHash.isEmpty() ||
        storedHash.size() >= static_cast<qsizetype>(crypto_pwhash_STRBYTES) || !initializePasswordHashing()) {
        return false;
    }
    // crypto_pwhash_str_verify needs a NUL-terminated string; QByteArray provides one.
    const QByteArray utf8 = preparedPassword(password);
    return crypto_pwhash_str_verify(storedHash.constData(), utf8.constData(),
                                    static_cast<unsigned long long>(utf8.size())) == 0;
}

bool hashNeedsUpgrade(const QByteArray &storedHash, PasswordHashCost cost)
{
    if (storedHash.isEmpty() || storedHash.size() >= static_cast<qsizetype>(crypto_pwhash_STRBYTES)) {
        return true;
    }
    return crypto_pwhash_str_needs_rehash(storedHash.constData(), cost.operations, cost.memoryBytes) != 0;
}

} // namespace opentalk::security
