# SPDX-License-Identifier: GPL-3.0-only
add_library(opentalk_ui STATIC
    include/opentalk/ui/AdminDialog.h
    include/opentalk/ui/AnnouncementQueue.h
    include/opentalk/ui/AudioController.h
    include/opentalk/ui/AudioDialogs.h
    include/opentalk/ui/ChannelTree.h
    include/opentalk/ui/ConnectDialog.h
    include/opentalk/ui/MainWindow.h
    include/opentalk/ui/ModerationDialogs.h
    include/opentalk/ui/PluginDialogs.h
    include/opentalk/ui/PushToTalkMonitor.h
    include/opentalk/ui/RunningPrograms.h
    include/opentalk/ui/SettingsDialog.h
    include/opentalk/ui/SupportFileDialog.h
    include/opentalk/ui/SupportReport.h
    AdminDialog.cpp
    AnnouncementQueue.cpp
    AudioController.cpp
    AudioDialogs.cpp
    ChannelTree.cpp
    ConnectDialog.cpp
    MainWindow.cpp
    ModerationDialogs.cpp
    PluginDialogs.cpp
    RequestTracker.h
    UiHelpers.h
    PushToTalkMonitor.cpp
    RunningPrograms.cpp
    SettingsDialog.cpp
    SupportFileDialog.cpp
    SupportReport.cpp)
add_library(OpenTalk::Ui ALIAS opentalk_ui)
target_include_directories(opentalk_ui PUBLIC include)
target_link_libraries(opentalk_ui
    PUBLIC Qt6::Widgets OpenTalk::Storage OpenTalk::Logging OpenTalk::Client OpenTalk::Audio OpenTalk::Moderation
           OpenTalk::Plugins
    PRIVATE OpenTalk::CompilerOptions)
target_compile_definitions(opentalk_ui PRIVATE
    OPENTALK_VERSION_STRING="${OPENTALK_DISPLAY_VERSION}")
