# SPDX-License-Identifier: GPL-3.0-only
add_library(opentalk_logging STATIC
    include/opentalk/logging/Logging.h
    Logging.cpp)
add_library(OpenTalk::Logging ALIAS opentalk_logging)
target_include_directories(opentalk_logging PUBLIC include)
target_link_libraries(opentalk_logging
    PUBLIC Qt6::Core
    PRIVATE OpenTalk::CompilerOptions)
