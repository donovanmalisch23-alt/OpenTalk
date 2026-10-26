// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/protocol/Version.h"

#include <algorithm>

namespace opentalk::protocol {

std::optional<quint16> negotiateVersion(VersionRange local, VersionRange remote)
{
    if (!local.isValid() || !remote.isValid()) {
        return std::nullopt;
    }
    const quint16 low = std::max(local.minimum, remote.minimum);
    const quint16 high = std::min(local.maximum, remote.maximum);
    if (low > high) {
        return std::nullopt;
    }
    return high;
}

} // namespace opentalk::protocol
