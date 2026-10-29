// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>

#include <optional>

/// Reads one line from standard input. When standard input is a terminal, the typed
/// characters are not echoed (for passwords). Returns nullopt at end of input.
std::optional<QString> readSecretLine(const QString &prompt);
