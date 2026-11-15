#include "host/tray_icon.hpp"
#include "host/host_messages.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <shellapi.h>
#include <vector>

namespace liteime::host {

namespace {
constexpr UINT kTrayIconId = 1001;

HICON createTrayBadgeIcon(const EngineConfig& cfg) {
    constexpr int kSz = 32;
    uint8_t bgR = 37, bgG = 99, bgB = 235;
    if (!cfg.chineseMode) {
        bgR = 71; bgG = 85; bgB = 105;
    } else {
        switch (cfg.skin) {
            case SkinTheme::MatchaJade:  bgR = 15;  bgG = 118; bgB = 110; break;
            case SkinTheme::SakuraCream: bgR = 219; bgG = 39;  bgB = 119; break;
            case SkinTheme::ObsidianInk: bgR = 2;   bgG = 132; bgB = 199; break;
            case SkinTheme::CloudMist:
            default:                     bgR = 37;  bgG = 99;  bgB = 235; break;
        }
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = kSz;
    bmi.bmiHeader.biHeight = -kSz;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* txtBits = nullptr;
    void* outBits = nullptr;
    HDC screenDC = GetDC(nullptr);
    HBITMAP txtBmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &txtBits, nullptr, 0);
    HBITMAP outBmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &outBits, nullptr, 0);
    HDC memDC = CreateCompatibleDC(screenDC);
    if (screenDC) ReleaseDC(nullptr, screenDC);

    if (!txtBmp || !outBmp || !memDC || !txtBits || !outBits) {
        if (memDC) DeleteDC(memDC);
        if (txtBmp) DeleteObject(txtBmp);
        if (outBmp) DeleteObject(outBmp);
        return nullptr;
    }

    std::memset(txtBits, 0, kSz * kSz * 4);
    HGDIOBJ oldBmp = SelectObject(memDC, txtBmp);
    HFONT font = CreateFontW(
        cfg.chineseMode ? -21 : -16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
    HGDIOBJ oldFont = SelectObject(memDC, font);
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(255, 255, 255));
    RECT rc{0, 0, kSz, kSz};
    const wchar_t* glyph = cfg.chineseMode ? L"\u4e2d" : L"EN";
    DrawTextW(memDC, glyph, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    GdiFlush();
    SelectObject(memDC, oldFont);
    SelectObject(memDC, oldBmp);
    if (font) DeleteObject(font);
    DeleteDC(memDC);

    const auto* tpx = static_cast<const uint32_t*>(txtBits);
    auto* opx = static_cast<uint32_t*>(outBits);
    const float rad = cfg.squareFrame ? 6.0f : 13.5f;
    constexpr float kHalf = 13.5f;
    constexpr float kCenter = 15.5f;

    for (int y = 0; y < kSz; ++y) {
        for (int x = 0; x < kSz; ++x) {
            const float px = std::abs(static_cast<float>(x) - kCenter) - (kHalf - rad);
            const float py = std::abs(static_cast<float>(y) - kCenter) - (kHalf - rad);
            const float ex = std::max(px, 0.0f);
            const float ey = std::max(py, 0.0f);
            const float dist = std::sqrt(ex * ex + ey * ey) + std::min(std::max(px, py), 0.0f) - rad;
            const float alpha = std::clamp(0.5f - dist, 0.0f, 1.0f);

            const uint32_t tp = tpx[y * kSz + x];
            const uint8_t cov8 = static_cast<uint8_t>(
                std::max({tp & 0xFFu, (tp >> 8) & 0xFFu, (tp >> 16) & 0xFFu}));
            const float cov = static_cast<float>(cov8) / 255.0f;

            const float r = static_cast<float>(bgR) + (255.0f - static_cast<float>(bgR)) * cov;
            const float g = static_cast<float>(bgG) + (255.0f - static_cast<float>(bgG)) * cov;
            const float b = static_cast<float>(bgB) + (255.0f - static_cast<float>(bgB)) * cov;

            const uint8_t a8 = static_cast<uint8_t>(std::lround(alpha * 255.0f));
            const uint8_t r8 = static_cast<uint8_t>(std::lround(r * alpha));
            const uint8_t g8 = static_cast<uint8_t>(std::lround(g * alpha));
            const uint8_t b8 = static_cast<uint8_t>(std::lround(b * alpha));
            opx[y * kSz + x] = (static_cast<uint32_t>(a8) << 24) |
                               (static_cast<uint32_t>(r8) << 16) |
                               (static_cast<uint32_t>(g8) << 8) |
                               static_cast<uint32_t>(b8);
        }
    }

    std::vector<uint8_t> maskBits(kSz * 4, 0);
    HBITMAP maskBmp = CreateBitmap(kSz, kSz, 1, 1, maskBits.data());
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = maskBmp;
    ii.hbmColor = outBmp;
    HICON icon = CreateIconIndirect(&ii);

    if (maskBmp) DeleteObject(maskBmp);
    DeleteObject(txtBmp);
    DeleteObject(outBmp);
    return icon;
}

} // namespace

TrayIcon::~TrayIcon() {
    destroy();
}

bool TrayIcon::create(HWND hostHwnd, HINSTANCE) {
    hostHwnd_ = hostHwnd;
    EngineConfig defCfg{};
    HICON initialIcon = createTrayBadgeIcon(defCfg);

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hostHwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY_CALLBACK;
    nid.hIcon = initialIcon ? initialIcon : LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"LiteIME \u6781\u7b80\u53cc\u8bed\u8f93\u5165\u6cd5");
    added_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    if (added_ && initialIcon) {
        icon_ = initialIcon;
    } else if (initialIcon) {
        DestroyIcon(initialIcon);
    }
    return added_;
}

