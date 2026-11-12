// margaretinjector - GUI front-end (Windows x64).
//
// A small dark-themed window: pick a running Minecraft: Bedrock Edition
// instance, choose the DLL, and inject. Text/background colours and the UI
// language (English / Japanese / Chinese, default English) are user-
// configurable and saved to margaretinjector.ini next to the exe.
// Reuses the core logic in inject.cpp / process.cpp / acl.cpp.

#include <windows.h>

#include <commdlg.h>

#include <memory>
#include <string>
#include <vector>

#include "inject.h"
#include "process.h"

namespace {

constexpr const wchar_t* kAppName = L"margaretinjector";
constexpr const wchar_t* kVersion = L"1.0.0";
constexpr const wchar_t* kDefaultDll = L"Margaret.dll";
constexpr const wchar_t* kCredit = L"made by aki12459";

// Fixed palette.
constexpr COLORREF kPanel = RGB(38, 38, 42);
constexpr COLORREF kGreen = RGB(76, 175, 92);   // inject button
constexpr COLORREF kGreenDim = RGB(58, 128, 70);
constexpr COLORREF kSub = RGB(140, 140, 150);
constexpr COLORREF kBtn = RGB(50, 50, 58);
constexpr COLORREF kBtnHot = RGB(66, 66, 76);
constexpr COLORREF kErr = RGB(240, 120, 120);

// User-configurable colours (defaults match the Horion-style look).
COLORREF g_bg = RGB(26, 26, 28);
COLORREF g_text = RGB(93, 200, 232);  // cyan

// ---- Localization -------------------------------------------------------
enum Lang { LANG_EN = 0, LANG_JA = 1, LANG_ZH = 2, LANG_COUNT = 3 };
Lang g_lang = LANG_EN;  // default English

enum Str {
    S_SUBTITLE,
    S_LIST_LABEL,
    S_DLL_LABEL,
    S_REFRESH,
    S_TEXTCOLOR,
    S_BGCOLOR,
    S_INJECT,
    S_NO_INSTANCE,
    S_START_PROMPT,
    S_SELECT,
    S_SPECIFY_DLL,
    S_INJECTING,
    S_THREAD_FAIL,
    S_BROWSE_TITLE,
    S_LANGNAME,
    STR_COUNT
};

// [string][language]
const wchar_t* const kStrings[STR_COUNT][LANG_COUNT] = {
    /* S_SUBTITLE    */ {L"Bedrock Edition Injector", L"Bedrock Edition インジェクター", L"基岩版注入器"},
    /* S_LIST_LABEL  */ {L"Detected Minecraft", L"検出された Minecraft", L"检测到的 Minecraft"},
    /* S_DLL_LABEL   */ {L"DLL", L"DLL", L"DLL"},
    /* S_REFRESH     */ {L"Refresh", L"更新", L"刷新"},
    /* S_TEXTCOLOR   */ {L"Text", L"文字色", L"文字色"},
    /* S_BGCOLOR     */ {L"BG", L"背景色", L"背景色"},
    /* S_INJECT      */ {L"inject", L"注入", L"注入"},
    /* S_NO_INSTANCE */
    {L"(No Bedrock Edition found - start it, then Refresh)",
     L"(Bedrock Edition が見つかりません - 起動してから更新)", L"(未找到基岩版 - 启动后点击刷新)"},
    /* S_START_PROMPT*/
    {L"Please start Minecraft: Bedrock Edition.", L"Minecraft: Bedrock Edition を起動してください。",
     L"请先启动 Minecraft: 基岩版。"},
    /* S_SELECT      */
    {L"Select the Minecraft to inject into.", L"注入先の Minecraft を選択してください。",
     L"请选择要注入的 Minecraft。"},
    /* S_SPECIFY_DLL */ {L"Please specify a DLL.", L"DLL を指定してください。", L"请指定 DLL。"},
    /* S_INJECTING   */ {L"Injecting...", L"注入中...", L"注入中..."},
    /* S_THREAD_FAIL */ {L"Failed to create thread.", L"スレッドの作成に失敗しました。", L"创建线程失败。"},
    /* S_BROWSE_TITLE*/ {L"Select a DLL to inject", L"注入する DLL を選択", L"选择要注入的 DLL"},
    /* S_LANGNAME    */ {L"English", L"日本語", L"中文"},
};

const wchar_t* T(Str s) { return kStrings[s][g_lang]; }

// "N instance(s) detected." localized.
std::wstring DetectedText(size_t n) {
    const std::wstring num = std::to_wstring(n);
    switch (g_lang) {
        case LANG_JA:
            return num + L" 個のインスタンスを検出しました。";
        case LANG_ZH:
            return L"检测到 " + num + L" 个实例。";
        default:
            return num + (n == 1 ? L" instance detected." : L" instances detected.");
    }
}

// ---- Fonts / brushes / control handles ----------------------------------
constexpr UINT WM_APP_RESULT = WM_APP + 1;

HFONT g_fontTitle = nullptr;
HFONT g_fontBody = nullptr;
HFONT g_fontBtn = nullptr;
HFONT g_fontCredit = nullptr;
HBRUSH g_brushBg = nullptr;
HBRUSH g_brushPanel = nullptr;

HWND g_list = nullptr;
HWND g_dllEdit = nullptr;
HWND g_status = nullptr;
HWND g_injectBtn = nullptr;
HWND g_subtitle = nullptr;
HWND g_listLabel = nullptr;
HWND g_dllLabel = nullptr;
HWND g_refreshBtn = nullptr;
HWND g_textBtn = nullptr;
HWND g_bgBtn = nullptr;
HWND g_langBtn = nullptr;

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
    IDC_COLOR_TEXT,
    IDC_COLOR_BG,
    IDC_CREDIT,
    IDC_LANG,
};

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

