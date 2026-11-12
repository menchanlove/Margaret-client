#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace margaret {

// A running Minecraft: Bedrock Edition instance (UWP / Microsoft Store app).
struct GameProcess {
    DWORD pid = 0;
    std::wstring exeName;      // Minecraft.Windows.exe
    std::wstring windowTitle;  // e.g. "Minecraft" (may be empty while loading)
};

// The process image name of Bedrock Edition. Version-independent.
inline const wchar_t* kBedrockExe = L"Minecraft.Windows.exe";

// Finds every running Bedrock Edition process (Minecraft.Windows.exe).
std::vector<GameProcess> FindMinecraftProcesses();

// Returns info for a specific PID.
bool QueryProcess(DWORD pid, GameProcess& out);

// True when the process is a native 64-bit process.
bool Is64BitProcess(HANDLE process);

// True when a module with the given file name is already loaded in the process.
bool IsModuleLoaded(DWORD pid, const std::wstring& moduleFileName);

}  // namespace margaret
