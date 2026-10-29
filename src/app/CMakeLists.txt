# SPDX-License-Identifier: GPL-3.0-only
qt_add_executable(opentalk WIN32 main.cpp)
set_target_properties(opentalk PROPERTIES OUTPUT_NAME "OpenTalk")
target_link_libraries(opentalk PRIVATE
    OpenTalk::Ui OpenTalk::Storage OpenTalk::Security OpenTalk::Logging OpenTalk::CompilerOptions)
target_compile_definitions(opentalk PRIVATE OPENTALK_VERSION_STRING="${OPENTALK_DISPLAY_VERSION}")
