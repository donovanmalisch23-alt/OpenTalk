# SPDX-License-Identifier: GPL-3.0-only
# Server runtime (no GUI dependencies) and the headless opentalk-server program.
add_library(opentalk_server STATIC
    include/opentalk/server/Server.h
    include/opentalk/server/ServerDatabase.h
    Server.cpp
    ServerDatabase.cpp)
add_library(OpenTalk::Server ALIAS opentalk_server)
target_include_directories(opentalk_server PUBLIC include)
target_link_libraries(opentalk_server
    PUBLIC Qt6::Network Qt6::Sql OpenTalk::Protocol OpenTalk::Security
    PRIVATE OpenTalk::Logging OpenTalk::CompilerOptions)

qt_add_executable(opentalk_server_app main.cpp ShutdownSignal.cpp ShutdownSignal.h Console.cpp Console.h)
set_target_properties(opentalk_server_app PROPERTIES OUTPUT_NAME "opentalk-server")
target_link_libraries(opentalk_server_app PRIVATE
    OpenTalk::Server OpenTalk::Security OpenTalk::Logging OpenTalk::CompilerOptions)
target_compile_definitions(opentalk_server_app PRIVATE OPENTALK_VERSION_STRING="${PROJECT_VERSION}")
