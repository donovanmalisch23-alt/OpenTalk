# SPDX-License-Identifier: GPL-3.0-only
qt_add_executable(opentalk_greeter_bot main.cpp)
set_target_properties(opentalk_greeter_bot PROPERTIES OUTPUT_NAME "opentalk-greeter-bot")
target_link_libraries(opentalk_greeter_bot PRIVATE OpenTalk::Sdk OpenTalk::CompilerOptions)
