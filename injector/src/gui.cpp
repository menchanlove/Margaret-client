// Margaret Injector - GUI front-end (Windows x64).
//
// A small dark-themed window: pick a running Minecraft: Bedrock Edition
// instance, choose the DLL, and inject. Reuses the core logic in
// inject.cpp / process.cpp / acl.cpp.

#include <windows.h>

#include <commdlg.h>

#include <memory>
#include <string>
#include <vector>

#include "inject.h"
#include "process.h"

namespace {

constexpr const wchar_t* kAppTitle = L"Margaret Injector";
constexpr const wchar_t* kVersion = L"1.0.0";
constexpr const wchar_t* kDefaultDll = L"Margaret.dll";

// Palette (dark theme, Horion-style reference).
constexpr COLORREF kBg = RGB(26, 26, 28);
constexpr COLORREF kPanel = RGB(38, 38, 42);
constexpr COLORREF kAccent = RGB(93, 200, 232);   // cyan script logo
constexpr COLORREF kGreen = RGB(76, 175, 92);      // inject button
constexpr COLORREF kGreenDim = RGB(58, 128, 70);
constexpr COLORREF kText = RGB(236, 236, 238);
constexpr COLORREF kSub = RGB(140, 140, 150);
constexpr COLORREF kBtn = RGB(50, 50, 58);
constexpr COLORREF kBtnHot = RGB(66, 66, 76);

// Control IDs.
enum {
    IDC_LIST = 1001,
    IDC_REFRESH,
    IDC_DLL,
    IDC_BROWSE,
    IDC_INJECT,
    IDC_STATUS,
    IDC_BANNER,
    IDC_SUBTITLE,
    IDC_LIST_LABEL,
    IDC_DLL_LABEL,
};

constexpr UINT WM_APP_RESULT = WM_APP + 1;

HFONT g_fontTitle = nullptr;
HFONT g_fontBody = nullptr;
HFONT g_fontBtn = nullptr;
HBRUSH g_brushBg = nullptr;
HBRUSH g_brushPanel = nullptr;
HBRUSH g_brushBtn = nullptr;

HWND g_list = nullptr;
HWND g_dllEdit = nullptr;
HWND g_status = nullptr;
HWND g_injectBtn = nullptr;

std::vector<DWORD> g_pids;  // parallel to listbox items

struct InjectJob {
    HWND hwnd;
    DWORD pid;
    std::wstring dll;
};

std::wstring ExeDirectory() {
    wchar_t buf[MAX_PATH * 4] = {};
    const DWORD len = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
    std::wstring path(buf, len);
    const size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : path.substr(0, pos);
}

std::wstring GetEditText(HWND edit) {
    const int len = GetWindowTextLengthW(edit);
    std::wstring s(len, L'\0');
    if (len) GetWindowTextW(edit, &s[0], len + 1);
    return s;
}

void SetStatus(const std::wstring& text, COLORREF color) {
    // Color is applied in WM_CTLCOLORSTATIC via the stored value.
    SetWindowLongPtrW(g_status, GWLP_USERDATA, static_cast<LONG_PTR>(color));
    SetWindowTextW(g_status, text.c_str());
    InvalidateRect(g_status, nullptr, TRUE);
}

void RefreshList() {
    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    g_pids.clear();

    const auto procs = margaret::FindMinecraftProcesses();
    for (const auto& p : procs) {
        std::wstring label = L"PID " + std::to_wstring(p.pid) + L"   " +
                             (p.windowTitle.empty() ? p.exeName : p.windowTitle);
        SendMessageW(g_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        g_pids.push_back(p.pid);
    }

    if (g_pids.empty()) {
        SendMessageW(g_list, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"(Bedrock Edition が見つかりません - 起動してから Refresh)"));
        EnableWindow(g_injectBtn, FALSE);
        SetStatus(L"Minecraft: Bedrock Edition を起動してください。", kSub);
    } else {
        SendMessageW(g_list, LB_SETCURSEL, 0, 0);
        EnableWindow(g_injectBtn, TRUE);
        SetStatus(std::to_wstring(g_pids.size()) + L" 個のインスタンスを検出しました。", kSub);
    }
}

void BrowseForDll(HWND hwnd) {
    wchar_t file[MAX_PATH * 4] = {};
    std::wstring current = GetEditText(g_dllEdit);
    if (!current.empty() && current.size() < sizeof(file) / sizeof(file[0])) {
        wcscpy_s(file, current.c_str());
    }

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"DLL files\0*.dll\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = static_cast<DWORD>(sizeof(file) / sizeof(file[0]));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle = L"注入する DLL を選択";

    if (GetOpenFileNameW(&ofn)) {
        SetWindowTextW(g_dllEdit, file);
    }
}

DWORD WINAPI InjectThread(LPVOID param) {
    std::unique_ptr<InjectJob> job(static_cast<InjectJob*>(param));
    auto* result = new margaret::InjectResult(margaret::InjectDll(job->pid, job->dll));
    PostMessageW(job->hwnd, WM_APP_RESULT, 0, reinterpret_cast<LPARAM>(result));
    return 0;
}

void StartInject(HWND hwnd) {
    const LRESULT sel = SendMessageW(g_list, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR || sel < 0 || static_cast<size_t>(sel) >= g_pids.size()) {
        SetStatus(L"注入先の Minecraft を選択してください。", kAccent);
        return;
    }

    std::wstring dll = GetEditText(g_dllEdit);
    if (dll.empty()) {
        SetStatus(L"DLL を指定してください。", kAccent);
        return;
    }

    std::wstring error;
    if (!margaret::ValidateDll(dll, error)) {
        SetStatus(error, kAccent);
        return;
    }

    EnableWindow(g_injectBtn, FALSE);
    SetStatus(L"注入中...", kText);

    auto* job = new InjectJob{hwnd, g_pids[sel], dll};
    HANDLE th = CreateThread(nullptr, 0, InjectThread, job, 0, nullptr);
    if (th) {
        CloseHandle(th);
    } else {
        delete job;
        EnableWindow(g_injectBtn, TRUE);
        SetStatus(L"スレッドの作成に失敗しました。", kAccent);
    }
}

HWND MakeLabel(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h, HFONT font,
               DWORD extra = 0) {
    HWND c = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | extra, x, y, w, h, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return c;
}

HWND MakeButton(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    HWND c = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, x, y, w, h, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontBtn), TRUE);
    return c;
}

