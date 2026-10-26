# SPDX-License-Identifier: GPL-3.0-only
# Public SDK: the stable API for bots, tools and other clients. Internals stay private.
add_library(opentalk_sdk STATIC
    include/opentalk/sdk/Client.h
    include/opentalk/sdk/Version.h
    Client.cpp)
add_library(OpenTalk::Sdk ALIAS opentalk_sdk)
target_include_directories(opentalk_sdk PUBLIC include)
target_link_libraries(opentalk_sdk
    PUBLIC Qt6::Core
    PRIVATE OpenTalk::Client OpenTalk::Security OpenTalk::CompilerOptions)
