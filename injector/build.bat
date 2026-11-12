@echo off
rem Builds MargaretInjector.exe (x64 Release). Requires Visual Studio 2022 with C++ and CMake.
cd /d "%~dp0"
cmake -S . -B build -A x64 || exit /b 1
cmake --build build --config Release || exit /b 1
echo.
echo Built: %~dp0build\Release\MargaretInjector.exe