void CreateControls(HWND hwnd) {
    // Banner (cyan script logo, centred).
    HWND banner = MakeLabel(hwnd, IDC_BANNER, L"Margaret", 24, 16, 452, 56, g_fontTitle, SS_CENTER);
    SetWindowLongPtrW(banner, GWLP_USERDATA, static_cast<LONG_PTR>(kAccent));
    HWND sub = MakeLabel(hwnd, IDC_SUBTITLE, L"Bedrock Edition Injector", 24, 74, 452, 20, g_fontBody,
                         SS_CENTER);
    SetWindowLongPtrW(sub, GWLP_USERDATA, static_cast<LONG_PTR>(kSub));

    MakeLabel(hwnd, IDC_LIST_LABEL, L"検出された Minecraft", 24, 94, 300, 18, g_fontBody);

    g_list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
                             WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY, 24, 114, 352,
                             110, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LIST)), nullptr, nullptr);
    SendMessageW(g_list, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontBody), TRUE);

    MakeButton(hwnd, IDC_REFRESH, L"Refresh", 386, 114, 90, 32);

    MakeLabel(hwnd, IDC_DLL_LABEL, L"DLL", 24, 240, 100, 18, g_fontBody);
    g_dllEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", (ExeDirectory() + L"\\" + kDefaultDll).c_str(),
                                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 24, 260, 352, 28, hwnd,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_DLL)), nullptr, nullptr);
    SendMessageW(g_dllEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontBody), TRUE);
    MakeButton(hwnd, IDC_BROWSE, L"...", 386, 260, 90, 28);

    g_injectBtn = MakeButton(hwnd, IDC_INJECT, L"inject", 24, 304, 452, 46);

    g_status = MakeLabel(hwnd, IDC_STATUS, L"", 24, 358, 452, 40, g_fontBody);
    SetWindowLongPtrW(g_status, GWLP_USERDATA, static_cast<LONG_PTR>(kSub));
}

void DrawButton(LPDRAWITEMSTRUCT dis) {
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const bool isInject = dis->CtlID == IDC_INJECT;

    COLORREF fill = isInject ? kGreen : kBtn;
    if (isInject && pressed) fill = kGreenDim;
    if (!isInject && pressed) fill = kBtnHot;
    if (disabled) fill = isInject ? kGreenDim : RGB(40, 40, 50);

    HBRUSH brush = CreateSolidBrush(fill);
    FillRect(dis->hDC, &dis->rcItem, brush);
    DeleteObject(brush);

    wchar_t text[64] = {};
    GetWindowTextW(dis->hwndItem, text, 64);

    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, isInject ? RGB(240, 248, 240) : (disabled ? kSub : kText));
    SelectObject(dis->hDC, g_fontBtn);
    DrawTextW(dis->hDC, text, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            CreateControls(hwnd);
            RefreshList();
            return 0;

        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            HWND ctl = reinterpret_cast<HWND>(lParam);
            SetBkMode(dc, TRANSPARENT);
            COLORREF color = static_cast<COLORREF>(GetWindowLongPtrW(ctl, GWLP_USERDATA));
            SetTextColor(dc, color ? color : kText);
            return reinterpret_cast<LRESULT>(g_brushBg);
        }

        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkColor(dc, kPanel);
            SetTextColor(dc, kText);
            return reinterpret_cast<LRESULT>(g_brushPanel);
        }

        case WM_DRAWITEM:
            DrawButton(reinterpret_cast<LPDRAWITEMSTRUCT>(lParam));
            return TRUE;

        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == IDC_REFRESH) {
                RefreshList();
            } else if (id == IDC_BROWSE) {
                BrowseForDll(hwnd);
            } else if (id == IDC_INJECT) {
                StartInject(hwnd);
            } else if (id == IDC_LIST && HIWORD(wParam) == LBN_DBLCLK) {
                StartInject(hwnd);
            }
            return 0;
        }

        case WM_APP_RESULT: {
            std::unique_ptr<margaret::InjectResult> result(reinterpret_cast<margaret::InjectResult*>(lParam));
            SetStatus(result->message, result->ok ? kAccent : RGB(240, 120, 120));
            EnableWindow(g_injectBtn, TRUE);
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow) {
    g_fontTitle = CreateFontW(46, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SCRIPT, L"Segoe Script");
    g_fontBody = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontBtn = CreateFontW(17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_brushBg = CreateSolidBrush(kBg);
    g_brushPanel = CreateSolidBrush(kPanel);
    g_brushBtn = CreateSolidBrush(kBtn);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = g_brushBg;
    wc.lpszClassName = L"MargaretInjectorWindow";
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    const int width = 516, height = 456;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, (std::wstring(kAppTitle) + L"  v" + kVersion).c_str(),
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, x, y, width, height,
                                nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG m{};
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }

    DeleteObject(g_fontTitle);
    DeleteObject(g_fontBody);
    DeleteObject(g_fontBtn);
    DeleteObject(g_brushBg);
    DeleteObject(g_brushPanel);
    DeleteObject(g_brushBtn);
    return 0;
}