void TrayIcon::destroy() {
    if (added_ && hostHwnd_) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hostHwnd_;
        nid.uID = kTrayIconId;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        added_ = false;
    }
    if (icon_) {
        DestroyIcon(icon_);
        icon_ = nullptr;
    }
}

void TrayIcon::updateTip(const EngineConfig& cfg) {
    if (!added_ || !hostHwnd_) return;
    if (hasLastTip_ &&
        lastTipChinese_ == cfg.chineseMode &&
        lastTipSkin_ == cfg.skin &&
        lastTipSquare_ == cfg.squareFrame) {
        return;
    }
    HICON nextIcon = createTrayBadgeIcon(cfg);
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hostHwnd_;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_TIP | (nextIcon ? NIF_ICON : 0);
    if (nextIcon) nid.hIcon = nextIcon;
    wsprintfW(nid.szTip, L"LiteIME [%s]", cfg.chineseMode ? L"\u4e2d\u6587" : L"English");
    if (Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        if (nextIcon) {
            if (icon_) DestroyIcon(icon_);
            icon_ = nextIcon;
        }
        hasLastTip_ = true;
        lastTipChinese_ = cfg.chineseMode;
        lastTipSkin_ = cfg.skin;
        lastTipSquare_ = cfg.squareFrame;
    } else if (nextIcon) {
        DestroyIcon(nextIcon);
    }
}

