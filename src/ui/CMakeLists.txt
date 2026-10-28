# SPDX-License-Identifier: GPL-3.0-only
add_library(opentalk_ui STATIC
    include/opentalk/ui/AnnouncementQueue.h
    include/opentalk/ui/AudioController.h
    include/opentalk/ui/AudioDialogs.h
    include/opentalk/ui/ChannelTree.h
    include/opentalk/ui/ConnectDialog.h
    include/opentalk/ui/MainWindow.h
    include/opentalk/ui/PushToTalkMonitor.h
    include/opentalk/ui/SettingsDialog.h
    AnnouncementQueue.cpp
    AudioController.cpp
    AudioDialogs.cpp
    ChannelTree.cpp
    ConnectDialog.cpp
    MainWindow.cpp
    PushToTalkMonitor.cpp
    SettingsDialog.cpp)
add_library(OpenTalk::Ui ALIAS opentalk_ui)
target_include_directories(opentalk_ui PUBLIC include)
target_link_libraries(opentalk_ui
    PUBLIC Qt6::Widgets OpenTalk::Storage OpenTalk::Logging OpenTalk::Client OpenTalk::Audio
    PRIVATE OpenTalk::CompilerOptions)
target_compile_definitions(opentalk_ui PRIVATE
    OPENTALK_VERSION_STRING="${PROJECT_VERSION}")
