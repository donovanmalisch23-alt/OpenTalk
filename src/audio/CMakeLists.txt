# SPDX-License-Identifier: GPL-3.0-only
# Audio: devices (miniaudio), Opus, jitter buffering, mixing and event sounds.
# Depends only on QtCore, Opus and miniaudio, so it can be reused by bots and tools.

# miniaudio is third-party code compiled exactly as published, without our warning flags.
add_library(opentalk_miniaudio STATIC miniaudio_impl.c)
target_include_directories(opentalk_miniaudio SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/third_party/miniaudio)
if(UNIX)
    target_link_libraries(opentalk_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
endif()
if(WIN32)
    target_link_libraries(opentalk_miniaudio PUBLIC ole32)
endif()

add_library(opentalk_audio STATIC
    include/opentalk/audio/AudioBackend.h
    include/opentalk/audio/AudioEngine.h
    include/opentalk/audio/AudioFormat.h
    include/opentalk/audio/Dsp.h
    include/opentalk/audio/JitterBuffer.h
    include/opentalk/audio/OpusCodec.h
    include/opentalk/audio/RingBuffer.h
    include/opentalk/audio/Sounds.h
    AudioEngine.cpp
    Dsp.cpp
    JitterBuffer.cpp
    MiniaudioBackend.cpp
    OpusCodec.cpp
    Sounds.cpp
    SyntheticAudioBackend.cpp)
add_library(OpenTalk::Audio ALIAS opentalk_audio)
target_include_directories(opentalk_audio PUBLIC include)
target_link_libraries(opentalk_audio
    PUBLIC Qt6::Core
    PRIVATE OpenTalk::Opus opentalk_miniaudio OpenTalk::CompilerOptions)
