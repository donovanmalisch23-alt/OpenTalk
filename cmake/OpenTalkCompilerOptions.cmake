# SPDX-License-Identifier: GPL-3.0-only
# Shared compiler settings for every first-party OpenTalk target.

add_library(opentalk_compiler_options INTERFACE)
add_library(OpenTalk::CompilerOptions ALIAS opentalk_compiler_options)

if(MSVC)
    target_compile_options(opentalk_compiler_options INTERFACE
        /W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc)
    if(OPENTALK_WARNINGS_AS_ERRORS)
        target_compile_options(opentalk_compiler_options INTERFACE /WX)
    endif()
else()
    target_compile_options(opentalk_compiler_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual)
    if(OPENTALK_WARNINGS_AS_ERRORS)
        target_compile_options(opentalk_compiler_options INTERFACE -Werror)
    endif()
    if(OPENTALK_BUILD_FUZZERS)
        # Instrument every first-party library, not only the fuzz harnesses: libFuzzer
        # needs coverage from the parsers themselves to explore them, and AddressSanitizer
        # must check their memory accesses. Harnesses add -fsanitize=fuzzer when linking.
        target_compile_options(opentalk_compiler_options INTERFACE
            -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
        target_link_options(opentalk_compiler_options INTERFACE -fsanitize=address,undefined)
    endif()
    if(OPENTALK_ENABLE_SANITIZERS)
        target_compile_options(opentalk_compiler_options INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        target_link_options(opentalk_compiler_options INTERFACE -fsanitize=address,undefined)
    endif()
endif()

# Keep Qt from silently converting between QString and const char*, and from
# using deprecated APIs that will disappear in later Qt releases.
target_compile_definitions(opentalk_compiler_options INTERFACE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QT_NO_URL_CAST_FROM_STRING
    QT_NO_NARROWING_CONVERSIONS_IN_CONNECT
    QT_DISABLE_DEPRECATED_UP_TO=0x060400)
if(WIN32)
    # Keep <windows.h> from defining min and max macros that break std::min and std::max.
    target_compile_definitions(opentalk_compiler_options INTERFACE NOMINMAX)
endif()
