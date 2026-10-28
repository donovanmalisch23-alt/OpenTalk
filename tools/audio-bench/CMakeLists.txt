# SPDX-License-Identifier: GPL-3.0-only
# Measures the audio path: codec CPU, jitter and loss tolerance, network latency through
# a real server, and (with hardware) device round-trip latency. See docs/audio/benchmarks.md.
qt_add_executable(opentalk_audio_bench main.cpp)
set_target_properties(opentalk_audio_bench PROPERTIES OUTPUT_NAME "opentalk-audio-bench")
target_link_libraries(opentalk_audio_bench PRIVATE
    OpenTalk::Audio OpenTalk::Client OpenTalk::Server OpenTalk::Security OpenTalk::Opus OpenTalk::CompilerOptions)
target_compile_definitions(opentalk_audio_bench PRIVATE OPENTALK_VERSION_STRING="${PROJECT_VERSION}")
