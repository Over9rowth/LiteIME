#include "tsf/inproc_candidate_window.hpp"
#include "tsf/tsf_compat.hpp"
#include "common/skin_palette.hpp"
#include "common/utf.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace liteime::tsf {

namespace {

constexpr const wchar_t* kInProcCandClassName = L"LiteIME_InProcCandidateWindow";
constexpr float kShadowPad = 14.0f;

UINT queryWindowDpi(HWND hwnd) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using FnGetDpiForWindow = UINT(WINAPI*)(HWND);
        auto fn = reinterpret_cast<FnGetDpiForWindow>(
            reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow")));
        if (fn && hwnd) {
            UINT d = fn(hwnd);
            if (d > 0) return d;
        }
    }
    HDC hdc = GetDC(nullptr);
    UINT d = hdc ? static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX)) : 96;
    if (hdc) ReleaseDC(nullptr, hdc);
    return d > 0 ? d : 96;
}

DWORD queryWindowBand(HWND hwnd) {
    if (!hwnd) return 0;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return 0;
    using FnGetWindowBand = BOOL(WINAPI*)(HWND, DWORD*);
    auto fn = reinterpret_cast<FnGetWindowBand>(
        reinterpret_cast<void*>(GetProcAddress(user32, "GetWindowBand")));
    if (!fn) return 0;
    DWORD band = 0;
    return fn(hwnd, &band) ? band : 0;
}

} // namespace

InProcCandidateWindow::~InProcCandidateWindow() {
    destroy();
}

HWND InProcCandidateWindow::resolveOwnerRoot(uint64_t ownerHint) const {
    auto sameThreadOwned = [](HWND candidate) -> HWND {
        if (!candidate || !IsWindow(candidate)) return nullptr;
        HWND root = GetAncestor(candidate, GA_ROOT);
        if (!root || !IsWindow(root)) root = candidate;
        DWORD pid = 0;
        DWORD tid = GetWindowThreadProcessId(root, &pid);
        if (pid != GetCurrentProcessId() || tid != GetCurrentThreadId()) return nullptr;
        return root;
    };

    if (ownerHint != 0) {
        if (HWND r = sameThreadOwned(reinterpret_cast<HWND>(static_cast<uintptr_t>(ownerHint)))) {
            return r;
        }
    }
    if (HWND r = sameThreadOwned(GetFocus())) return r;
    if (HWND r = sameThreadOwned(GetActiveWindow())) return r;
    if (HWND r = sameThreadOwned(GetForegroundWindow())) return r;
    return nullptr;
}

bool InProcCandidateWindow::ensureWindow(HWND ownerRoot) {
    if (hwnd_ && IsWindow(hwnd_) && ownerRoot_ == ownerRoot) {
        return true;
    }
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    ownerRoot_ = ownerRoot;

    ScopedPerMonitorDpi dpiGuard;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    if (!GetClassInfoExW(g_hInst, kInProcCandClassName, &wc)) {
        wc.lpfnWndProc = InProcCandidateWindow::wndProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kInProcCandClassName;
        RegisterClassExW(&wc);
    }

    const DWORD exStyle = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    const DWORD ownerBand = queryWindowBand(ownerRoot);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");

    if (user32 && ownerBand > 1) {
        using FnCreateWindowInBand = HWND(WINAPI*)(
            DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int,
            HWND, HMENU, HINSTANCE, LPVOID, DWORD);
        auto createInBand = reinterpret_cast<FnCreateWindowInBand>(
            reinterpret_cast<void*>(GetProcAddress(user32, "CreateWindowInBand")));
        if (createInBand) {
            hwnd_ = createInBand(
                exStyle, kInProcCandClassName, L"", WS_POPUP,
                0, 0, 100, 60,
                ownerRoot, nullptr, g_hInst, this, ownerBand);
        }
    }

    if (!hwnd_) {
        hwnd_ = CreateWindowExW(
            exStyle, kInProcCandClassName, L"", WS_POPUP,
            0, 0, 100, 60,
            ownerRoot, nullptr, g_hInst, this);
    }

    if (hwnd_ && user32 && ownerBand > 1 && queryWindowBand(hwnd_) < ownerBand) {
        using FnSetWindowBand = BOOL(WINAPI*)(HWND, HWND, DWORD);
        auto setBand = reinterpret_cast<FnSetWindowBand>(
            reinterpret_cast<void*>(GetProcAddress(user32, "SetWindowBand")));
        if (setBand) {
            setBand(hwnd_, ownerRoot, ownerBand);
        }
    }

    return hwnd_ != nullptr;
}

