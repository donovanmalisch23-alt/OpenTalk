# SPDX-License-Identifier: GPL-3.0-only
# Moderation rules: capabilities, roles, permission resolution, sanctions, audit chain,
# reports and appeals. Pure logic with no I/O, so every rule is unit-testable.
add_library(opentalk_moderation STATIC
    include/opentalk/moderation/Appeals.h
    include/opentalk/moderation/Audit.h
    include/opentalk/moderation/Permissions.h
    include/opentalk/moderation/Sanctions.h
    Appeals.cpp
    Audit.cpp
    Permissions.cpp
    Sanctions.cpp)
add_library(OpenTalk::Moderation ALIAS opentalk_moderation)
target_include_directories(opentalk_moderation PUBLIC include)
target_link_libraries(opentalk_moderation
    PUBLIC Qt6::Core Qt6::Network
    PRIVATE OpenTalk::CompilerOptions)
