# SPDX-License-Identifier: GPL-3.0-only
# libFuzzer harnesses for code that parses untrusted network input.
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "OPENTALK_BUILD_FUZZERS requires Clang (libFuzzer)")
endif()

function(opentalk_add_fuzzer name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE OpenTalk::Protocol OpenTalk::CompilerOptions)
    target_compile_options(${name} PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer,address,undefined)
endfunction()

opentalk_add_fuzzer(fuzz_frame_decoder)
opentalk_add_fuzzer(fuzz_message_decoder)
opentalk_add_fuzzer(fuzz_session)
opentalk_add_fuzzer(fuzz_server_core)
target_link_libraries(fuzz_server_core PRIVATE OpenTalk::Server)
