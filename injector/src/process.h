#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace margaret {

// A running Minecraft (Java Edition) instance.
struct GameProcess {
    DWORD pid = 0;
    std::wstring exeName;      // javaw.exe / java.exe
    std::wstring windowTitle;  // e.g. "Minecraft 1.21.x"
};

// Finds every running Java process that owns a visible top-level window
// whose title starts with "Minecraft" (the launcher window is excluded).
std::vector<GameProcess> FindMinecraftProcesses();

// Returns info for a specific PID (window title may be empty).
bool QueryProcess(DWORD pid, GameProcess& out);

// True when the process is a native 64-bit process.
bool Is64BitProcess(HANDLE process);

// True when a module with the given file name is already loaded in the process.
bool IsModuleLoaded(DWORD pid, const std::wstring& moduleFileName);

}  // namespace margaret
