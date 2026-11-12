#include "inject.h"

#include "acl.h"
#include "process.h"


namespace margaret {
namespace {

// Closes a HANDLE on scope exit.
struct HandleGuard {
    HANDLE h = nullptr;
    explicit HandleGuard(HANDLE handle) : h(handle) {}
    ~HandleGuard() {
        if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    HandleGuard(const HandleGuard&) = delete;
    HandleGuard& operator=(const HandleGuard&) = delete;
};

// Frees remote memory on scope exit.
struct RemoteMemory {
    HANDLE process;
    void* address = nullptr;
    RemoteMemory(HANDLE p, void* a) : process(p), address(a) {}
    ~RemoteMemory() {
        if (address) VirtualFreeEx(process, address, 0, MEM_RELEASE);
    }
    RemoteMemory(const RemoteMemory&) = delete;
    RemoteMemory& operator=(const RemoteMemory&) = delete;
};

std::wstring FileNameOf(const std::wstring& path) {
    const size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

InjectResult Fail(const std::wstring& what, DWORD code) {
    return {false, what + L": " + Win32ErrorText(code) + L" (" + std::to_wstring(code) + L")"};
}

}  // namespace

std::wstring Win32ErrorText(DWORD code) {
    wchar_t* buf = nullptr;
    const DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buf), 0,
        nullptr);
    std::wstring text = (len && buf) ? std::wstring(buf, len) : L"Unknown error";
    if (buf) LocalFree(buf);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) {
        text.pop_back();
    }
    return text;
}

bool ValidateDll(const std::wstring& path, std::wstring& error) {
    HandleGuard file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.h == INVALID_HANDLE_VALUE) {
        error = L"DLL not found: " + path;
        return false;
    }

    auto readAt = [&](LONG offset, void* out, DWORD size) {
        DWORD read = 0;
        return SetFilePointer(file.h, offset, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
               ReadFile(file.h, out, size, &read, nullptr) && read == size;
    };

    IMAGE_DOS_HEADER dos{};
    if (!readAt(0, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
        error = L"Not a valid PE file (missing MZ header).";
        return false;
    }

    DWORD signature = 0;
    IMAGE_FILE_HEADER header{};
    if (!readAt(dos.e_lfanew, &signature, sizeof(signature)) || signature != IMAGE_NT_SIGNATURE ||
        !readAt(dos.e_lfanew + static_cast<LONG>(sizeof(signature)), &header, sizeof(header))) {
        error = L"Not a valid PE file (missing PE header).";
        return false;
    }

    if (!(header.Characteristics & IMAGE_FILE_DLL)) {
        error = L"The file is an executable, not a DLL.";
        return false;
    }
    if (header.Machine != IMAGE_FILE_MACHINE_AMD64) {
        error = L"The DLL is not built for x64. Bedrock Edition is a 64-bit app, so build Margaret as x64.";
        return false;
    }
    return true;
}

InjectResult InjectDll(DWORD pid, const std::wstring& dllPath) {
    // Resolve to an absolute path: LoadLibraryW runs with the game's working directory.
    wchar_t fullPath[MAX_PATH * 4] = {};
    const DWORD fullLen = GetFullPathNameW(dllPath.c_str(), static_cast<DWORD>(sizeof(fullPath) / sizeof(fullPath[0])),
                                           fullPath, nullptr);
    if (fullLen == 0 || fullLen >= sizeof(fullPath) / sizeof(fullPath[0])) {
        return Fail(L"Could not resolve the DLL path", GetLastError());
    }

    std::wstring error;
    if (!ValidateDll(fullPath, error)) {
        return {false, error};
    }

    const std::wstring moduleName = FileNameOf(fullPath);
    if (IsModuleLoaded(pid, moduleName)) {
        return {false, moduleName + L" is already loaded in this Minecraft instance."};
    }

    // Bedrock runs in a UWP AppContainer: without this grant, LoadLibraryW
    // inside the sandbox fails with ERROR_ACCESS_DENIED (5).
    if (!GrantAppContainerAccess(fullPath, error)) {
        return {false, error};
    }

    HandleGuard process(OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                        PROCESS_VM_WRITE | PROCESS_VM_READ,
                                    FALSE, pid));
    if (!process.h) {
        return Fail(L"OpenProcess failed (try running the injector as the same user as Minecraft, or as admin)",
                    GetLastError());
    }

    if (!Is64BitProcess(process.h)) {
        return {false, L"The target process is 32-bit, but Bedrock Edition should be 64-bit."};
    }

    const SIZE_T bytes = (wcslen(fullPath) + 1) * sizeof(wchar_t);
    RemoteMemory remote(process.h,
                        VirtualAllocEx(process.h, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!remote.address) {
        return Fail(L"VirtualAllocEx failed", GetLastError());
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(process.h, remote.address, fullPath, bytes, &written) || written != bytes) {
        return Fail(L"WriteProcessMemory failed", GetLastError());
    }

    // kernel32 is mapped at the same base in every process of a session, so the
    // local address of LoadLibraryW is valid in the target too.
    auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW")));
    if (!loadLibrary) {
        return Fail(L"GetProcAddress(LoadLibraryW) failed", GetLastError());
    }

    HandleGuard thread(CreateRemoteThread(process.h, nullptr, 0, loadLibrary, remote.address, 0, nullptr));
    if (!thread.h) {
        return Fail(L"CreateRemoteThread failed", GetLastError());
    }

    const DWORD wait = WaitForSingleObject(thread.h, 15000);
    if (wait != WAIT_OBJECT_0) {
        // Don't free the path buffer while LoadLibraryW may still be reading it.
        remote.address = nullptr;
        return {false, L"Timed out waiting for LoadLibraryW (DllMain may be blocking)."};
    }

    // The thread exit code only holds the low 32 bits of the HMODULE, so check
    // the module list instead to see whether LoadLibraryW succeeded.
    if (!IsModuleLoaded(pid, moduleName)) {
        return {false, L"LoadLibraryW failed inside Minecraft (missing dependency, or DllMain returned FALSE)."};
    }

    return {true, L"Injected " + moduleName + L" into PID " + std::to_wstring(pid) + L"."};
}

}  // namespace margaret
