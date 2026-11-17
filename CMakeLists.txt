cmake_minimum_required(VERSION 3.20)

project(stream-client
    VERSION 0.1.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Os fontes estao em UTF-8 e a interface mostra textos com
# acento: sem isso o MSVC le os arquivos na pagina de codigo do
# Windows e os acentos saem trocados na tela.
if (MSVC)
    add_compile_options(/utf-8)
endif()


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


# ================================================================
# Dear ImGui (interface grafica, desenhada por cima do Direct3D 11)
#
# Tambem nao traz CMakeLists proprio: a biblioteca e montada aqui
# com o nucleo + os backends de Win32 e DirectX 11.
# ================================================================

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
)

FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

# Sem suporte a controle de videogame (evita depender do XInput).
target_compile_definitions(imgui PRIVATE
    IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
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

    src/invite_code.cpp
    src/invite_code.h

    src/ffmpeg_check.cpp
    src/ffmpeg_check.h
)


# WIN32 = programa de janela: o Windows nao abre console junto.
add_executable(stream-receiver WIN32
    src/receiver.cpp

    src/ui_common.cpp
    src/ui_common.h
    src/ui_widgets.cpp
    src/ui_widgets.h
    src/receiver_screens.cpp
    src/receiver_screens.h

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

    src/invite_code.cpp
    src/invite_code.h

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
    imgui
    dwmapi
    shell32
)