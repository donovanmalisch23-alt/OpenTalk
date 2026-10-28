// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/moderation/Sanctions.h"

namespace opentalk::moderation {

QString toString(SanctionType type)
{
    switch (type) {
    case SanctionType::Warning:
        return QStringLiteral("warning");
    case SanctionType::VoiceMute:
        return QStringLiteral("voice mute");
    case SanctionType::TextMute:
        return QStringLiteral("text mute");
    case SanctionType::Mute:
        return QStringLiteral("mute");
    case SanctionType::Kick:
        return QStringLiteral("kick");
    case SanctionType::Ban:
        return QStringLiteral("ban");
    case SanctionType::Restrict:
        return QStringLiteral("restriction");
    }
    return QStringLiteral("sanction");
}

QString toString(SanctionState state)
{
    switch (state) {
    case SanctionState::Active:
        return QStringLiteral("active");
    case SanctionState::Expired:
        return QStringLiteral("expired");
    case SanctionState::Revoked:
        return QStringLiteral("lifted");
    case SanctionState::Overturned:
        return QStringLiteral("overturned on appeal");
    }
    return QStringLiteral("unknown");
}

SanctionRequirements requirementsFor(SanctionType type, qint64 durationMs, bool banAddress,
                                     const SanctionPolicy &policy)
{
    SanctionRequirements r;
    if (durationMs < 0) {
        r.problem = QStringLiteral("the duration cannot be negative");
        return r;
    }
    switch (type) {
    case SanctionType::Warning:
        r.capability = Capability::UserWarn;
        break;
    case SanctionType::Kick:
        r.capability = Capability::UserKick;
        break;
    case SanctionType::VoiceMute:
    case SanctionType::TextMute:
    case SanctionType::Mute:
        r.capability = Capability::UserMute;
        r.reasonRequired = durationMs == 0 || durationMs > policy.longMuteMs;
        break;
    case SanctionType::Ban:
        r.reasonRequired = true;
        if (banAddress) {
            r.capability = Capability::UserBanAddress;
        } else if (durationMs == 0 || durationMs > policy.maxTemporaryBanMs) {
            r.capability = Capability::UserBanPermanent;
        } else {
            r.capability = Capability::UserBanTemporary;
        }
        break;
    case SanctionType::Restrict:
        r.capability = Capability::UserMute;
        r.reasonRequired = true;
        break;
    }
    if (banAddress && type != SanctionType::Ban) {
        r.problem = QStringLiteral("only bans can block an address");
    }
    return r;
}

void SanctionBook::setSanctions(const QList<Sanction> &sanctions)
{
    m_sanctions = sanctions;
}

void SanctionBook::add(const Sanction &sanction)
{
    m_sanctions.append(sanction);
}

std::optional<Sanction> SanctionBook::find(quint64 id) const
{
    for (const Sanction &s : m_sanctions) {
        if (s.id == id) {
            return s;
        }
    }
    return std::nullopt;
}

bool SanctionBook::resolve(quint64 id, SanctionState state, const QString &by, const QString &resolution, qint64 nowMs)
{
    for (Sanction &s : m_sanctions) {
        if (s.id == id && s.state == SanctionState::Active) {
            s.state = state;
            s.resolvedBy = by;
            s.resolution = resolution;
            s.resolvedMs = nowMs;
            return true;
        }
    }
    return false;
}

QList<Sanction> SanctionBook::expire(qint64 nowMs)
{
    QList<Sanction> expired;
    for (Sanction &s : m_sanctions) {
        if (s.state == SanctionState::Active && s.isLasting() && s.expiresMs != 0 && nowMs >= s.expiresMs) {
            s.state = SanctionState::Expired;
            s.resolvedMs = s.expiresMs;
            expired.append(s);
        }
    }
    return expired;
}

bool SanctionBook::appliesTo(const Sanction &sanction, const SanctionSubject &subject)
{
    if (sanction.targetAccountId != 0 && sanction.targetAccountId == subject.accountId) {
        return true;
    }
    if (sanction.targetAddress.isEmpty() || subject.address.isEmpty()) {
        return false;
    }
    // A sanction on a guest applies to guests from that address. An address ban on an
    // account also applies to everyone else from that address.
    const bool guestSanction = sanction.targetAccountId == 0;
    if (guestSanction ? subject.accountId != 0 : sanction.type != SanctionType::Ban) {
        return false;
    }
    return QHostAddress(sanction.targetAddress)
        .isEqual(QHostAddress(subject.address), QHostAddress::TolerantConversion);
}

std::optional<Sanction> SanctionBook::firstActive(const SanctionSubject &subject, const QList<quint32> &channelChain,
                                                  qint64 nowMs, const QList<SanctionType> &types,
                                                  bool includeServer) const
{
    for (const Sanction &s : m_sanctions) {
        if (!types.contains(s.type) || !s.isActiveAt(nowMs) || !appliesTo(s, subject)) {
            continue;
        }
        if ((s.channelId == 0 && includeServer) || (s.channelId != 0 && channelChain.contains(s.channelId))) {
            return s;
        }
    }
    return std::nullopt;
}

std::optional<Sanction> SanctionBook::activeBan(const SanctionSubject &subject, const QList<quint32> &channelChain,
                                                qint64 nowMs) const
{
    // A server ban is checked at login; a channel question only looks at channel bans.
    return firstActive(subject, channelChain, nowMs, {SanctionType::Ban}, channelChain.isEmpty());
}

bool SanctionBook::isVoiceMuted(const SanctionSubject &subject, const QList<quint32> &channelChain, qint64 nowMs) const
{
    return firstActive(subject, channelChain, nowMs, {SanctionType::VoiceMute, SanctionType::Mute}, true).has_value();
}

bool SanctionBook::isTextMuted(const SanctionSubject &subject, const QList<quint32> &channelChain, qint64 nowMs) const
{
    return firstActive(subject, channelChain, nowMs, {SanctionType::TextMute, SanctionType::Mute}, true).has_value();
}

bool SanctionBook::isRestricted(const SanctionSubject &subject, qint64 nowMs) const
{
    return firstActive(subject, {}, nowMs, {SanctionType::Restrict}, true).has_value();
}

QList<Sanction> SanctionBook::activeFor(const SanctionSubject &subject, qint64 nowMs) const
{
    QList<Sanction> result;
    for (const Sanction &s : m_sanctions) {
        if (s.isActiveAt(nowMs) && appliesTo(s, subject)) {
            result.append(s);
        }
    }
    return result;
}

} // namespace opentalk::moderation
