cmake_minimum_required(VERSION 3.20)

project(stream-project
    VERSION 0.8.12
    LANGUAGES C CXX
)

# Recursos do Windows (icone e versao dos .exe - ver resources/).
enable_language(RC)


# ================================================================
# Runtime do C++ embutido no .exe
#
# Por padrao o MSVC liga o programa a DLLs do "Microsoft Visual
# C++ Redistributable" - quem nao o tem instalado ve o erro de
# VCRUNTIME140.dll. Com o runtime estatico (/MT), tudo vai dentro
# do executavel. Precisa vir antes de qualquer biblioteca (opus,
# imgui...) ser criada, pra todas usarem o mesmo runtime.
# ================================================================

set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)


# ================================================================
# Versao (gera version.h a partir do "VERSION" acima)
# ================================================================

configure_file(
    ${CMAKE_SOURCE_DIR}/src/version.h.in
    ${CMAKE_BINARY_DIR}/generated/version.h
)

include_directories(
    ${CMAKE_BINARY_DIR}/generated
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Os fontes estao em UTF-8 e a interface mostra textos com
# acento: sem isso o MSVC le os arquivos na pagina de codigo do
# Windows e os acentos saem trocados na tela.
if (MSVC)
    add_compile_options(/utf-8)

    # Informacao de depuracao (.pdb) tambem no Release. Nao muda
    # a velocidade do programa; e o que permite abrir um .dmp de
    # crash (ver crash_report.h) e ver a linha exata do erro. O
    # .pdb fica na pasta do build - nao precisa ir junto pra
    # quem usa o programa.
    add_compile_options($<$<CONFIG:Release>:/Zi>)
    add_link_options($<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
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
# RNNoise (reducao de ruido do microfone no canal de voz)
#
# Da Xiph.Org (a mesma do Opus), licenca BSD. Baixa o pacote da
# versao 0.2 (que ja traz o modelo treinado) e monta a biblioteca
# aqui - o pacote usa autotools, sem CMakeLists. Codigo em C puro
# (sem as otimizacoes AVX2 opcionais: ~1-2% de um nucleo).
# ================================================================

FetchContent_Declare(
    rnnoise
    URL      https://github.com/xiph/rnnoise/releases/download/v0.2/rnnoise-0.2.tar.gz
    URL_HASH SHA256=90fce4b00b9ff24c08dbfe31b82ffd43bae383d85c5535676d28b0a2b11c0d37
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(rnnoise)

add_library(rnnoise STATIC
    ${rnnoise_SOURCE_DIR}/src/denoise.c
    ${rnnoise_SOURCE_DIR}/src/rnn.c
    ${rnnoise_SOURCE_DIR}/src/pitch.c
    ${rnnoise_SOURCE_DIR}/src/kiss_fft.c
    ${rnnoise_SOURCE_DIR}/src/celt_lpc.c
    ${rnnoise_SOURCE_DIR}/src/nnet.c
    ${rnnoise_SOURCE_DIR}/src/nnet_default.c
    ${rnnoise_SOURCE_DIR}/src/rnnoise_data.c
    ${rnnoise_SOURCE_DIR}/src/rnnoise_tables.c
    ${rnnoise_SOURCE_DIR}/src/parse_lpcnet_weights.c
)

# C11: o codigo usa "restrict".
set_target_properties(rnnoise PROPERTIES C_STANDARD 11)

target_include_directories(rnnoise
    PUBLIC  ${rnnoise_SOURCE_DIR}/include
    PRIVATE ${rnnoise_SOURCE_DIR}/src
)

if(MSVC)
    # O MSVC nao define __SSE2__ (o GCC/Clang sim): sem isto o
    # RNNoise cai num caminho generico que pede um arquivo que
    # nao vem no pacote (os_support.h). Todo PC x64 tem SSE2; o
    # proprio RNNoise usa estas macros pra isso no MSVC.
    target_compile_definitions(rnnoise PRIVATE
        OPUS_X86_MAY_HAVE_SSE
        OPUS_X86_MAY_HAVE_SSE2
    )
endif()


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


# ============================================================
# Colae.exe - programa unico (Assistir + Transmitir)
# ============================================================
#
# WIN32 = programa de janela: o Windows nao abre console junto.
# receiver.cpp e o programa (janela, Direct3D, interface, modo
# Assistir); main.cpp + client_ui.cpp sao o modo Transmitir
# (namespace sender - ver sender.h).

add_executable(stream-project WIN32
    # Programa + modo Assistir
    src/receiver.cpp
    src/receiver_screens.cpp
    src/receiver_screens.h
    src/video_reassembly.cpp
    src/video_reassembly.h
    src/h264_decoder.cpp
    src/h264_decoder.h
    src/audio_decoder.cpp
    src/audio_decoder.h
    src/audio_playback.cpp
    src/audio_playback.h

    # Modo Transmitir
    src/sender.h
    src/main.cpp
    src/client_ui.cpp
    src/client_ui.h
    src/client_view.cpp
    src/client_view.h
    src/client_screens.cpp
    src/client_screens.h
    src/capture_targets.cpp
    src/capture_targets.h
    src/capture_thumbnails.cpp
    src/capture_thumbnails.h
    src/h264_encoder.cpp
    src/h264_encoder.h
    src/audio_capture.cpp
    src/audio_capture.h
    src/audio_encoder.cpp
    src/audio_encoder.h
    src/udp_server.cpp
    src/udp_server.h
    src/udp_audio_server.cpp
    src/udp_audio_server.h

    # Comum
    src/app_settings.cpp
    src/app_settings.h
    src/power_mode.cpp
    src/power_mode.h
    src/crash_report.cpp
    src/crash_report.h
    src/ui_common.cpp
    src/ui_common.h
    src/ui_widgets.cpp
    src/ui_widgets.h
    src/signaling_client.cpp
    src/signaling_client.h
    src/noise_suppressor.cpp
    src/noise_suppressor.h
    src/mic_tester.cpp
    src/mic_tester.h
    src/ui_sounds.cpp
    src/ui_sounds.h
    src/room_client.cpp
    src/room_crypto.cpp
    src/room_crypto.h
    src/room_client.h
    src/image_codec.cpp
    src/image_codec.h
    src/shortcuts.cpp
    src/shortcuts.h
    src/voice_channel.cpp
    src/voice_channel.h
    src/voice_capture.cpp
    src/voice_capture.h
    src/voice_mixer.cpp
    src/voice_mixer.h
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
    src/video_protocol.h
    src/audio_protocol.h
    src/updater.cpp
    src/updater.h
    src/update_signature.cpp
    src/update_signature.h
    src/update_key.h

    src/version.h.in
    resources/colae.rc
)


set_target_properties(stream-project PROPERTIES
    OUTPUT_NAME "Colae"
)


target_compile_definitions(stream-project PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)


target_link_libraries(stream-project PRIVATE
    d3d11
    dxgi
    d3dcompiler
    windowsapp
    ws2_32
    ole32
    opus
    winhttp
    winmm
    monocypher
    rnnoise
    bcrypt
    imgui
    dwmapi
    shell32
    comdlg32
    advapi32
    gdi32
    user32
)


# ============================================================
# colae-assinar - assina os instaladores das atualizacoes
# (ver tools/assinar/colae_assinar.cpp e PUBLICAR.md). Programa
# de linha de comando so pra quem publica versoes: nao vai no
# pacote.
# ============================================================

add_executable(colae-assinar
    tools/assinar/colae_assinar.cpp
    src/update_signature.cpp
    src/update_signature.h
)

target_include_directories(colae-assinar PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(colae-assinar PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(colae-assinar PRIVATE
    monocypher
    bcrypt
)
