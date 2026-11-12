cmake_minimum_required(VERSION 3.16)
project(MargaretInjector LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "MargaretInjector only builds for Windows (x64).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# GUI application (dark theme). Set WIN32 so no console window is created.
add_executable(MargaretInjector WIN32
    src/gui.cpp
    src/inject.cpp
    src/process.cpp
    src/acl.cpp
)

target_compile_definitions(MargaretInjector PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(MargaretInjector PRIVATE advapi32 comdlg32)

if(MSVC)
    target_compile_options(MargaretInjector PRIVATE /W4 /utf-8)
    set_property(TARGET MargaretInjector PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(MargaretInjector PRIVATE -Wall -Wextra)
    target_link_options(MargaretInjector PRIVATE -municode -static -mwindows)
endif()

# Optional console build (MargaretInjectorCLI) kept for scripting/automation.
add_executable(MargaretInjectorCLI
    src/main.cpp
    src/inject.cpp
    src/process.cpp
    src/acl.cpp
)
target_compile_definitions(MargaretInjectorCLI PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(MargaretInjectorCLI PRIVATE advapi32)
if(MSVC)
    target_compile_options(MargaretInjectorCLI PRIVATE /W4 /utf-8)
    set_property(TARGET MargaretInjectorCLI PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(MargaretInjectorCLI PRIVATE -Wall -Wextra)
    target_link_options(MargaretInjectorCLI PRIVATE -municode -static)
endif()
