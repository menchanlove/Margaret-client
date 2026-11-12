#include "process.h"

#include <tlhelp32.h>

#include <algorithm>
#include <cwctype>

namespace margaret {
namespace {

std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

bool IsJavaExe(const std::wstring& exe) {
    const std::wstring lower = ToLower(exe);
    return lower == L"javaw.exe" || lower == L"java.exe";
}

bool StartsWith(const std::wstring& s, const std::wstring& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

struct WindowSearch {
    DWORD pid;
    std::wstring title;
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    auto* search = reinterpret_cast<WindowSearch*>(lParam);

    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (windowPid != search->pid || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) {
        return TRUE;
    }

    wchar_t buf[512] = {};
    if (GetWindowTextW(hwnd, buf, static_cast<int>(sizeof(buf) / sizeof(buf[0]))) <= 0) {
        return TRUE;
    }

    std::wstring title = buf;
    // The game window is titled "Minecraft <version>" (optionally with
    // "- Singleplayer" / "- Multiplayer ..."). Skip the launcher.
    if (StartsWith(title, L"Minecraft") && title.find(L"Launcher") == std::wstring::npos) {
        search->title = std::move(title);
        return FALSE;  // found
    }
    return TRUE;
}

std::wstring FindGameWindowTitle(DWORD pid) {
    WindowSearch search{pid, L""};
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&search));
    return search.title;
}

}  // namespace

std::vector<GameProcess> FindMinecraftProcesses() {
    std::vector<GameProcess> result;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return result;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (!IsJavaExe(entry.szExeFile)) {
                continue;
            }
            std::wstring title = FindGameWindowTitle(entry.th32ProcessID);
            if (!title.empty()) {
                result.push_back({entry.th32ProcessID, entry.szExeFile, std::move(title)});
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return result;
}

bool QueryProcess(DWORD pid, GameProcess& out) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    bool found = false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == pid) {
                out.pid = pid;
                out.exeName = entry.szExeFile;
                out.windowTitle = FindGameWindowTitle(pid);
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return found;
}

bool Is64BitProcess(HANDLE process) {
    // IsWow64Process2 (Win10 1511+) also handles ARM64 hosts correctly.
    using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    auto fn = reinterpret_cast<IsWow64Process2Fn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2")));
    if (fn) {
        USHORT processMachine = 0, nativeMachine = 0;
        if (fn(process, &processMachine, &nativeMachine)) {
            return processMachine == IMAGE_FILE_MACHINE_UNKNOWN;  // not running under WOW64
        }
    }

    BOOL wow64 = FALSE;
    if (!IsWow64Process(process, &wow64)) {
        return false;
    }
    return !wow64;
}

bool IsModuleLoaded(DWORD pid, const std::wstring& moduleFileName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    const std::wstring wanted = ToLower(moduleFileName);
    bool loaded = false;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (ToLower(entry.szModule) == wanted) {
                loaded = true;
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return loaded;
}

}  // namespace margaret
