# SPDX-License-Identifier: GPL-3.0-only
# A plugin program whose behaviour the tests choose (well-behaved, hostile or broken).
qt_add_executable(opentalk_test_plugin test-plugin/main.cpp)
target_link_libraries(opentalk_test_plugin PRIVATE Qt6::Core OpenTalk::CompilerOptions)