void TrayIcon::showContextMenu(const EngineConfig& cfg) {
    if (!hostHwnd_) return;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING | (cfg.chineseMode ? MF_CHECKED : 0),
                static_cast<UINT_PTR>(HostAction::ToggleChineseEnglish),
                L"\u4e2d\u6587\u6a21\u5f0f (Shift)");
    AppendMenuW(menu, MF_STRING | (cfg.chinesePunct ? MF_CHECKED : 0),
                static_cast<UINT_PTR>(HostAction::TogglePunct),
                L"\u4e2d\u6587\u6807\u70b9");
    AppendMenuW(menu, MF_STRING | (cfg.mixedEnglish ? MF_CHECKED : 0),
                static_cast<UINT_PTR>(HostAction::ToggleMixedEnglish),
                L"\u663e\u793a\u5e38\u7528\u82f1\u6587\u5355\u8bcd (\u4e2d\u82f1\u6df7\u8f93)");
    AppendMenuW(menu, MF_STRING | (cfg.layout == CandidateLayout::Vertical ? MF_CHECKED : 0),
                static_cast<UINT_PTR>(HostAction::ToggleLayout),
                L"\u7ad6\u6392\u5019\u9009\u6846");
    AppendMenuW(menu, MF_STRING | (cfg.squareFrame ? MF_CHECKED : 0),
                static_cast<UINT_PTR>(HostAction::ToggleSquareFrame),
                L"\u65b9\u6846\u6837\u5f0f (\u5173\u95ed\u5219\u4e3a\u80f6\u56ca)");

    // Inline preedit mode submenu (inside target input box before final commit)
    HMENU preeditMenu = CreatePopupMenu();
    if (preeditMenu) {
        AppendMenuW(preeditMenu, MF_STRING | (cfg.inlinePreedit == InlinePreeditMode::CandidatePreview ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(HostAction::SetPreeditPreview),
                    L"\u9996\u9009\u6c49\u5b57\u9884\u89c8 (\u9ed8\u8ba4)");
        AppendMenuW(preeditMenu, MF_STRING | (cfg.inlinePreedit == InlinePreeditMode::CommittedPlusPinyin ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(HostAction::SetPreeditPinyin),
                    L"\u5df2\u786e\u8ba4\u5b57 + \u672a\u786e\u8ba4\u62fc\u97f3");
        AppendMenuW(preeditMenu, MF_STRING | (cfg.inlinePreedit == InlinePreeditMode::None ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(HostAction::SetPreeditNone),
                    L"\u4e0d\u51fa\u73b0\u5b57 (\u4ec5\u60ac\u6d6e\u5019\u9009\u6846)");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(preeditMenu), L"\u8f93\u5165\u6846\u5185\u663e\u793a");
    }

    // Candidate page size submenu (5..8)
    HMENU pageMenu = CreatePopupMenu();
    if (pageMenu) {
        AppendMenuW(pageMenu, MF_STRING | (cfg.pageSize == 5 ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(HostAction::SetPageSize5), L"5 \u4e2a\u5019\u9009\u8bcd (\u9ed8\u8ba4)");
        AppendMenuW(pageMenu, MF_STRING | (cfg.pageSize == 6 ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(HostAction::SetPageSize6), L"6 \u4e2a\u5019\u9009\u8bcd");
        AppendMenuW(pageMenu, MF_STRING | (cfg.pageSize == 7 ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(HostAction::SetPageSize7), L"7 \u4e2a\u5019\u9009\u8bcd");
        AppendMenuW(pageMenu, MF_STRING | (cfg.pageSize == 8 ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(HostAction::SetPageSize8), L"8 \u4e2a\u5019\u9009\u8bcd");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(pageMenu), L"\u6bcf\u9875\u5019\u9009\u6570\u91cf (5~8)");
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, MF_STRING | (cfg.skin == SkinTheme::CloudMist ? MF_CHECKED : 0),
                static_cast<UINT_PTR>(HostAction::SetSkinCloudMist),
                L"\u76ae\u80a4: \u4e91\u96fe\u7409\u7483 (\u9ed8\u8ba4)");
    AppendMenuW(menu, MF_STRING | (cfg.skin == SkinTheme::MatchaJade ? MF_CHECKED : 0),
                static_cast<UINT_PTR>(HostAction::SetSkinMatchaJade),
                L"\u76ae\u80a4: \u82cd\u7fe0\u7af9\u9752");
    AppendMenuW(menu, MF_STRING | (cfg.skin == SkinTheme::SakuraCream ? MF_CHECKED : 0),
                static_cast<UINT_PTR>(HostAction::SetSkinSakuraCream),
                L"\u76ae\u80a4: \u6a31\u7c89\u67d4\u96fe");
    AppendMenuW(menu, MF_STRING | (cfg.skin == SkinTheme::ObsidianInk ? MF_CHECKED : 0),
                static_cast<UINT_PTR>(HostAction::SetSkinObsidianInk),
                L"\u76ae\u80a4: \u661f\u66dc\u7384\u591c");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // Fuzzy Pinyin & Auto-Typo submenu
    HMENU fuzzyMenu = CreatePopupMenu();
    if (fuzzyMenu) {
        AppendMenuW(fuzzyMenu, MF_STRING | (cfg.fuzzy.r_l ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(HostAction::ToggleFuzzyRL),
                    L"\u6a21\u7cca\u97f3: r <-> l");
        AppendMenuW(fuzzyMenu, MF_STRING | (cfg.fuzzy.zh_z ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(HostAction::ToggleFuzzyConsonants),
                    L"\u6a21\u7cca\u97f3: zh/ch/sh <-> z/c/s");
        AppendMenuW(fuzzyMenu, MF_STRING | (cfg.fuzzy.n_l ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(HostAction::ToggleFuzzyNasals),
                    L"\u6a21\u7cca\u97f3: n/l + an/en/in <-> ang/eng/ing");
        AppendMenuW(fuzzyMenu, MF_STRING | (cfg.fuzzy.f_h ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(HostAction::ToggleFuzzyFH),
                    L"\u6a21\u7cca\u97f3: f <-> h");
        AppendMenuW(fuzzyMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fuzzyMenu, MF_STRING | (cfg.typoCorrection ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(HostAction::ToggleTypoCorrection),
                    L"\u660e\u663e\u9519\u62fc\u81ea\u52a8\u7ea0\u6b63 (zhogn/zhog -> zhong)");
        AppendMenuW(fuzzyMenu, MF_STRING | (cfg.looseInitialMatch ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(HostAction::ToggleLooseInitial),
                    L"\u5168\u62fc\u9644\u5e26\u9996\u5b57\u6bcd\u9ad8\u9891\u8bcd (\u6700\u591a2\u4e2a\uff0c\u9ed8\u8ba4\u5173\u95ed)");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(fuzzyMenu), L"\u6a21\u7cca\u97f3\u4e0e\u7ea0\u9519\u8bbe\u7f6e");
    }

    // Domain Packs & Custom Dict submenu
    HMENU dictMenu = CreatePopupMenu();
    if (dictMenu) {
        AppendMenuW(dictMenu, MF_STRING | (cfg.enableEnPack ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(HostAction::TogglePackCommonEn),
                    L"\u5e38\u7528\u82f1\u6587\u9ad8\u9891\u8bcd\u5e93 (1000+)");
        AppendMenuW(dictMenu, MF_STRING | (cfg.enableCsPack ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(HostAction::TogglePackComputer),
                    L"\u8ba1\u7b97\u673a\u4e13\u4e1a\u8bcd\u5e93 (CS/408/AI)");
        AppendMenuW(dictMenu, MF_STRING | (cfg.enableProPack ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(HostAction::TogglePackProFields),
                    L"\u4e13\u4e1a\u6269\u5c55\u8bcd\u5e93 (\u8d22\u7ecf/\u533b\u5b66/\u6cd5\u5f8b)");
        AppendMenuW(dictMenu, MF_STRING | (cfg.enableExtPack ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(HostAction::TogglePackExtCn),
                    L"\u4e2d\u6587\u6269\u5c55\u5927\u8bcd\u5e93 (33\u4e07+\u8bcd\u6761)");
        AppendMenuW(dictMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(dictMenu, MF_STRING,
                    static_cast<UINT_PTR>(HostAction::OpenCustomDictFolder),
                    L"\u6253\u5f00\u81ea\u5b9a\u4e49\u8bcd\u5e93\u76ee\u5f55...");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(dictMenu), L"\u6269\u5c55\u4e0e\u4e13\u4e1a\u8bcd\u5e93 (\u6309\u9700\u5f00\u542f)");
    }

    AppendMenuW(menu, MF_STRING,
                static_cast<UINT_PTR>(HostAction::OpenQuickInputManager),
                L"\u7ba1\u7406\u300c\u5feb\u8f93\u5165 (vvv)\u300d...");

    AppendMenuW(menu, MF_STRING | (cfg.showStatusBar ? MF_CHECKED : 0),
                static_cast<UINT_PTR>(HostAction::ToggleStatusBar),
                L"\u663e\u793a\u60ac\u6d6e\u72b6\u6001\u680f");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(HostAction::ReloadDict), L"\u91cd\u65b0\u52a0\u8f7d\u8bcd\u5e93");
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(HostAction::ExitHost), L"\u9000\u51fa LiteIMEHost");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hostHwnd_);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hostHwnd_, nullptr);
    PostMessageW(hostHwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (cmd != 0) {
        PostMessageW(hostHwnd_, WM_APP_ENGINE_ACTION, static_cast<WPARAM>(cmd), 0);
    }
}

} // namespace liteime::host
