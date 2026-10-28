// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

namespace opentalk::moderation {

/// One line of the audit log: who did what to whom, where, why and when.
///
/// Entries form a hash chain: each stores the SHA-256 hash of the previous entry and its
/// own hash covers every field including that link. Changing, removing or reordering
/// an entry breaks the chain from that point on. Someone with write access to the
/// database can still rebuild a consistent chain; the chain shows tampering by anyone
/// who cannot, and exported copies kept elsewhere show it in any case.
struct AuditEntry {
    quint64 sequence = 0;      ///< 1, 2, 3 ... without gaps.
    qint64 timeMs = 0;         ///< UTC milliseconds since 1970.
    qint64 actorAccountId = 0; ///< 0 for the server itself or a guest.
    QString actorName;
    QString action; ///< Stable machine name, for example "sanction.ban" or "role.assign".
    QString target; ///< Who or what was acted on, as text.
    quint32 channelId = 0;
    QString reason;
    QString details; ///< Extra facts in "key=value; ..." form.
    QByteArray previousHash;
    QByteArray hash;
    friend bool operator==(const AuditEntry &, const AuditEntry &) = default;
};

/// SHA-256 over a canonical encoding of every field except `hash`.
QByteArray computeAuditHash(const AuditEntry &entry);

/// Fills in sequence, previousHash and hash for a new entry that follows `last`
/// (nullopt for the first entry).
AuditEntry chainAuditEntry(AuditEntry entry, const std::optional<AuditEntry> &last);

struct AuditVerification {
    bool intact = true;
    quint64 checked = 0;
    std::optional<quint64> firstBadSequence; ///< Where the chain breaks.
    QString problem;
};

/// Checks a run of consecutive entries. `before` is the entry preceding the first one,
/// if the run does not start at sequence 1.
AuditVerification verifyAuditChain(const QList<AuditEntry> &entries, const std::optional<AuditEntry> &before = {});

/// Plain-text export, one entry per line, ending with the hash of the last entry.
QString formatAuditEntry(const AuditEntry &entry);

} // namespace opentalk::moderation
