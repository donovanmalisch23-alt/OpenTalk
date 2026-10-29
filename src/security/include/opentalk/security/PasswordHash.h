// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <optional>

namespace opentalk::security {

/// Cost settings for Argon2id. Stored inside each hash, so they can be raised later and
/// old hashes still verify (and can be upgraded on the next successful login).
struct PasswordHashCost {
    unsigned long long operations = 0;
    std::size_t memoryBytes = 0;

    /// libsodium's "interactive" level: about 64 MiB and well under a second per hash on
    /// a modern PC. Used for real accounts.
    static PasswordHashCost interactive();
    /// The minimum libsodium allows. Only for tests, where speed matters more than strength.
    static PasswordHashCost minimumForTests();
};

/// Longest password accepted, in UTF-16 code units. Prevents hashing huge inputs.
inline constexpr qsizetype kMaxPasswordLength = 1024;

/// Initialises libsodium. Safe to call more than once; returns false if it cannot start.
bool initializePasswordHashing();

/// Hashes a password with Argon2id. The result is a self-describing ASCII string
/// ("$argon2id$v=19$m=...") that includes a random salt. Passwords are normalised to
/// Unicode NFC first, so the same visible password always hashes the same way.
/// Returns nullopt for empty or over-long passwords, or if hashing fails (out of memory).
std::optional<QByteArray> hashPassword(const QString &password, PasswordHashCost cost);

/// Checks a password against a stored hash in constant time.
bool verifyPassword(const QByteArray &storedHash, const QString &password);

/// True if the stored hash used weaker settings than `cost` and should be replaced.
bool hashNeedsUpgrade(const QByteArray &storedHash, PasswordHashCost cost);

} // namespace opentalk::security
