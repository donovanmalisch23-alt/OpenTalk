// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/moderation/Appeals.h"

namespace opentalk::moderation {

QString toString(Report::State state)
{
    switch (state) {
    case Report::State::Open:
        return QStringLiteral("open");
    case Report::State::ActionTaken:
        return QStringLiteral("action taken");
    case Report::State::Dismissed:
        return QStringLiteral("dismissed");
    }
    return QStringLiteral("unknown");
}

QString toString(Appeal::State state)
{
    switch (state) {
    case Appeal::State::Open:
        return QStringLiteral("open");
    case Appeal::State::Upheld:
        return QStringLiteral("upheld");
    case Appeal::State::Reduced:
        return QStringLiteral("reduced");
    case Appeal::State::Overturned:
        return QStringLiteral("overturned");
    }
    return QStringLiteral("unknown");
}

} // namespace opentalk::moderation
