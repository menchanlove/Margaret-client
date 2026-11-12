// Margaret Injector - loads Margaret.dll into a running Minecraft: Java Edition client.
//
// Usage:
//   MargaretInjector.exe                 auto-detect Minecraft and inject Margaret.dll
//   MargaretInjector.exe --wait          wait until Minecraft is started, then inject
//   MargaretInjector.exe --pid 1234      inject into a specific process
//   MargaretInjector.exe --dll C:\x.dll  use a different DLL
//   MargaretInjector.exe --list          list detected Minecraft instances

#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <iostream>
#include <string>
#include <vector>

#include "inject.h"
#include "process.h"

namespace {

constexpr const wchar_t* kVersion = L"1.0.0";
constexpr const wchar_t* kDefaultDll = L"Margaret.dll";

struct Options {
    std::wstring dllPath;
    DWORD pid = 0;
    bool wait = false;
    bool list = false;
    bool noPause = false;
    bool help = false;
};

void PrintUsage() {
    std::wcout << L"Usage: MargaretInjector.exe [options]\n"
                  L"  --dll <path>   DLL to inject (default: Margaret.dll next to the injector)\n"
                  L"  --pid <pid>    Inject into this process instead of auto-detecting\n"
                  L"  --wait         Wait for Minecraft to start before injecting\n"
                  L"  --list         List detected Minecraft instances and exit\n"
                  L"  --no-pause     Do not wait for Enter before exiting\n"
                  L"  --help         Show this help\n";
}

bool ParseArgs(int argc, wchar_t** argv, Options& opt) {
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        auto needValue = [&](const wchar_t* name) -> const wchar_t* {
            if (i + 1 >= argc) {
                std::wcerr << L"Missing value for " << name << L"\n";
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == L"--dll") {
            const wchar_t* v = needValue(L"--dll");
            if (!v) return false;
            opt.dllPath = v;
        } else if (arg == L"--pid") {
            const wchar_t* v = needValue(L"--pid");
            if (!v) return false;
            wchar_t* end = nullptr;
            const unsigned long pid = std::wcstoul(v, &end, 10);
            if (!end || *end != L'\0' || pid == 0) {
                std::wcerr << L"Invalid PID: " << v << L"\n";
                return false;
            }
            opt.pid = static_cast<DWORD>(pid);
        } else if (arg == L"--wait") {
            opt.wait = true;
        } else if (arg == L"--list") {
            opt.list = true;
        } else if (arg == L"--no-pause") {
            opt.noPause = true;
        } else if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
            opt.help = true;
        } else {
            std::wcerr << L"Unknown option: " << arg << L"\n";
            return false;
        }
    }
    return true;
}

std::wstring ExeDirectory() {
    wchar_t buf[MAX_PATH * 4] = {};
    const DWORD len = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
    std::wstring path(buf, len);
    const size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : path.substr(0, pos);
}

void PrintProcesses(const std::vector<margaret::GameProcess>& procs) {
    for (size_t i = 0; i < procs.size(); ++i) {
        std::wcout << L"  [" << (i + 1) << L"] PID " << procs[i].pid << L"  " << procs[i].exeName << L"  \""
                   << procs[i].windowTitle << L"\"\n";
    }
}

// Picks the target process. Returns 0 if none was chosen.
DWORD SelectTarget(const Options& opt) {
    if (opt.pid != 0) {
        margaret::GameProcess proc;
        if (!margaret::QueryProcess(opt.pid, proc)) {
            std::wcerr << L"[!] No process with PID " << opt.pid << L".\n";
            return 0;
        }
        if (proc.windowTitle.empty()) {
            std::wcout << L"[*] Warning: PID " << opt.pid << L" (" << proc.exeName
                       << L") has no Minecraft window. Injecting anyway.\n";
        }
        return opt.pid;
    }

    std::vector<margaret::GameProcess> procs = margaret::FindMinecraftProcesses();
    if (procs.empty() && opt.wait) {
        std::wcout << L"[*] Waiting for Minecraft to start (Ctrl+C to cancel)...\n";
        while (procs.empty()) {
            Sleep(1000);
            procs = margaret::FindMinecraftProcesses();
        }
        // Give the game a moment to finish creating its window / OpenGL context.
        std::wcout << L"[*] Minecraft detected, waiting for it to finish loading...\n";
        Sleep(5000);
    }

    if (procs.empty()) {
        std::wcerr << L"[!] Minecraft is not running. Start the game (or use --wait) and try again.\n";
        return 0;
    }

    if (procs.size() == 1) {
        std::wcout << L"[*] Found Minecraft: PID " << procs[0].pid << L" \"" << procs[0].windowTitle << L"\"\n";
        return procs[0].pid;
    }

    std::wcout << L"[*] Multiple Minecraft instances found:\n";
    PrintProcesses(procs);
    for (;;) {
        std::wcout << L"Select [1-" << procs.size() << L"] (0 to cancel): ";
        std::wstring line;
        if (!std::getline(std::wcin, line)) return 0;
        wchar_t* end = nullptr;
        const unsigned long n = std::wcstoul(line.c_str(), &end, 10);
        if (end != line.c_str()) {
            if (n == 0) return 0;
            if (n <= procs.size()) return procs[n - 1].pid;
        }
    }
}

void PauseIfNeeded(const Options& opt) {
    if (opt.noPause) return;
    // Only pause when we own the console (double-clicked from Explorer).
    DWORD consoleProcs[2];
    if (GetConsoleProcessList(consoleProcs, 2) <= 1) {
        std::wcout << L"\nPress Enter to exit...";
        std::wstring dummy;
        std::getline(std::wcin, dummy);
    }
}

int Run(const Options& opt) {
    std::wcout << L"Margaret Injector v" << kVersion << L"\n\n";

    if (opt.list) {
        const auto procs = margaret::FindMinecraftProcesses();
        if (procs.empty()) {
            std::wcout << L"No Minecraft instances found.\n";
        } else {
            PrintProcesses(procs);
        }
        return 0;
    }

    const std::wstring dll = opt.dllPath.empty() ? ExeDirectory() + L"\\" + kDefaultDll : opt.dllPath;
    std::wstring error;
    if (!margaret::ValidateDll(dll, error)) {
        std::wcerr << L"[!] " << error << L"\n";
        return 1;
    }
    std::wcout << L"[*] DLL: " << dll << L"\n";

    const DWORD pid = SelectTarget(opt);
    if (pid == 0) return 1;

    const margaret::InjectResult result = margaret::InjectDll(pid, dll);
    if (!result.ok) {
        std::wcerr << L"[!] " << result.message << L"\n";
        return 1;
    }
    std::wcout << L"[+] " << result.message << L"\n";
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleTitleW(L"Margaret Injector");

    Options opt;
    if (!ParseArgs(argc, argv, opt)) {
        PrintUsage();
        return 2;
    }
    if (opt.help) {
        PrintUsage();
        return 0;
    }

    const int code = Run(opt);
    PauseIfNeeded(opt);
    return code;
}
