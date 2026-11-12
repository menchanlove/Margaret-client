#include "acl.h"

#include <aclapi.h>
#include <sddl.h>

namespace margaret {
namespace {

// Local free / allocation guards.
struct LocalFreeGuard {
    void* p = nullptr;
    ~LocalFreeGuard() {
        if (p) LocalFree(p);
    }
};

std::wstring Win32Msg(DWORD code) {
    wchar_t* buf = nullptr;
    const DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring text = (len && buf) ? std::wstring(buf, len) : L"error";
    if (buf) LocalFree(buf);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) text.pop_back();
    return text + L" (" + std::to_wstring(code) + L")";
}

}  // namespace

bool HasAppContainerAccess(const std::wstring& filePath) {
    PSID appSid = nullptr;
    if (!ConvertStringSidToSidW(L"S-1-15-2-1", &appSid)) {
        return false;
    }
    LocalFreeGuard sidGuard{appSid};

    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (GetNamedSecurityInfoW(filePath.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                              &dacl, nullptr, &sd) != ERROR_SUCCESS) {
        return false;
    }
    LocalFreeGuard sdGuard{sd};

    if (!dacl) return false;

    for (WORD i = 0; i < dacl->AceCount; ++i) {
        void* aceRaw = nullptr;
        if (!GetAce(dacl, i, &aceRaw)) continue;
        auto* header = static_cast<ACE_HEADER*>(aceRaw);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) continue;
        auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(aceRaw);
        PSID aceSid = &ace->SidStart;
        if (EqualSid(aceSid, appSid) && (ace->Mask & (GENERIC_READ | GENERIC_EXECUTE)) != 0) {
            return true;
        }
    }
    return false;
}

bool GrantAppContainerAccess(const std::wstring& filePath, std::wstring& error) {
    if (HasAppContainerAccess(filePath)) {
        return true;
    }

    PSID appSid = nullptr;
    if (!ConvertStringSidToSidW(L"S-1-15-2-1", &appSid)) {
        error = L"Failed to build the AppContainer SID: " + Win32Msg(GetLastError());
        return false;
    }
    LocalFreeGuard sidGuard{appSid};

    PACL oldDacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    DWORD rc = GetNamedSecurityInfoW(filePath.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
                                     nullptr, &oldDacl, nullptr, &sd);
    if (rc != ERROR_SUCCESS) {
        error = L"GetNamedSecurityInfo failed: " + Win32Msg(rc);
        return false;
    }
    LocalFreeGuard sdGuard{sd};

    EXPLICIT_ACCESSW ea{};
    ea.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
    ea.grfAccessMode = GRANT_ACCESS;
    ea.grfInheritance = NO_INHERITANCE;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea.Trustee.ptstrName = static_cast<LPWSTR>(appSid);

    PACL newDacl = nullptr;
    rc = SetEntriesInAclW(1, &ea, oldDacl, &newDacl);
    if (rc != ERROR_SUCCESS || !newDacl) {
        error = L"SetEntriesInAcl failed: " + Win32Msg(rc);
        return false;
    }
    LocalFreeGuard daclGuard{newDacl};

    rc = SetNamedSecurityInfoW(const_cast<LPWSTR>(filePath.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                               nullptr, nullptr, newDacl, nullptr);
    if (rc != ERROR_SUCCESS) {
        error = L"SetNamedSecurityInfo failed: " + Win32Msg(rc) +
                L". Make sure you own the DLL file and it is not read-only.";
        return false;
    }
    return true;
}

}  // namespace margaret
