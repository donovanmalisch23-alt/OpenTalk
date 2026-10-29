# SPDX-License-Identifier: GPL-3.0-only
qt_add_executable(opentalk WIN32 main.cpp)
set_target_properties(opentalk PROPERTIES OUTPUT_NAME "OpenTalk")
target_link_libraries(opentalk PRIVATE
    OpenTalk::Ui OpenTalk::Storage OpenTalk::Security OpenTalk::Logging OpenTalk::CompilerOptions)
target_compile_definitions(opentalk PRIVATE OPENTALK_VERSION_STRING="${OPENTALK_DISPLAY_VERSION}")

# The window icon (drawn by tools/icon/make_icon.py).
qt_add_resources(opentalk "icons"
    PREFIX "/icons"
    BASE resources
    FILES resources/opentalk-16.png resources/opentalk-32.png resources/opentalk-48.png resources/opentalk-256.png)

# On Windows, the icon and version details of OpenTalk.exe itself.
if(WIN32)
    configure_file(opentalk.rc.in "${CMAKE_CURRENT_BINARY_DIR}/opentalk.rc" @ONLY)
    target_sources(opentalk PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/opentalk.rc")
endif()
