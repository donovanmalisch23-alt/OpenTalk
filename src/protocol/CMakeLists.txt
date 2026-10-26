# SPDX-License-Identifier: GPL-3.0-only
# Wire-format code. Depends only on QtCore so the server, SDK and tools can share it.
add_library(opentalk_protocol STATIC
    include/opentalk/protocol/Frame.h
    include/opentalk/protocol/Messages.h
    include/opentalk/protocol/Session.h
    include/opentalk/protocol/Version.h
    Frame.cpp
    Messages.cpp
    Session.cpp
    Version.cpp)
add_library(OpenTalk::Protocol ALIAS opentalk_protocol)
target_include_directories(opentalk_protocol PUBLIC include)
target_link_libraries(opentalk_protocol
    PUBLIC Qt6::Core
    PRIVATE OpenTalk::CompilerOptions)
