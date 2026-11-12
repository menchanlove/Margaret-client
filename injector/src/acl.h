#pragma once

#include <windows.h>

#include <string>

namespace margaret {

// Bedrock Edition runs inside a UWP AppContainer sandbox. A process in that
// sandbox can only LoadLibrary a file whose ACL grants read/execute to the
// package (or to ALL APPLICATION PACKAGES). This adds that grant to the DLL
// file if it is missing. Returns true on success (or if already granted).
bool GrantAppContainerAccess(const std::wstring& filePath, std::wstring& error);

// True if the file's ACL already grants ALL APPLICATION PACKAGES read access.
bool HasAppContainerAccess(const std::wstring& filePath);

}  // namespace margaret
