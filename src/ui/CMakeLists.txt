# SPDX-License-Identifier: GPL-3.0-only
add_library(opentalk_ui STATIC
    include/opentalk/ui/MainWindow.h
    include/opentalk/ui/SettingsDialog.h
    MainWindow.cpp
    SettingsDialog.cpp)
add_library(OpenTalk::Ui ALIAS opentalk_ui)
target_include_directories(opentalk_ui PUBLIC include)
target_link_libraries(opentalk_ui
    PUBLIC Qt6::Widgets OpenTalk::Storage OpenTalk::Logging
    PRIVATE OpenTalk::CompilerOptions)
target_compile_definitions(opentalk_ui PRIVATE
    OPENTALK_VERSION_STRING="${PROJECT_VERSION}")
