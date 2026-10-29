// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>
#include <QtGlobal>

namespace opentalk::moderation {

/// A report about a person, for moderators. The reported person never learns who sent it.
struct Report {
    enum class State : quint8 { Open = 0, ActionTaken = 1, Dismissed = 2 };
    quint64 id = 0;
    qint64 reporterAccountId = 0;
    QString reporterName;
    qint64 targetAccountId = 0;
    QString targetName;
    QString targetAddress; ///< Kept for moderators acting on guests; never shown to the reporter.
    QString reason;
    qint64 createdMs = 0;
    State state = State::Open;
    QString handledBy;
    QString resolution;
    qint64 handledMs = 0;
    friend bool operator==(const Report &, const Report &) = default;
};

/// A request to reconsider a sanction. One per sanction.
struct Appeal {
    enum class State : quint8 { Open = 0, Upheld = 1, Reduced = 2, Overturned = 3 };
    quint64 id = 0;
    quint64 sanctionId = 0;
    qint64 appellantAccountId = 0;
    QString appellantName;
    QString text;
    qint64 createdMs = 0;
    State state = State::Open;
    QString decidedBy;
    QString decisionReason;
    qint64 decidedMs = 0;
    friend bool operator==(const Appeal &, const Appeal &) = default;
};

QString toString(Report::State state);
QString toString(Appeal::State state);

/// Separation of duties: the moderator who issued a sanction may not decide its appeal
/// while someone else is able to. (A one-person server cannot follow this rule.)
inline bool mayDecideAppeal(qint64 deciderAccountId, qint64 issuerAccountId, bool someoneElseCanDecide)
{
    return deciderAccountId != issuerAccountId || issuerAccountId == 0 || !someoneElseCanDecide;
}

} // namespace opentalk::moderation
