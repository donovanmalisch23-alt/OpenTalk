// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/moderation/Permissions.h"

#include <QHostAddress>
#include <QList>
#include <QString>

#include <optional>

namespace opentalk::moderation {

/// Values are stored and sent over the network: never renumber them.
enum class SanctionType : quint8 {
    Warning = 0,   ///< A recorded warning; nothing is blocked.
    VoiceMute = 1, ///< Cannot talk or stream audio.
    TextMute = 2,  ///< Cannot send messages.
    Mute = 3,      ///< Both.
    Kick = 4,      ///< Removed from the server (or from a channel) once.
    Ban = 5,       ///< Cannot log in (server scope) or enter the channel (channel scope).
    Restrict = 6,  ///< Treated as the Restricted role: can join channels, nothing else.
};

enum class SanctionState : quint8 {
    Active = 0,
    Expired = 1,
    Revoked = 2,    ///< Lifted early by a moderator.
    Overturned = 3, ///< Cancelled after an appeal.
};

QString toString(SanctionType type);
QString toString(SanctionState state);

struct Sanction {
    quint64 id = 0;
    SanctionType type = SanctionType::Warning;
    quint32 channelId = 0;      ///< 0 = the whole server; otherwise the channel and its sub-channels.
    qint64 targetAccountId = 0; ///< 0 for a guest.
    QString targetName;         ///< Name at the time (for display).
    QString targetAddress;      ///< Guests, and address bans: the IP address that is blocked.
    RoleId targetRole = roles::kGuest;
    qint64 actorAccountId = 0; ///< 0 for the server itself.
    QString actorName;
    QString reason;
    qint64 createdMs = 0; ///< UTC milliseconds since 1970.
    qint64 expiresMs = 0; ///< 0 = never (permanent).
    SanctionState state = SanctionState::Active;
    QString resolvedBy;
    QString resolution;
    qint64 resolvedMs = 0;
    friend bool operator==(const Sanction &, const Sanction &) = default;

    /// Warnings and kicks are events: they are recorded but block nothing afterwards.
    bool isLasting() const { return type != SanctionType::Warning && type != SanctionType::Kick; }
    bool isActiveAt(qint64 nowMs) const
    {
        return state == SanctionState::Active && isLasting() && (expiresMs == 0 || nowMs < expiresMs);
    }
};

/// Who might be affected: an account (accountId > 0) or a guest (accountId 0).
struct SanctionSubject {
    qint64 accountId = 0;
    QString address;
};

/// Rules every new sanction must follow, checked before it is stored.
struct SanctionPolicy {
    qint64 maxTemporaryBanMs = 30LL * 24 * 60 * 60 * 1000; ///< Longest ban "user.ban.temporary" allows.
    qint64 longMuteMs = 60LL * 60 * 1000;                  ///< Mutes longer than this need a reason.
};

/// What a new sanction needs: the capability that allows it and whether a reason is
/// required. Returns an English explanation when the request is not acceptable.
struct SanctionRequirements {
    Capability capability = Capability::UserWarn;
    bool reasonRequired = false;
    std::optional<QString> problem;
};
SanctionRequirements requirementsFor(SanctionType type, qint64 durationMs, bool banAddress,
                                     const SanctionPolicy &policy = {});

/// The active sanctions of a server, with the queries the server needs. Pure logic.
class SanctionBook {
public:
    void setSanctions(const QList<Sanction> &sanctions);
    const QList<Sanction> &sanctions() const { return m_sanctions; }
    void add(const Sanction &sanction);
    /// Changes the state of an active sanction. Returns false if it is not active.
    bool resolve(quint64 id, SanctionState state, const QString &by, const QString &resolution, qint64 nowMs);
    std::optional<Sanction> find(quint64 id) const;
    /// Marks sanctions whose time is up as expired and returns them.
    QList<Sanction> expire(qint64 nowMs);

    /// A ban that applies: server-wide if `channelChain` is empty, else for the channel
    /// or any of its parents.
    std::optional<Sanction> activeBan(const SanctionSubject &subject, const QList<quint32> &channelChain,
                                      qint64 nowMs) const;
    bool isVoiceMuted(const SanctionSubject &subject, const QList<quint32> &channelChain, qint64 nowMs) const;
    bool isTextMuted(const SanctionSubject &subject, const QList<quint32> &channelChain, qint64 nowMs) const;
    bool isRestricted(const SanctionSubject &subject, qint64 nowMs) const;
    QList<Sanction> activeFor(const SanctionSubject &subject, qint64 nowMs) const;

    static bool appliesTo(const Sanction &sanction, const SanctionSubject &subject);

private:
    std::optional<Sanction> firstActive(const SanctionSubject &subject, const QList<quint32> &channelChain,
                                        qint64 nowMs, const QList<SanctionType> &types, bool includeServer) const;
    QList<Sanction> m_sanctions;
};

} // namespace opentalk::moderation
