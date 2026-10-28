// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/moderation/Audit.h"

#include <QCborArray>
#include <QCborValue>
#include <QCryptographicHash>
#include <QDateTime>

namespace opentalk::moderation {

QByteArray computeAuditHash(const AuditEntry &e)
{
    // A CBOR array of plain values has exactly one encoding, so the hash is stable.
    const QCborArray fields{QStringLiteral("opentalk-audit-v1"),
                            static_cast<qint64>(e.sequence),
                            e.timeMs,
                            e.actorAccountId,
                            e.actorName,
                            e.action,
                            e.target,
                            static_cast<qint64>(e.channelId),
                            e.reason,
                            e.details,
                            e.previousHash};
    return QCryptographicHash::hash(QCborValue(fields).toCbor(), QCryptographicHash::Sha256);
}

AuditEntry chainAuditEntry(AuditEntry entry, const std::optional<AuditEntry> &last)
{
    entry.sequence = last ? last->sequence + 1 : 1;
    entry.previousHash = last ? last->hash : QByteArray();
    entry.hash = computeAuditHash(entry);
    return entry;
}

AuditVerification verifyAuditChain(const QList<AuditEntry> &entries, const std::optional<AuditEntry> &before)
{
    AuditVerification result;
    std::optional<AuditEntry> previous = before;
    for (const AuditEntry &e : entries) {
        const quint64 expectedSequence = previous ? previous->sequence + 1 : 1;
        const QByteArray expectedLink = previous ? previous->hash : QByteArray();
        QString problem;
        if (e.sequence != expectedSequence) {
            problem = QStringLiteral("entry %1 follows entry %2; an entry is missing or out of order")
                          .arg(e.sequence)
                          .arg(previous ? previous->sequence : 0);
        } else if (e.previousHash != expectedLink) {
            problem = QStringLiteral("entry %1 does not link to the entry before it").arg(e.sequence);
        } else if (e.hash != computeAuditHash(e)) {
            problem = QStringLiteral("entry %1 was changed after it was written").arg(e.sequence);
        }
        if (!problem.isEmpty()) {
            result.intact = false;
            result.firstBadSequence = e.sequence;
            result.problem = problem;
            return result;
        }
        ++result.checked;
        previous = e;
    }
    return result;
}

QString formatAuditEntry(const AuditEntry &e)
{
    QString line = QStringLiteral("#%1 %2 %3 (%4) %5")
                       .arg(e.sequence)
                       .arg(QDateTime::fromMSecsSinceEpoch(e.timeMs).toUTC().toString(Qt::ISODate))
                       .arg(e.actorName.isEmpty() ? QStringLiteral("server") : e.actorName)
                       .arg(e.actorAccountId)
                       .arg(e.action);
    if (!e.target.isEmpty()) {
        line += QStringLiteral(" target=\"%1\"").arg(e.target);
    }
    if (e.channelId != 0) {
        line += QStringLiteral(" channel=%1").arg(e.channelId);
    }
    if (!e.reason.isEmpty()) {
        line += QStringLiteral(" reason=\"%1\"").arg(e.reason);
    }
    if (!e.details.isEmpty()) {
        line += QStringLiteral(" %1").arg(e.details);
    }
    line += QStringLiteral(" hash=%1").arg(QString::fromLatin1(e.hash.toHex()));
    return line;
}

} // namespace opentalk::moderation
