cmake_minimum_required(VERSION 3.20)

project(stream-client
    VERSION 0.1.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# ================================================================
# libopus (via FetchContent - baixa e compila junto com o projeto)
# ================================================================

include(FetchContent)

set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    opus
    GIT_REPOSITORY https://github.com/xiph/opus.git
    GIT_TAG        v1.5.2
)

FetchContent_MakeAvailable(opus)


# ================================================================
# Monocypher (criptografia do canal: X25519 + XChaCha20-Poly1305)
#
# Biblioteca pequena (um .c e um .h), de dominio publico. Nao traz
# CMakeLists proprio, entao o FetchContent so baixa o codigo e a
# biblioteca e montada aqui.
# ================================================================

FetchContent_Declare(
    monocypher
    GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git
    GIT_TAG        4.0.2
)

FetchContent_MakeAvailable(monocypher)

add_library(monocypher STATIC
    ${monocypher_SOURCE_DIR}/src/monocypher.c
)

target_include_directories(monocypher PUBLIC
    ${monocypher_SOURCE_DIR}/src
)


add_executable(stream-client WIN32
    src/main.cpp

    src/server.cpp
    src/server.h

    src/udp_server.cpp
    src/udp_server.h

    src/h264_encoder.cpp
    src/h264_encoder.h

    src/audio_capture.cpp
    src/audio_capture.h

    src/audio_encoder.cpp
    src/audio_encoder.h

    src/udp_audio_server.cpp
    src/udp_audio_server.h

    src/signaling_client.cpp
    src/signaling_client.h

    src/nat_traversal.cpp
    src/nat_traversal.h
    src/stun_message.h
    src/udp_viewers.h

    src/stream_crypto.cpp
    src/stream_crypto.h

    src/ffmpeg_check.cpp
    src/ffmpeg_check.h
)


add_executable(stream-receiver
    src/receiver.cpp

    src/h264_decoder.cpp
    src/h264_decoder.h

    src/audio_decoder.cpp
    src/audio_decoder.h

    src/audio_playback.cpp
    src/audio_playback.h

    src/signaling_client.cpp
    src/signaling_client.h

    src/nat_traversal.cpp
    src/nat_traversal.h
    src/stun_message.h
    src/udp_viewers.h

    src/stream_crypto.cpp
    src/stream_crypto.h

    src/ffmpeg_check.cpp
    src/ffmpeg_check.h
)


target_compile_definitions(stream-client PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)


target_compile_definitions(stream-receiver PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)


target_link_libraries(stream-client PRIVATE
    d3d11
    dxgi
    windowsapp
    ws2_32
    ole32
    opus
    winhttp
    winmm
    monocypher
    bcrypt
)


target_link_libraries(stream-receiver PRIVATE
    ws2_32
    ole32
    opus
    d3d11
    dxgi
    d3dcompiler
    winhttp
    monocypher
    bcrypt
)