std::wstring IniPath() { return ExeDirectory() + L"\\margaretinjector.ini"; }

// ---- Settings persistence ----------------------------------------------
void LoadSettings() {
    const std::wstring ini = IniPath();
    wchar_t buf[32] = {};
    auto readColor = [&](const wchar_t* key, COLORREF def) -> COLORREF {
        GetPrivateProfileStringW(L"colors", key, L"", buf, 32, ini.c_str());
        if (buf[0] == L'\0') return def;
        const unsigned long v = wcstoul(buf, nullptr, 16);
        return RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    };
    g_text = readColor(L"text", g_text);
    g_bg = readColor(L"bg", g_bg);

    GetPrivateProfileStringW(L"general", L"lang", L"en", buf, 32, ini.c_str());
    if (!_wcsicmp(buf, L"ja")) {
        g_lang = LANG_JA;
    } else if (!_wcsicmp(buf, L"zh")) {
        g_lang = LANG_ZH;
    } else {
        g_lang = LANG_EN;
    }
}

void SaveColor(const wchar_t* key, COLORREF c) {
    wchar_t buf[16] = {};
    swprintf_s(buf, L"%06lX", (static_cast<unsigned long>(GetRValue(c)) << 16) |
                                  (static_cast<unsigned long>(GetGValue(c)) << 8) | GetBValue(c));
    WritePrivateProfileStringW(L"colors", key, buf, IniPath().c_str());
}

void SaveLang() {
    const wchar_t* code = g_lang == LANG_JA ? L"ja" : (g_lang == LANG_ZH ? L"zh" : L"en");
    WritePrivateProfileStringW(L"general", L"lang", code, IniPath().c_str());
}

std::wstring GetEditText(HWND edit) {
    const int len = GetWindowTextLengthW(edit);
    std::wstring s(len, L'\0');
    if (len) GetWindowTextW(edit, &s[0], len + 1);
    return s;
}

