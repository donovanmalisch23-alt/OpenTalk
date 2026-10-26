# SPDX-License-Identifier: GPL-3.0-only
# libFuzzer harnesses for code that parses untrusted network input.
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "OPENTALK_BUILD_FUZZERS requires Clang (libFuzzer)")
endif()

add_executable(fuzz_frame_decoder fuzz_frame_decoder.cpp)
target_link_libraries(fuzz_frame_decoder PRIVATE OpenTalk::Protocol OpenTalk::CompilerOptions)
target_compile_options(fuzz_frame_decoder PRIVATE -fsanitize=fuzzer,address,undefined)
target_link_options(fuzz_frame_decoder PRIVATE -fsanitize=fuzzer,address,undefined)
