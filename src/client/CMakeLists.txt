# SPDX-License-Identifier: GPL-3.0-only
# Client-side networking: a TLS connection driving a ClientSession.
add_library(opentalk_client STATIC
    include/opentalk/client/Connection.h
    Connection.cpp)
add_library(OpenTalk::Client ALIAS opentalk_client)
target_include_directories(opentalk_client PUBLIC include)
target_link_libraries(opentalk_client
    PUBLIC Qt6::Network OpenTalk::Protocol OpenTalk::Security
    PRIVATE OpenTalk::Logging OpenTalk::CompilerOptions)
