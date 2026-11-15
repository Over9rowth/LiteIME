#pragma once

#include <windows.h>

namespace liteime::host {

constexpr UINT WM_APP_SYNC_UI = WM_APP + 101;
constexpr UINT WM_APP_TRAY_CALLBACK = WM_APP + 102;
constexpr UINT WM_APP_ENGINE_ACTION = WM_APP + 103;
constexpr UINT WM_APP_PIN_CANDIDATE = WM_APP + 105;
constexpr UINT WM_APP_OPEN_QUICK_INPUT = WM_APP + 106;
constexpr UINT WM_APP_REMOVE_USER_WORD = WM_APP + 107;
constexpr UINT WM_APP_STATUS_POS_CHANGED = WM_APP + 108;
constexpr UINT WM_APP_CANDIDATE_CLICK = WM_APP + 109;
constexpr UINT WM_APP_CANDIDATE_PAGE = WM_APP + 110;

enum class HostAction : WPARAM {
    ToggleChineseEnglish = 1,
    TogglePunct = 2,
    ToggleLayout = 3,
    CycleSkin = 4,
    SetSkinCloudMist = 5,
    SetSkinMatchaJade = 6,
    SetSkinSakuraCream = 7,
    ToggleFuzzyConsonants = 8,
    ToggleFuzzyNasals = 9,
    ToggleStatusBar = 10,
    ReloadDict = 11,
    ExitHost = 12,
    SetPageSize5 = 20,
    SetPageSize6 = 21,
    SetPageSize7 = 22,
    SetPageSize8 = 23,
    ToggleFuzzyRL = 24,
    ToggleFuzzyFH = 25,
    ToggleTypoCorrection = 26,
    TogglePackComputer = 27,
    TogglePackExtCn = 28,
    TogglePackProFields = 29,
    OpenCustomDictFolder = 30,
    OpenQuickInputManager = 31,
    SetSkinObsidianInk = 32,
    ToggleSquareFrame = 33,
    SetPreeditPreview = 34,
    SetPreeditPinyin = 35,
    SetPreeditNone = 36,
    ToggleMixedEnglish = 37,
    TogglePackCommonEn = 38,
    ToggleLooseInitial = 39,
};

inline UINT queryDpiForWindow(HWND hwnd) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using FnGetDpi = UINT(WINAPI*)(HWND);
        auto fn = reinterpret_cast<FnGetDpi>(reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow")));
        if (fn && hwnd) {
            UINT d = fn(hwnd);
            if (d > 0) return d;
        }
    }
    HDC hdc = GetDC(hwnd);
    UINT dpi = hdc ? static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX)) : 96;
    if (hdc) ReleaseDC(hwnd, hdc);
    return dpi > 0 ? dpi : 96;
}

} // namespace liteime::host
