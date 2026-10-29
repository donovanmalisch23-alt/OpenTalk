# SPDX-License-Identifier: GPL-3.0-only
qt_add_executable(opentalk_ping main.cpp)
set_target_properties(opentalk_ping PROPERTIES OUTPUT_NAME "opentalk-ping")
target_link_libraries(opentalk_ping PRIVATE OpenTalk::Sdk OpenTalk::CompilerOptions)
