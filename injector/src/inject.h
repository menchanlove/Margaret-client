#pragma once

#include <windows.h>

#include <string>

namespace margaret {

struct InjectResult {
    bool ok = false;
    std::wstring message;
};

// Validates that the file is a 64-bit (x64) PE DLL.
bool ValidateDll(const std::wstring& path, std::wstring& error);

// Loads the DLL into the target process with LoadLibraryW via a remote thread.
InjectResult InjectDll(DWORD pid, const std::wstring& dllPath);

// Human readable text for a Win32 error code.
std::wstring Win32ErrorText(DWORD code);

}  // namespace margaret
