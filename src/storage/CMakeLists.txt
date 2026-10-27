# SPDX-License-Identifier: GPL-3.0-only
add_library(opentalk_storage STATIC
    include/opentalk/storage/AppPaths.h
    include/opentalk/storage/ClientSettings.h
    include/opentalk/storage/SettingsSchema.h
    include/opentalk/storage/SettingsStore.h
    include/opentalk/storage/ServerProfiles.h
    AppPaths.cpp
    ClientSettings.cpp
    SettingsSchema.cpp
    SettingsStore.cpp
    ServerProfiles.cpp)
add_library(OpenTalk::Storage ALIAS opentalk_storage)
target_include_directories(opentalk_storage PUBLIC include)
target_link_libraries(opentalk_storage
    PUBLIC Qt6::Core OpenTalk::Logging
    PRIVATE OpenTalk::CompilerOptions)
