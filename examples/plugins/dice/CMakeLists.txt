# SPDX-License-Identifier: GPL-3.0-only
# Built into <build>/plugins/org.opentalk.examples.dice/ together with its plugin.json,
# so that folder can be installed with "Plugins > Manage plugins > Install from folder".
set(OPENTALK_DICE_PLUGIN_DIR "${CMAKE_BINARY_DIR}/plugins/org.opentalk.examples.dice")
qt_add_executable(opentalk_dice_plugin main.cpp)
set_target_properties(opentalk_dice_plugin PROPERTIES
    OUTPUT_NAME "opentalk-dice-plugin"
    RUNTIME_OUTPUT_DIRECTORY "${OPENTALK_DICE_PLUGIN_DIR}")
target_link_libraries(opentalk_dice_plugin PRIVATE Qt6::Core OpenTalk::CompilerOptions)
add_custom_command(TARGET opentalk_dice_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/plugin.json"
            "${OPENTALK_DICE_PLUGIN_DIR}/plugin.json")
