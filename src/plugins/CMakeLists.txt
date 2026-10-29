# SPDX-License-Identifier: GPL-3.0-only
# Out-of-process plugins: manifests, the user's decisions, plugin processes and the
# bridge that enforces what each plugin may do (docs/plugins/).
add_library(opentalk_plugins STATIC
    include/opentalk/plugins/PluginHost.h
    include/opentalk/plugins/PluginManifest.h
    include/opentalk/plugins/PluginProcess.h
    include/opentalk/plugins/PluginRegistry.h
    PluginHost.cpp
    PluginManifest.cpp
    PluginProcess.cpp
    PluginRegistry.cpp)
add_library(OpenTalk::Plugins ALIAS opentalk_plugins)
target_include_directories(opentalk_plugins PUBLIC include)
target_link_libraries(opentalk_plugins
    PUBLIC Qt6::Core OpenTalk::Client OpenTalk::Protocol
    PRIVATE OpenTalk::Logging OpenTalk::CompilerOptions)
