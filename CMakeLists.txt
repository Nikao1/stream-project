cmake_minimum_required(VERSION 3.20)

project(stream-client
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(stream-client WIN32
    src/main.cpp
    src/server.cpp
)

add_executable(stream-receiver
    src/receiver.cpp
)

target_compile_definitions(stream-client PRIVATE
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
)

target_link_libraries(stream-receiver PRIVATE
    ws2_32
)