void SetStatus(const std::wstring& text, COLORREF color) {
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
        SendMessageW(g_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(T(S_NO_INSTANCE)));
        EnableWindow(g_injectBtn, FALSE);
        SetStatus(T(S_START_PROMPT), kSub);
    } else {
        SendMessageW(g_list, LB_SETCURSEL, 0, 0);
        EnableWindow(g_injectBtn, TRUE);
        SetStatus(DetectedText(g_pids.size()), kSub);
    }
}

void ApplyLanguage(HWND hwnd) {
    SetWindowTextW(g_subtitle, T(S_SUBTITLE));
    SetWindowTextW(g_listLabel, T(S_LIST_LABEL));
    SetWindowTextW(g_dllLabel, T(S_DLL_LABEL));
    SetWindowTextW(g_refreshBtn, T(S_REFRESH));
    SetWindowTextW(g_textBtn, T(S_TEXTCOLOR));
    SetWindowTextW(g_bgBtn, T(S_BGCOLOR));
    SetWindowTextW(g_injectBtn, T(S_INJECT));
    SetWindowTextW(g_langBtn, T(S_LANGNAME));
    RefreshList();
    InvalidateRect(hwnd, nullptr, TRUE);
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
    ofn.lpstrTitle = T(S_BROWSE_TITLE);

    if (GetOpenFileNameW(&ofn)) {
        SetWindowTextW(g_dllEdit, file);
    }
}

void PickColor(HWND hwnd, COLORREF& target, const wchar_t* iniKey) {
    static COLORREF custom[16] = {};
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hwnd;
    cc.lpCustColors = custom;
    cc.rgbResult = target;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&cc)) {
        target = cc.rgbResult;
        SaveColor(iniKey, target);
        if (&target == &g_bg) {
            DeleteObject(g_brushBg);
            g_brushBg = CreateSolidBrush(g_bg);
        }
        InvalidateRect(hwnd, nullptr, TRUE);
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
        SetStatus(T(S_SELECT), kErr);
        return;
    }

    std::wstring dll = GetEditText(g_dllEdit);
    if (dll.empty()) {
        SetStatus(T(S_SPECIFY_DLL), kErr);
        return;
    }

    std::wstring error;
    if (!margaret::ValidateDll(dll, error)) {
        SetStatus(error, kErr);
        return;
    }

    EnableWindow(g_injectBtn, FALSE);
    SetStatus(T(S_INJECTING), g_text);

    auto* job = new InjectJob{hwnd, g_pids[sel], dll};
    HANDLE th = CreateThread(nullptr, 0, InjectThread, job, 0, nullptr);
    if (th) {
        CloseHandle(th);
    } else {
        delete job;
        EnableWindow(g_injectBtn, TRUE);
        SetStatus(T(S_THREAD_FAIL), kErr);
    }
}

// Labels carry a colour in GWLP_USERDATA: 0 means "follow g_text".
HWND MakeLabel(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h, HFONT font,
               DWORD extra = 0, COLORREF color = 0) {
    HWND c = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | extra, x, y, w, h, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SetWindowLongPtrW(c, GWLP_USERDATA, static_cast<LONG_PTR>(color));
    return c;
}

HWND MakeButton(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    HWND c = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, x, y, w, h, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontBtn), TRUE);
    return c;
}

