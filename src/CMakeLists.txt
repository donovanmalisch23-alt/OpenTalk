# SPDX-License-Identifier: GPL-3.0-only
# Dependency direction (lower layers never depend on higher ones):
#   app -> ui -> storage, logging
#   protocol depends only on QtCore
add_subdirectory(logging)
add_subdirectory(storage)
add_subdirectory(protocol)
add_subdirectory(audio)
add_subdirectory(moderation)
add_subdirectory(security)
add_subdirectory(client)
add_subdirectory(server)
add_subdirectory(sdk)
if(NOT OPENTALK_SERVER_ONLY)
    add_subdirectory(plugins)
    add_subdirectory(ui)
    add_subdirectory(app)
endif()
