cmake_minimum_required(VERSION 3.16)
project(MargaretInjector LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "MargaretInjector only builds for Windows (x64).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(MargaretInjector
    src/main.cpp
    src/inject.cpp
    src/process.cpp
)

target_compile_definitions(MargaretInjector PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

if(MSVC)
    target_compile_options(MargaretInjector PRIVATE /W4 /utf-8)
    # Static CRT so the exe runs without the VC++ redistributable.
    set_property(TARGET MargaretInjector PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(MargaretInjector PRIVATE -Wall -Wextra)
    target_link_options(MargaretInjector PRIVATE -municode -static)
endif()
