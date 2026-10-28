# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.21)

project(OpenTalk
    VERSION 0.1.0
    DESCRIPTION "Open-source, self-hosted, accessible voice and text communication"
    HOMEPAGE_URL "https://github.com/donovanmalisch23-alt/OpenTalk"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(OPENTALK_BUILD_TESTS "Build the OpenTalk test suite" ON)
option(OPENTALK_BUILD_FUZZERS "Build libFuzzer targets (Clang only)" OFF)
option(OPENTALK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(OPENTALK_ENABLE_SANITIZERS "Build with AddressSanitizer and UBSan (GCC/Clang only)" OFF)

# Qt 6.4 is the oldest version the code is verified against. Windows release
# builds use a newer Qt LTS; see docs/development/building.md.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Network Sql Widgets)
qt_standard_project_setup()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(OpenTalkCompilerOptions)
include(OpenTalkDependencies)

add_subdirectory(src)
add_subdirectory(examples)
add_subdirectory(tools)

if(OPENTALK_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests/unit)
    add_subdirectory(tests/integration)
endif()

if(OPENTALK_BUILD_FUZZERS)
    add_subdirectory(tests/fuzz)
endif()