void CreateControls(HWND hwnd) {
    MakeLabel(hwnd, IDC_BANNER, kAppName, 24, 16, 452, 56, g_fontTitle, SS_CENTER);
    g_subtitle = MakeLabel(hwnd, IDC_SUBTITLE, T(S_SUBTITLE), 24, 76, 452, 20, g_fontBody, SS_CENTER, kSub);

    g_listLabel = MakeLabel(hwnd, IDC_LIST_LABEL, T(S_LIST_LABEL), 24, 100, 300, 18, g_fontBody);

    g_list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
                             WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY, 24, 120, 352,
                             104, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LIST)), nullptr, nullptr);
    SendMessageW(g_list, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontBody), TRUE);

    g_refreshBtn = MakeButton(hwnd, IDC_REFRESH, T(S_REFRESH), 386, 120, 90, 30);
    g_textBtn = MakeButton(hwnd, IDC_COLOR_TEXT, T(S_TEXTCOLOR), 386, 156, 90, 30);
    g_bgBtn = MakeButton(hwnd, IDC_COLOR_BG, T(S_BGCOLOR), 386, 192, 90, 30);

    g_dllLabel = MakeLabel(hwnd, IDC_DLL_LABEL, T(S_DLL_LABEL), 24, 240, 100, 18, g_fontBody);
    g_dllEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", (ExeDirectory() + L"\\" + kDefaultDll).c_str(),
                                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 24, 260, 352, 28, hwnd,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_DLL)), nullptr, nullptr);
    SendMessageW(g_dllEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontBody), TRUE);
    MakeButton(hwnd, IDC_BROWSE, L"...", 386, 260, 90, 28);

    g_injectBtn = MakeButton(hwnd, IDC_INJECT, T(S_INJECT), 24, 304, 452, 46);

    g_status = MakeLabel(hwnd, IDC_STATUS, L"", 24, 360, 452, 36, g_fontBody, 0, kSub);

    g_langBtn = MakeButton(hwnd, IDC_LANG, T(S_LANGNAME), 24, 402, 96, 24);
    MakeLabel(hwnd, IDC_CREDIT, kCredit, 236, 404, 240, 18, g_fontCredit, SS_RIGHT, kSub);
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
    SetTextColor(dis->hDC, isInject ? RGB(240, 248, 240) : (disabled ? kSub : RGB(230, 230, 234)));
    SelectObject(dis->hDC, g_fontBtn);
    DrawTextW(dis->hDC, text, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            CreateControls(hwnd);
            RefreshList();
            return 0;

        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wParam), &rc, g_brushBg);
            return 1;
        }

        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            HWND ctl = reinterpret_cast<HWND>(lParam);
            SetBkMode(dc, TRANSPARENT);
            COLORREF color = static_cast<COLORREF>(GetWindowLongPtrW(ctl, GWLP_USERDATA));
            SetTextColor(dc, color ? color : g_text);
            return reinterpret_cast<LRESULT>(g_brushBg);
        }

        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkColor(dc, kPanel);
            SetTextColor(dc, g_text);
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
            } else if (id == IDC_COLOR_TEXT) {
                PickColor(hwnd, g_text, L"text");
            } else if (id == IDC_COLOR_BG) {
                PickColor(hwnd, g_bg, L"bg");
            } else if (id == IDC_LANG) {
                g_lang = static_cast<Lang>((g_lang + 1) % LANG_COUNT);
                SaveLang();
                ApplyLanguage(hwnd);
            } else if (id == IDC_LIST && HIWORD(wParam) == LBN_DBLCLK) {
                StartInject(hwnd);
            }
            return 0;
        }

        case WM_APP_RESULT: {
            std::unique_ptr<margaret::InjectResult> result(reinterpret_cast<margaret::InjectResult*>(lParam));
            SetStatus(result->message, result->ok ? kGreen : kErr);
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
    LoadSettings();

    g_fontTitle = CreateFontW(46, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SCRIPT, L"Segoe Script");
    g_fontBody = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontBtn = CreateFontW(17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontCredit = CreateFontW(14, 0, 0, 0, FW_NORMAL, TRUE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_brushBg = CreateSolidBrush(g_bg);
    g_brushPanel = CreateSolidBrush(kPanel);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = g_brushBg;
    wc.lpszClassName = L"MargaretInjectorWindow";
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    const int width = 516, height = 476;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, (std::wstring(kAppName) + L"  v" + kVersion).c_str(),
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
    DeleteObject(g_fontCredit);
    DeleteObject(g_brushBg);
    DeleteObject(g_brushPanel);
    return 0;
}