bool InProcCandidateWindow::ensureD2D() {
    if (d2dFactory_ && dcTarget_ && dwFactory_ && textFmt_ && smallFmt_ && indexFmt_) {
        return true;
    }

    if (!d2dMod_) d2dMod_ = LoadLibraryW(L"d2d1.dll");
    if (!dwriteMod_) dwriteMod_ = LoadLibraryW(L"dwrite.dll");
    if (!d2dMod_ || !dwriteMod_) return false;

    if (!d2dFactory_) {
        using FnD2D1CreateFactory = HRESULT(WINAPI*)(D2D1_FACTORY_TYPE, REFIID, const D2D1_FACTORY_OPTIONS*, void**);
        auto createD2D = reinterpret_cast<FnD2D1CreateFactory>(
            reinterpret_cast<void*>(GetProcAddress(d2dMod_, "D2D1CreateFactory")));
        if (createD2D) {
            createD2D(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory),
                      nullptr, reinterpret_cast<void**>(&d2dFactory_));
        }
    }
    if (!dwFactory_) {
        using FnDWriteCreateFactory = HRESULT(WINAPI*)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);
        auto createDW = reinterpret_cast<FnDWriteCreateFactory>(
            reinterpret_cast<void*>(GetProcAddress(dwriteMod_, "DWriteCreateFactory")));
        if (createDW) {
            createDW(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                     reinterpret_cast<IUnknown**>(&dwFactory_));
        }
    }
    if (d2dFactory_ && !dcTarget_) {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            0.0f, 0.0f, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
        d2dFactory_->CreateDCRenderTarget(&props, &dcTarget_);
    }
    if (dwFactory_ && !textFmt_) {
        dwFactory_->CreateTextFormat(L"Microsoft YaHei UI", nullptr,
                                     DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                     DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"zh-CN", &textFmt_);
        if (textFmt_) {
            textFmt_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            textFmt_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    if (dwFactory_ && !smallFmt_) {
        dwFactory_->CreateTextFormat(L"Segoe UI", nullptr,
                                     DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                     DWRITE_FONT_STRETCH_NORMAL, 13.5f, L"en-US", &smallFmt_);
        if (smallFmt_) {
            smallFmt_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            smallFmt_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    if (dwFactory_ && !indexFmt_) {
        dwFactory_->CreateTextFormat(L"Segoe UI", nullptr,
                                     DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL,
                                     DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-US", &indexFmt_);
        if (indexFmt_) {
            indexFmt_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            indexFmt_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    return d2dFactory_ && dcTarget_ && dwFactory_ && textFmt_ && smallFmt_ && indexFmt_;
}

void InProcCandidateWindow::releaseD2D() {
    surface_.release();
    if (indexFmt_) { indexFmt_->Release(); indexFmt_ = nullptr; }
    if (smallFmt_) { smallFmt_->Release(); smallFmt_ = nullptr; }
    if (textFmt_) { textFmt_->Release(); textFmt_ = nullptr; }
    if (dcTarget_) { dcTarget_->Release(); dcTarget_ = nullptr; }
    if (dwFactory_) { dwFactory_->Release(); dwFactory_ = nullptr; }
    if (d2dFactory_) { d2dFactory_->Release(); d2dFactory_ = nullptr; }
}

void InProcCandidateWindow::hide() {
    visible_ = false;
    hoverRegion_ = -1;
    hoverTracked_ = false;
    if (hwnd_ && IsWindow(hwnd_)) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void InProcCandidateWindow::destroy() {
    hide();
    releaseD2D();
    if (hwnd_ && IsWindow(hwnd_)) {
        DestroyWindow(hwnd_);
    }
    hwnd_ = nullptr;
    ownerRoot_ = nullptr;
    onSelect_ = {};
    onPage_ = {};
}

void InProcCandidateWindow::update(const ipc::Response& resp,
                                   int caretX, int caretY, int caretHeight,
                                   uint64_t ownerHint,
                                   SelectCb onSelect,
                                   PageCb onPage) {
    if (resp.showCandidates == 0 || resp.candCount == 0) {
        hide();
        return;
    }
    HWND ownerRoot = resolveOwnerRoot(ownerHint);
    if (!ensureWindow(ownerRoot) || !ensureD2D()) {
        hide();
        return;
    }

    if (lastResp_.page != resp.page || lastResp_.headerLen != resp.headerLen ||
        lastResp_.candCount != resp.candCount) {
        hoverRegion_ = -1;
    }
    lastResp_ = resp;
    caretX_ = caretX;
    caretY_ = caretY;
    caretH_ = caretHeight;
    onSelect_ = std::move(onSelect);
    onPage_ = std::move(onPage);
    visible_ = true;

    renderAndShow();
}

void InProcCandidateWindow::moveCaret(int caretX, int caretY, int caretHeight) {
    caretX_ = caretX;
    caretY_ = caretY;
    caretH_ = caretHeight;
    if (isVisible() && lastPxW_ > 0 && lastPxH_ > 0) {
        ScopedPerMonitorDpi dpiGuard;
        positionWindow(lastPxW_, lastPxH_, lastScale_);
    }
}

void InProcCandidateWindow::positionWindow(int pxW, int pxH, float scale) {
    if (!hwnd_) return;
    POINT caretPt{caretX_, caretY_};
    HMONITOR mon = MonitorFromPoint(caretPt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);

    const int padPx = static_cast<int>(std::round(kShadowPad * scale));
    const int clampedCaretH = std::clamp(caretH_, 12, 64);
    int posX = caretX_ - padPx;
    int posY = caretY_ + static_cast<int>(std::round(6.0f * scale)) - padPx;

    if (posX + pxW > mi.rcWork.right) posX = mi.rcWork.right - pxW;
    if (posX < mi.rcWork.left) posX = mi.rcWork.left;
    if (posY + pxH > mi.rcWork.bottom) {
        posY = caretY_ - clampedCaretH - pxH + padPx - static_cast<int>(std::round(6.0f * scale));
    }
    if (posY + pxH > mi.rcWork.bottom) posY = mi.rcWork.bottom - pxH;
    if (posY < mi.rcWork.top) posY = mi.rcWork.top;

    SetWindowPos(hwnd_, HWND_TOPMOST, posX, posY, pxW, pxH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void InProcCandidateWindow::renderAndShow() {
    ScopedPerMonitorDpi dpiGuard;
    if (!hwnd_ || !dcTarget_ || !dwFactory_ || !textFmt_ || !smallFmt_ || !indexFmt_) return;

    UINT dpi = queryWindowDpi(ownerRoot_ ? ownerRoot_ : hwnd_);
    if (dpi == 0) dpi = 96;
    const float scale = static_cast<float>(dpi) / 96.0f;

    const float padX = 14.0f;
    const float headerH = 26.0f;
    const float rowH = 34.0f;
    const float candGap = 6.0f;
    const CandidateLayout layout = (lastResp_.layout == 1) ? CandidateLayout::Vertical : CandidateLayout::Horizontal;
    const bool squareFrame = (lastResp_.squareFrame != 0);
    const SkinTheme skin = static_cast<SkinTheme>(std::clamp<int>(lastResp_.skin, 0, 3));

    const std::string headerUtf8(
        lastResp_.header, std::min<size_t>(lastResp_.headerLen, sizeof(lastResp_.header)));
    const std::wstring headerW = utf8_to_utf16(headerUtf8);
    const D2D1_SIZE_F headerSz = liteime::host::measureDWriteText(dwFactory_, smallFmt_, headerW);

    struct MeasuredCand {
        std::wstring idxStr;
        std::wstring textStr;
        std::wstring pinBadgeStr;
        std::wstring commentStr;
        float wIdx = 0.0f;
        float wTxt = 0.0f;
        float wPinChip = 0.0f;
        float wCom = 0.0f;
        float width = 0.0f;
    };
    const size_t nCands = std::min<size_t>(lastResp_.candCount, ipc::kMaxOverlayCandidates);
    std::vector<MeasuredCand> measured;
    measured.reserve(nCands);

    float candsTotalW = 0.0f;
    float maxVertRowW = 0.0f;
    for (size_t i = 0; i < nCands; ++i) {
        const auto& c = lastResp_.candidates[i];
        MeasuredCand mc;
        mc.idxStr = std::to_wstring(i + 1);
        mc.textStr = utf8_to_utf16(std::string_view(
            c.text, std::min<size_t>(c.textLen, sizeof(c.text))));
        if (c.pinnedSlot >= 0 && c.pinnedSlot < 9) {
            mc.pinBadgeStr = L"\u56fa" + std::to_wstring(c.pinnedSlot + 1);
            mc.wPinChip = liteime::host::measureDWriteText(dwFactory_, indexFmt_, mc.pinBadgeStr).width + 8.0f;
        }
        if (c.commentLen > 0) {
            mc.commentStr = utf8_to_utf16(std::string_view(
                c.comment, std::min<size_t>(c.commentLen, sizeof(c.comment))));
        }

        mc.wIdx = liteime::host::measureDWriteText(dwFactory_, indexFmt_, mc.idxStr).width;
        mc.wTxt = liteime::host::measureDWriteText(dwFactory_, textFmt_, mc.textStr).width;
        mc.wCom = mc.commentStr.empty() ? 0.0f
            : liteime::host::measureDWriteText(dwFactory_, indexFmt_, mc.commentStr).width;

        float innerW = 12.0f + mc.wIdx + 6.0f + mc.wTxt;
        if (mc.wPinChip > 0.0f) innerW += 6.0f + mc.wPinChip;
        if (mc.wCom > 0.0f) innerW += 5.0f + mc.wCom;
        mc.width = innerW + 12.0f;

        candsTotalW += mc.width + (i > 0 ? candGap : 0.0f);
        maxVertRowW = std::max(maxVertRowW, mc.width);
        measured.push_back(std::move(mc));
    }

    const bool hasPager = lastResp_.totalPages > 1;
    const float badgeW = 28.0f;
    const std::wstring pageInfoW = hasPager
        ? (std::to_wstring(lastResp_.page + 1) + L"/" + std::to_wstring(lastResp_.totalPages))
        : std::wstring{};
    const float pageInfoTextW = hasPager ? liteime::host::measureDWriteText(dwFactory_, indexFmt_, pageInfoW).width : 0.0f;
    const float pagerGroupW = hasPager ? (16.0f + pageInfoTextW + 6.0f + 16.0f) : 0.0f;
    const float minHeaderCardW = headerSz.width + badgeW + (hasPager ? pagerGroupW + 8.0f : 0.0f) + padX * 2.0f + 16.0f;

    float cardW = 196.0f;
    float cardH = headerH + 5.0f;
    if (layout == CandidateLayout::Horizontal) {
        cardW = std::max({minHeaderCardW, padX * 2.0f + candsTotalW, 196.0f});
        cardH += rowH + 7.0f;
    } else {
        cardW = std::max({minHeaderCardW, padX * 2.0f + maxVertRowW + 8.0f, 200.0f});
        cardH += static_cast<float>(measured.size()) * rowH + 7.0f;
    }

    const int dipW = static_cast<int>(std::ceil(cardW + kShadowPad * 2.0f));
    const int dipH = static_cast<int>(std::ceil(cardH + kShadowPad * 2.0f));
    const int pxW = static_cast<int>(std::ceil(dipW * scale));
    const int pxH = static_cast<int>(std::ceil(dipH * scale));
    lastPxW_ = pxW;
    lastPxH_ = pxH;
    lastScale_ = scale;

    HDC screenDC = GetDC(nullptr);
    if (!surface_.ensure(screenDC, pxW, pxH)) {
        ReleaseDC(nullptr, screenDC);
        return;
    }

    RECT bindRc{0, 0, pxW, pxH};
    dcTarget_->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
    dcTarget_->BindDC(surface_.dc(), &bindRc);

    const liteime::host::SkinPalette pal = liteime::host::paletteFor(skin);
    hitRegions_.clear();

    dcTarget_->BeginDraw();
    dcTarget_->Clear(D2D1::ColorF(0, 0, 0, 0));

    const float cardR = squareFrame ? 6.0f : 12.0f;
    const float innerR = squareFrame ? 5.0f : 11.0f;
    const float badgeR = squareFrame ? 3.5f : 9.0f;
    const float itemR = squareFrame ? 4.0f : 8.0f;
    const bool isDark = (skin == SkinTheme::ObsidianInk);
    const uint32_t shadowRgb = isDark ? 0x000000 : 0x0F172A;
    const float shadowAlphaBase = isDark ? 0.042f : 0.022f;

    for (int s = 5; s >= 1; --s) {
        const float expand = static_cast<float>(s) * 2.1f;
        const float shadowR = cardR + expand * (squareFrame ? 0.30f : 0.45f);
        D2D1_ROUNDED_RECT sr{
            D2D1::RectF(kShadowPad - expand, kShadowPad - expand + 3.0f,
                        kShadowPad + cardW + expand, kShadowPad + cardH + expand + 3.0f),
            shadowR, shadowR
        };
        ID2D1SolidColorBrush* sBrush = nullptr;
        dcTarget_->CreateSolidColorBrush(D2D1::ColorF(shadowRgb, shadowAlphaBase * (6 - s)), &sBrush);
        if (sBrush) {
            dcTarget_->FillRoundedRectangle(sr, sBrush);
            sBrush->Release();
        }
    }

    ID2D1LinearGradientBrush* cardBgBrush = liteime::host::makeVerticalGradient(
        dcTarget_, kShadowPad, kShadowPad + cardH, pal.bgTop, pal.bgBottom);
    ID2D1SolidColorBrush* borderBrush = nullptr;
    ID2D1SolidColorBrush* innerBrush = nullptr;
    ID2D1SolidColorBrush* divBrush = nullptr;
    ID2D1SolidColorBrush* pyBrush = nullptr;
    ID2D1SolidColorBrush* textBrush = nullptr;
    ID2D1SolidColorBrush* activeTextBrush = nullptr;
    ID2D1SolidColorBrush* pillBorderBrush = nullptr;
    ID2D1SolidColorBrush* accentBrush = nullptr;
    ID2D1SolidColorBrush* idxBrush = nullptr;
    ID2D1SolidColorBrush* activeIdxBrush = nullptr;
    ID2D1SolidColorBrush* comBrush = nullptr;
    ID2D1SolidColorBrush* badgeBgBrush = nullptr;
    ID2D1SolidColorBrush* badgeTxtBrush = nullptr;
    ID2D1SolidColorBrush* pinChipBgBrush = nullptr;
    ID2D1SolidColorBrush* hoverPillBrush = nullptr;

    dcTarget_->CreateSolidColorBrush(pal.border, &borderBrush);
    dcTarget_->CreateSolidColorBrush(pal.innerHighlight, &innerBrush);
    dcTarget_->CreateSolidColorBrush(D2D1::ColorF(pal.divider.r, pal.divider.g, pal.divider.b, pal.divider.a * 0.65f), &divBrush);
    dcTarget_->CreateSolidColorBrush(pal.pinyinText, &pyBrush);
    dcTarget_->CreateSolidColorBrush(pal.candText, &textBrush);
    dcTarget_->CreateSolidColorBrush(pal.activeCandText, &activeTextBrush);
    dcTarget_->CreateSolidColorBrush(pal.activePillBorder, &pillBorderBrush);
    dcTarget_->CreateSolidColorBrush(pal.accentBar, &accentBrush);
    dcTarget_->CreateSolidColorBrush(pal.indexText, &idxBrush);
    dcTarget_->CreateSolidColorBrush(pal.activeIndexText, &activeIdxBrush);
    dcTarget_->CreateSolidColorBrush(pal.commentText, &comBrush);
    dcTarget_->CreateSolidColorBrush(pal.badgeBg, &badgeBgBrush);
    dcTarget_->CreateSolidColorBrush(pal.badgeText, &badgeTxtBrush);
    dcTarget_->CreateSolidColorBrush(D2D1::ColorF(pal.badgeBg.r, pal.badgeBg.g, pal.badgeBg.b, 0.14f), &pinChipBgBrush);
    dcTarget_->CreateSolidColorBrush(
        D2D1::ColorF(pal.accentBar.r, pal.accentBar.g, pal.accentBar.b, isDark ? 0.14f : 0.08f), &hoverPillBrush);

    D2D1_ROUNDED_RECT cardRect{
        D2D1::RectF(kShadowPad, kShadowPad, kShadowPad + cardW, kShadowPad + cardH),
        cardR, cardR
    };
    if (cardBgBrush) dcTarget_->FillRoundedRectangle(cardRect, cardBgBrush);
    if (innerBrush) {
        D2D1_ROUNDED_RECT innerRc{
            D2D1::RectF(kShadowPad + 1.0f, kShadowPad + 1.0f, kShadowPad + cardW - 1.0f, kShadowPad + cardH - 1.0f),
            innerR, innerR
        };
        dcTarget_->DrawRoundedRectangle(innerRc, innerBrush, 1.0f);
    }
    if (borderBrush) dcTarget_->DrawRoundedRectangle(cardRect, borderBrush, 1.0f);

    D2D1_ROUNDED_RECT badgeRect{
        D2D1::RectF(kShadowPad + cardW - padX - badgeW, kShadowPad + 4.0f,
                    kShadowPad + cardW - padX, kShadowPad + headerH - 4.0f),
        badgeR, badgeR
    };
    D2D1_RECT_F pyRect = D2D1::RectF(
        kShadowPad + padX, kShadowPad + 1.0f,
        badgeRect.rect.left - (hasPager ? (pagerGroupW + 10.0f) : 6.0f), kShadowPad + headerH - 1.0f);
    if (pyBrush) {
        dcTarget_->DrawText(headerW.c_str(), static_cast<UINT32>(headerW.size()), smallFmt_, pyRect, pyBrush);
    }

    if (hasPager && idxBrush) {
        const float nextLeft = badgeRect.rect.left - 6.0f - 16.0f;
        const float pageLeft = nextLeft - (pageInfoTextW + 6.0f);
        const float prevLeft = pageLeft - 16.0f;
        D2D1_RECT_F prevRc = D2D1::RectF(prevLeft, kShadowPad + 1.0f, prevLeft + 16.0f, kShadowPad + headerH - 1.0f);
        D2D1_RECT_F pgRc   = D2D1::RectF(pageLeft, kShadowPad + 1.0f, pageLeft + pageInfoTextW + 6.0f, kShadowPad + headerH - 1.0f);
        D2D1_RECT_F nextRc = D2D1::RectF(nextLeft, kShadowPad + 1.0f, nextLeft + 16.0f, kShadowPad + headerH - 1.0f);

        const float arrowR = squareFrame ? 3.0f : 5.0f;
        const bool hoverPrev = (hoverRegion_ == 0);
        const bool hoverNext = (hoverRegion_ == 1);
        if (hoverPrev && hoverPillBrush) {
            D2D1_RECT_F hRc = D2D1::RectF(prevRc.left, prevRc.top + 3.0f, prevRc.right, prevRc.bottom - 3.0f);
            dcTarget_->FillRoundedRectangle(D2D1::RoundedRect(hRc, arrowR, arrowR), hoverPillBrush);
        }
        if (hoverNext && hoverPillBrush) {
            D2D1_RECT_F hRc = D2D1::RectF(nextRc.left, nextRc.top + 3.0f, nextRc.right, nextRc.bottom - 3.0f);
            dcTarget_->FillRoundedRectangle(D2D1::RoundedRect(hRc, arrowR, arrowR), hoverPillBrush);
        }
        indexFmt_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        dcTarget_->DrawText(L"\u2039", 1, indexFmt_, prevRc, (hoverPrev && pyBrush) ? pyBrush : idxBrush);
        dcTarget_->DrawText(pageInfoW.c_str(), static_cast<UINT32>(pageInfoW.size()), indexFmt_, pgRc, idxBrush);
        dcTarget_->DrawText(L"\u203a", 1, indexFmt_, nextRc, (hoverNext && pyBrush) ? pyBrush : idxBrush);
        indexFmt_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        hitRegions_.push_back({prevRc, 1, 0});
        hitRegions_.push_back({nextRc, 2, 0});
    }

    if (badgeBgBrush) dcTarget_->FillRoundedRectangle(badgeRect, badgeBgBrush);
    if (badgeTxtBrush) {
        indexFmt_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        const wchar_t* badge = (lastResp_.isEnglish == 0) ? L"\u4e2d" : L"EN";
        dcTarget_->DrawText(badge, static_cast<UINT32>(wcslen(badge)), indexFmt_, badgeRect.rect, badgeTxtBrush);
        indexFmt_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    if (divBrush) {
        dcTarget_->DrawLine(
            D2D1::Point2F(kShadowPad + padX, kShadowPad + headerH + 0.5f),
            D2D1::Point2F(kShadowPad + cardW - padX, kShadowPad + headerH + 0.5f),
            divBrush, 1.0f);
    }

    float curX = kShadowPad + padX;
    float curY = kShadowPad + headerH + 5.0f;
    for (size_t i = 0; i < measured.size(); ++i) {
        const auto& mc = measured[i];
        const bool isActive = (static_cast<int>(i) == static_cast<int>(lastResp_.selectedIndex));

        D2D1_RECT_F itemRect = (layout == CandidateLayout::Horizontal)
            ? D2D1::RectF(curX, curY, curX + mc.width, curY + rowH - 2.0f)
            : D2D1::RectF(kShadowPad + padX - 4.0f, curY, kShadowPad + cardW - padX + 4.0f, curY + rowH - 2.0f);

        if (isActive) {
            D2D1_ROUNDED_RECT pillRr = D2D1::RoundedRect(itemRect, itemR, itemR);
            ID2D1LinearGradientBrush* pillGrad = liteime::host::makeVerticalGradient(
                dcTarget_, itemRect.top, itemRect.bottom, pal.activePillTop, pal.activePillBottom);
            if (pillGrad) {
                dcTarget_->FillRoundedRectangle(pillRr, pillGrad);
                pillGrad->Release();
            }
            if (pillBorderBrush) dcTarget_->DrawRoundedRectangle(pillRr, pillBorderBrush, 1.0f);
            if (accentBrush) {
                const float barH = 14.0f;
                const float cy = (itemRect.top + itemRect.bottom) * 0.5f;
                const float barR = squareFrame ? 0.5f : 1.25f;
                D2D1_ROUNDED_RECT barRr = D2D1::RoundedRect(
                    D2D1::RectF(itemRect.left + 2.5f, cy - barH * 0.5f, itemRect.left + 5.0f, cy + barH * 0.5f),
                    barR, barR);
                dcTarget_->FillRoundedRectangle(barRr, accentBrush);
            }
        } else if (hoverRegion_ == static_cast<int>(hitRegions_.size()) && hoverPillBrush) {
            dcTarget_->FillRoundedRectangle(D2D1::RoundedRect(itemRect, itemR, itemR), hoverPillBrush);
        }

        float tx = itemRect.left + 12.0f;
        if (idxBrush) {
            D2D1_RECT_F rIdx = D2D1::RectF(tx, itemRect.top + 1.0f, tx + mc.wIdx + 4.0f, itemRect.bottom);
            dcTarget_->DrawText(mc.idxStr.c_str(), static_cast<UINT32>(mc.idxStr.size()), indexFmt_, rIdx,
                                (isActive && activeIdxBrush) ? activeIdxBrush : idxBrush);
        }
        tx += mc.wIdx + 6.0f;

        if (textBrush) {
            D2D1_RECT_F rTxt = D2D1::RectF(tx, itemRect.top, tx + mc.wTxt + 4.0f, itemRect.bottom);
            dcTarget_->DrawText(mc.textStr.c_str(), static_cast<UINT32>(mc.textStr.size()), textFmt_, rTxt,
                                (isActive && activeTextBrush) ? activeTextBrush : textBrush);
        }
        tx += mc.wTxt;

        if (mc.wPinChip > 0.0f) {
            tx += 6.0f;
            const float chipCy = (itemRect.top + itemRect.bottom) * 0.5f;
            const float chipH = 16.0f;
            const float chipR = squareFrame ? 2.5f : 5.0f;
            D2D1_ROUNDED_RECT chipRr{
                D2D1::RectF(tx, chipCy - chipH * 0.5f + 0.5f, tx + mc.wPinChip, chipCy + chipH * 0.5f + 0.5f),
                chipR, chipR
            };
            if (pinChipBgBrush) dcTarget_->FillRoundedRectangle(chipRr, pinChipBgBrush);
            indexFmt_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            if (pyBrush) {
                dcTarget_->DrawText(mc.pinBadgeStr.c_str(), static_cast<UINT32>(mc.pinBadgeStr.size()),
                                    indexFmt_, chipRr.rect, pyBrush);
            }
            indexFmt_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            tx += mc.wPinChip;
        }

        if (!mc.commentStr.empty() && comBrush) {
            tx += 5.0f;
            D2D1_RECT_F rCom = D2D1::RectF(tx, itemRect.top + 1.0f, itemRect.right - 8.0f, itemRect.bottom);
            dcTarget_->DrawText(mc.commentStr.c_str(), static_cast<UINT32>(mc.commentStr.size()), indexFmt_, rCom, comBrush);
        }

        hitRegions_.push_back({itemRect, 0, static_cast<int>(i)});
        if (layout == CandidateLayout::Horizontal) curX += mc.width + candGap;
        else curY += rowH;
    }

    dcTarget_->EndDraw();

    if (pinChipBgBrush) pinChipBgBrush->Release();
    if (hoverPillBrush) hoverPillBrush->Release();
    if (cardBgBrush) cardBgBrush->Release();
    if (borderBrush) borderBrush->Release();
    if (innerBrush) innerBrush->Release();
    if (divBrush) divBrush->Release();
    if (pyBrush) pyBrush->Release();
    if (textBrush) textBrush->Release();
    if (activeTextBrush) activeTextBrush->Release();
    if (pillBorderBrush) pillBorderBrush->Release();
    if (accentBrush) accentBrush->Release();
    if (idxBrush) idxBrush->Release();
    if (activeIdxBrush) activeIdxBrush->Release();
    if (comBrush) comBrush->Release();
    if (badgeBgBrush) badgeBgBrush->Release();
    if (badgeTxtBrush) badgeTxtBrush->Release();

    POINT dstPt{0, 0};
    RECT curRc{};
    if (GetWindowRect(hwnd_, &curRc)) {
        dstPt.x = curRc.left;
        dstPt.y = curRc.top;
    }
    SIZE winSz{pxW, pxH};
    POINT srcPt{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, screenDC, &dstPt, &winSz, surface_.dc(), &srcPt, 0, &blend, ULW_ALPHA);
    positionWindow(pxW, pxH, scale);

    ReleaseDC(nullptr, screenDC);
}

LRESULT CALLBACK InProcCandidateWindow::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    InProcCandidateWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<InProcCandidateWindow*>(cs->lpCreateParams);
        if (self) self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<InProcCandidateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->handleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT InProcCandidateWindow::handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_MOUSEMOVE: {
            if (!hoverTracked_) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
                hoverTracked_ = true;
            }
            const UINT dpi = queryWindowDpi(hwnd);
            const float scale = static_cast<float>(dpi) / 96.0f;
            const float x = static_cast<float>(static_cast<short>(LOWORD(lParam))) / scale;
            const float y = static_cast<float>(static_cast<short>(HIWORD(lParam))) / scale;
            int hit = -1;
            for (size_t i = 0; i < hitRegions_.size(); ++i) {
                const auto& r = hitRegions_[i].rect;
                if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
                    hit = static_cast<int>(i);
                    break;
                }
            }
            if (hit != hoverRegion_) {
                hoverRegion_ = hit;
                if (visible_) renderAndShow();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            hoverTracked_ = false;
            if (hoverRegion_ != -1) {
                hoverRegion_ = -1;
                if (visible_) renderAndShow();
            }
            return 0;
        case WM_LBUTTONUP: {
            const UINT dpi = queryWindowDpi(hwnd);
            const float scale = static_cast<float>(dpi) / 96.0f;
            const float x = static_cast<float>(static_cast<short>(LOWORD(lParam))) / scale;
            const float y = static_cast<float>(static_cast<short>(HIWORD(lParam))) / scale;
            for (const auto& hr : hitRegions_) {
                if (x >= hr.rect.left && x <= hr.rect.right && y >= hr.rect.top && y <= hr.rect.bottom) {
                    if (hr.kind == 0 && onSelect_) onSelect_(hr.index);
                    else if (hr.kind == 1 && onPage_) onPage_(false);
                    else if (hr.kind == 2 && onPage_) onPage_(true);
                    break;
                }
            }
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace liteime::tsf
