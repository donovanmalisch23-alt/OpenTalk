// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QtGlobal>

#include <optional>

namespace opentalk::protocol {

/// An inclusive range of protocol major versions an endpoint can speak.
struct VersionRange {
    quint16 minimum = 0;
    quint16 maximum = 0;

    bool isValid() const { return minimum != 0 && minimum <= maximum; }
};

/// Versions this build implements. Version 1 is the first (still provisional) protocol.
inline constexpr VersionRange kSupportedVersions{1, 1};

/// Picks the highest version both sides support, or nullopt if the ranges do not
/// overlap or either range is malformed. The server makes this choice and the client
/// must reject any answer outside its own range, which prevents silent downgrades.
std::optional<quint16> negotiateVersion(VersionRange local, VersionRange remote);

} // namespace opentalk::protocol
