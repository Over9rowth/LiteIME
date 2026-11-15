#include "host/candidate_window.hpp"
#include "host/host_messages.hpp"
#include "common/utf.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace liteime::host {

namespace {

constexpr const wchar_t* kCandClassName = L"LiteIME_CandidateWindow";
constexpr const wchar_t* kPinPopupClassName = L"LiteIME_PinMenuPopup";
constexpr float kPinShadowPad = 10.0f;
constexpr float kPinPopupWidth = 168.0f;
constexpr float kPinPadY = 6.0f;
constexpr float kPinRowHeight = 28.0f;

struct SkinPalette {
    D2D1_COLOR_F bgTop;
    D2D1_COLOR_F bgBottom;
    D2D1_COLOR_F border;
    D2D1_COLOR_F innerHighlight;
    D2D1_COLOR_F divider;
    D2D1_COLOR_F pinyinText;
    D2D1_COLOR_F candText;
    D2D1_COLOR_F activeCandText;
    D2D1_COLOR_F activePillTop;
    D2D1_COLOR_F activePillBottom;
    D2D1_COLOR_F activePillBorder;
    D2D1_COLOR_F accentBar;
    D2D1_COLOR_F indexText;
    D2D1_COLOR_F activeIndexText;
    D2D1_COLOR_F commentText;
    D2D1_COLOR_F badgeBg;
    D2D1_COLOR_F badgeText;
};

SkinPalette paletteFor(SkinTheme skin) {
    switch (skin) {
        case SkinTheme::MatchaJade:
            return {
                D2D1::ColorF(0xFCFEFD, 0.98f),
                D2D1::ColorF(0xEFF6F2, 0.98f),
                D2D1::ColorF(0xCEE0D6, 1.0f),
                D2D1::ColorF(0xFFFFFF, 0.85f),
                D2D1::ColorF(0xE1ECE5, 1.0f),
                D2D1::ColorF(0x0F766E, 1.0f),
                D2D1::ColorF(0x1F2933, 1.0f),
                D2D1::ColorF(0x0B5C54, 1.0f),
                D2D1::ColorF(0xE6F4EC, 1.0f),
                D2D1::ColorF(0xD8EDE2, 1.0f),
                D2D1::ColorF(0xB4DBCA, 1.0f),
                D2D1::ColorF(0x0F766E, 1.0f),
                D2D1::ColorF(0x7C9086, 1.0f),
                D2D1::ColorF(0x0F766E, 1.0f),
                D2D1::ColorF(0x047857, 0.90f),
                D2D1::ColorF(0x0F766E, 1.0f),
                D2D1::ColorF(0xFFFFFF, 1.0f),
            };
        case SkinTheme::SakuraCream:
            return {
                D2D1::ColorF(0xFFFCFD, 0.98f),
                D2D1::ColorF(0xFDF1F5, 0.98f),
                D2D1::ColorF(0xEFD5DF, 1.0f),
                D2D1::ColorF(0xFFFFFF, 0.85f),
                D2D1::ColorF(0xF6E4EB, 1.0f),
                D2D1::ColorF(0xBE185D, 1.0f),
                D2D1::ColorF(0x2D1E26, 1.0f),
                D2D1::ColorF(0x9D174D, 1.0f),
                D2D1::ColorF(0xFCE7EF, 1.0f),
                D2D1::ColorF(0xFAD9E6, 1.0f),
                D2D1::ColorF(0xF3BACF, 1.0f),
                D2D1::ColorF(0xDB2777, 1.0f),
                D2D1::ColorF(0x9D7A89, 1.0f),
                D2D1::ColorF(0xBE185D, 1.0f),
                D2D1::ColorF(0x9E5874, 0.92f),
                D2D1::ColorF(0xDB2777, 1.0f),
                D2D1::ColorF(0xFFFFFF, 1.0f),
            };
        case SkinTheme::ObsidianInk:
            return {
                D2D1::ColorF(0x202530, 0.97f),
                D2D1::ColorF(0x161922, 0.97f),
                D2D1::ColorF(0x3E4C63, 1.0f),
                D2D1::ColorF(0xFFFFFF, 0.13f),
                D2D1::ColorF(0x2B3140, 1.0f),
                D2D1::ColorF(0x7DD3FC, 1.0f),
                D2D1::ColorF(0xF1F5F9, 1.0f),
                D2D1::ColorF(0xE0F2FE, 1.0f),
                D2D1::ColorF(0x26354C, 1.0f),
                D2D1::ColorF(0x1E2A3D, 1.0f),
                D2D1::ColorF(0x3B5278, 1.0f),
                D2D1::ColorF(0x38BDF8, 1.0f),
                D2D1::ColorF(0x8E9BB0, 1.0f),
                D2D1::ColorF(0x7DD3FC, 1.0f),
                D2D1::ColorF(0x34D399, 0.92f),
                D2D1::ColorF(0x0284C7, 1.0f),
                D2D1::ColorF(0xF8FAFC, 1.0f),
            };
        case SkinTheme::CloudMist:
        default:
            return {
                D2D1::ColorF(0xFFFFFF, 0.98f),
                D2D1::ColorF(0xF2F6FC, 0.98f),
                D2D1::ColorF(0xD5E0EE, 1.0f),
                D2D1::ColorF(0xFFFFFF, 0.90f),
                D2D1::ColorF(0xE5ECF6, 1.0f),
                D2D1::ColorF(0x2563EB, 1.0f),
                D2D1::ColorF(0x1E293B, 1.0f),
                D2D1::ColorF(0x1D4ED8, 1.0f),
                D2D1::ColorF(0xECF3FF, 1.0f),
                D2D1::ColorF(0xDFECFF, 1.0f),
                D2D1::ColorF(0xBED6FE, 1.0f),
                D2D1::ColorF(0x2563EB, 1.0f),
                D2D1::ColorF(0x8190A5, 1.0f),
                D2D1::ColorF(0x2563EB, 1.0f),
                D2D1::ColorF(0x0D9488, 0.92f),
                D2D1::ColorF(0x2563EB, 1.0f),
                D2D1::ColorF(0xFFFFFF, 1.0f),
            };
    }
}

ID2D1LinearGradientBrush* makeVerticalGradient(
    ID2D1DCRenderTarget* rt, float yTop, float yBottom,
    D2D1_COLOR_F cTop, D2D1_COLOR_F cBottom) {
    D2D1_GRADIENT_STOP stops[2] = {
        {0.0f, cTop},
        {1.0f, cBottom},
    };
    ID2D1GradientStopCollection* col = nullptr;
    if (FAILED(rt->CreateGradientStopCollection(stops, 2, &col)) || !col) {
        return nullptr;
    }
    ID2D1LinearGradientBrush* brush = nullptr;
    rt->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, yTop), D2D1::Point2F(0.0f, yBottom)),
        col,
        &brush);
    col->Release();
    return brush;
}

D2D1_SIZE_F measureText(IDWriteFactory* dw, IDWriteTextFormat* fmt, const std::wstring& s) {
    if (s.empty()) return {0.0f, 20.0f};
    IDWriteTextLayout* layout = nullptr;
    D2D1_SIZE_F sz{0.0f, 20.0f};
    if (SUCCEEDED(dw->CreateTextLayout(s.c_str(), static_cast<UINT32>(s.size()), fmt, 2000.0f, 200.0f, &layout)) && layout) {
        DWRITE_TEXT_METRICS m{};
        if (SUCCEEDED(layout->GetMetrics(&m))) {
            sz.width = m.widthIncludingTrailingWhitespace;
            sz.height = m.height;
        }
        layout->Release();
    }
    return sz;
}

} // namespace

CandidateWindow::~CandidateWindow() {
    releaseD2DResources();
    if (pinPopupHwnd_) {
        DestroyWindow(pinPopupHwnd_);
        pinPopupHwnd_ = nullptr;
    }
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void CandidateWindow::ensureD2DResources() {
    if (!d2dFactory_) {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory_);
    }
    if (!dwFactory_) {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(&dwFactory_));
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
    if (dwFactory_ && !menuFmt_) {
        dwFactory_->CreateTextFormat(L"Microsoft YaHei UI", nullptr,
                                     DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                     DWRITE_FONT_STRETCH_NORMAL, 13.5f, L"zh-CN", &menuFmt_);
        if (menuFmt_) {
            menuFmt_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            menuFmt_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
}

void CandidateWindow::releaseD2DResources() {
    pinSurface_.release();
    cardSurface_.release();
    if (menuFmt_) { menuFmt_->Release(); menuFmt_ = nullptr; }
    if (indexFmt_) { indexFmt_->Release(); indexFmt_ = nullptr; }
    if (smallFmt_) { smallFmt_->Release(); smallFmt_ = nullptr; }
    if (textFmt_) { textFmt_->Release(); textFmt_ = nullptr; }
    if (dcTarget_) { dcTarget_->Release(); dcTarget_ = nullptr; }
    if (dwFactory_) { dwFactory_->Release(); dwFactory_ = nullptr; }
    if (d2dFactory_) { d2dFactory_->Release(); d2dFactory_ = nullptr; }
}

bool CandidateWindow::create(HINSTANCE hInst) {
    hInst_ = hInst;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = CandidateWindow::wndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kCandClassName;
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kCandClassName,
        L"",
        WS_POPUP,
        0, 0, 100, 60,
        nullptr, nullptr, hInst, this);
    return hwnd_ != nullptr;
}

void CandidateWindow::hidePinPopup() {
    if (pinPopupHwnd_) {
        ShowWindow(pinPopupHwnd_, SW_HIDE);
    }
    pinMenuCandIdx_ = -1;
    pinMenuHover_ = -1;
    pinHoverTracked_ = false;
    pinMenuRows_.clear();
}

void CandidateWindow::hide() {
    hidePinPopup();
    hoverRegion_ = -1;
    hoverTracked_ = false;
    state_.visible = false;
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

void CandidateWindow::update(const CandidateViewState& state) {
    const bool layoutChanged = (state_.page != state.page ||
                                state_.headerBuffer != state.headerBuffer ||
                                state_.items.size() != state.items.size());
    state_ = state;
    if (layoutChanged) {
        hoverRegion_ = -1;
    }
    if (!state_.visible || state_.items.empty()) {
        hide();
        return;
    }
    renderAndShow();
}

void CandidateWindow::renderAndShow() {
    ensureD2DResources();
    if (!hwnd_ || !dcTarget_ || !dwFactory_ || !textFmt_ || !smallFmt_ || !indexFmt_) return;

    UINT dpi = queryDpiForWindow(hwnd_);
    if (dpi == 0) dpi = 96;
    const float scale = static_cast<float>(dpi) / 96.0f;

    const float shadowPad = 14.0f;
    const float padX = 14.0f;
    const float headerH = 26.0f;
    const float rowH = 34.0f;
    const float candGap = 6.0f;

    std::wstring headerW = utf8_to_utf16(state_.headerBuffer);
    D2D1_SIZE_F headerSz = measureText(dwFactory_, smallFmt_, headerW);

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
    std::vector<MeasuredCand> measured;
    measured.reserve(state_.items.size());

    float candsTotalW = 0.0f;
    float maxVertRowW = 0.0f;

    for (size_t i = 0; i < state_.items.size(); ++i) {
        MeasuredCand mc;
        mc.idxStr = std::to_wstring(i + 1);
        mc.textStr = utf8_to_utf16(state_.items[i].text);
        if (state_.items[i].pinnedSlot >= 0 && state_.items[i].pinnedSlot < 9) {
            mc.pinBadgeStr = L"\u56fa" + std::to_wstring(state_.items[i].pinnedSlot + 1);
            mc.wPinChip = measureText(dwFactory_, indexFmt_, mc.pinBadgeStr).width + 8.0f;
        }
        mc.commentStr = utf8_to_utf16(state_.items[i].comment);

        mc.wIdx = measureText(dwFactory_, indexFmt_, mc.idxStr).width;
        mc.wTxt = measureText(dwFactory_, textFmt_, mc.textStr).width;
        mc.wCom = mc.commentStr.empty() ? 0.0f : measureText(dwFactory_, indexFmt_, mc.commentStr).width;

        float innerW = 12.0f + mc.wIdx + 6.0f + mc.wTxt;
        if (mc.wPinChip > 0.0f) innerW += 6.0f + mc.wPinChip;
        if (mc.wCom > 0.0f) innerW += 5.0f + mc.wCom;
        mc.width = innerW + 12.0f;

        candsTotalW += mc.width + (i > 0 ? candGap : 0.0f);
        maxVertRowW = std::max(maxVertRowW, mc.width);
        measured.push_back(std::move(mc));
    }

    const bool hasPager = state_.totalPages > 1;
    const float badgeW = 28.0f;
    std::wstring pageInfoW = hasPager
        ? (std::to_wstring(state_.page + 1) + L"/" + std::to_wstring(state_.totalPages))
        : std::wstring{};
    float pageInfoTextW = hasPager ? measureText(dwFactory_, indexFmt_, pageInfoW).width : 0.0f;
    float pagerGroupW = hasPager ? (16.0f + pageInfoTextW + 6.0f + 16.0f) : 0.0f;

    float minHeaderCardW = headerSz.width + badgeW + (hasPager ? pagerGroupW + 8.0f : 0.0f) + padX * 2.0f + 16.0f;
    float cardW = 196.0f;
    float cardH = headerH + 5.0f;

    if (state_.layout == CandidateLayout::Horizontal) {
        cardW = std::max({minHeaderCardW, padX * 2.0f + candsTotalW, 196.0f});
        cardH += rowH + 7.0f;
    } else {
        cardW = std::max({minHeaderCardW, padX * 2.0f + maxVertRowW + 8.0f, 200.0f});
        cardH += static_cast<float>(measured.size()) * rowH + 7.0f;
    }

    const int dipW = static_cast<int>(std::ceil(cardW + shadowPad * 2.0f));
    const int dipH = static_cast<int>(std::ceil(cardH + shadowPad * 2.0f));
    const int pxW = static_cast<int>(std::ceil(dipW * scale));
    const int pxH = static_cast<int>(std::ceil(dipH * scale));

    HDC screenDC = GetDC(nullptr);
    if (!cardSurface_.ensure(screenDC, pxW, pxH)) {
        ReleaseDC(nullptr, screenDC);
        return;
    }

    RECT bindRc{0, 0, pxW, pxH};
    dcTarget_->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
    dcTarget_->BindDC(cardSurface_.dc(), &bindRc);

    const SkinPalette pal = paletteFor(state_.skin);
    hitRegions_.clear();

    dcTarget_->BeginDraw();
    dcTarget_->Clear(D2D1::ColorF(0, 0, 0, 0));

    const float cardR = state_.squareFrame ? 6.0f : 12.0f;
    const float innerR = state_.squareFrame ? 5.0f : 11.0f;
    const float badgeR = state_.squareFrame ? 3.5f : 9.0f;
    const float itemR = state_.squareFrame ? 4.0f : 8.0f;

    // Soft 5-layer floating shadow (darker ambient drop shadow in ObsidianInk)
    const bool isDark = (state_.skin == SkinTheme::ObsidianInk);
    const uint32_t shadowRgb = isDark ? 0x000000 : 0x0F172A;
    const float shadowAlphaBase = isDark ? 0.042f : 0.022f;
    for (int s = 5; s >= 1; --s) {
        float expand = static_cast<float>(s) * 2.1f;
        float shadowR = cardR + expand * (state_.squareFrame ? 0.30f : 0.45f);
        D2D1_ROUNDED_RECT sr{
            D2D1::RectF(shadowPad - expand, shadowPad - expand + 3.0f,
                        shadowPad + cardW + expand, shadowPad + cardH + expand + 3.0f),
            shadowR, shadowR
        };
        ID2D1SolidColorBrush* sBrush = nullptr;
        dcTarget_->CreateSolidColorBrush(D2D1::ColorF(shadowRgb, shadowAlphaBase * (6 - s)), &sBrush);
        if (sBrush) {
            dcTarget_->FillRoundedRectangle(sr, sBrush);
            sBrush->Release();
        }
    }

    ID2D1LinearGradientBrush* cardBgBrush = makeVerticalGradient(
        dcTarget_, shadowPad, shadowPad + cardH, pal.bgTop, pal.bgBottom);
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
        D2D1::RectF(shadowPad, shadowPad, shadowPad + cardW, shadowPad + cardH),
        cardR, cardR
    };
    if (cardBgBrush) dcTarget_->FillRoundedRectangle(cardRect, cardBgBrush);
    if (innerBrush) {
        D2D1_ROUNDED_RECT innerRc{
            D2D1::RectF(shadowPad + 1.0f, shadowPad + 1.0f, shadowPad + cardW - 1.0f, shadowPad + cardH - 1.0f),
            innerR, innerR
        };
        dcTarget_->DrawRoundedRectangle(innerRc, innerBrush, 1.0f);
    }
    if (borderBrush) dcTarget_->DrawRoundedRectangle(cardRect, borderBrush, 1.0f);

    // Header row: Pinyin + unified < 1/N > pager + Mode badge
    D2D1_ROUNDED_RECT badgeRect{
        D2D1::RectF(shadowPad + cardW - padX - badgeW, shadowPad + 4.0f,
                    shadowPad + cardW - padX, shadowPad + headerH - 4.0f),
        badgeR, badgeR
    };
    D2D1_RECT_F pyRect = D2D1::RectF(
        shadowPad + padX, shadowPad + 1.0f,
        badgeRect.rect.left - (hasPager ? (pagerGroupW + 10.0f) : 6.0f), shadowPad + headerH - 1.0f);
    if (pyBrush) {
        dcTarget_->DrawText(headerW.c_str(), static_cast<UINT32>(headerW.size()), smallFmt_, pyRect, pyBrush);
    }

    if (hasPager && idxBrush) {
        float nextLeft = badgeRect.rect.left - 6.0f - 16.0f;
        float pageLeft = nextLeft - (pageInfoTextW + 6.0f);
        float prevLeft = pageLeft - 16.0f;

        D2D1_RECT_F prevRc = D2D1::RectF(prevLeft, shadowPad + 1.0f, prevLeft + 16.0f, shadowPad + headerH - 1.0f);
        D2D1_RECT_F pgRc   = D2D1::RectF(pageLeft, shadowPad + 1.0f, pageLeft + pageInfoTextW + 6.0f, shadowPad + headerH - 1.0f);
        D2D1_RECT_F nextRc = D2D1::RectF(nextLeft, shadowPad + 1.0f, nextLeft + 16.0f, shadowPad + headerH - 1.0f);

        const float arrowR = state_.squareFrame ? 3.0f : 5.0f;
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
        const wchar_t* badge = state_.chineseMode ? L"\u4e2d" : L"EN";
        dcTarget_->DrawText(badge, static_cast<UINT32>(wcslen(badge)), indexFmt_, badgeRect.rect, badgeTxtBrush);
        indexFmt_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    // Subtle divider line
    if (divBrush) {
        dcTarget_->DrawLine(
            D2D1::Point2F(shadowPad + padX, shadowPad + headerH + 0.5f),
            D2D1::Point2F(shadowPad + cardW - padX, shadowPad + headerH + 0.5f),
            divBrush, 1.0f);
    }

    // Candidates
    float curX = shadowPad + padX;
    float curY = shadowPad + headerH + 5.0f;

    for (size_t i = 0; i < measured.size(); ++i) {
        const auto& mc = measured[i];
        const bool isActive = (static_cast<int>(i) == state_.selectedIndex);

        D2D1_RECT_F itemRect = (state_.layout == CandidateLayout::Horizontal)
            ? D2D1::RectF(curX, curY, curX + mc.width, curY + rowH - 2.0f)
            : D2D1::RectF(shadowPad + padX - 4.0f, curY, shadowPad + cardW - padX + 4.0f, curY + rowH - 2.0f);

        if (isActive) {
            D2D1_ROUNDED_RECT pillRr = D2D1::RoundedRect(itemRect, itemR, itemR);
            ID2D1LinearGradientBrush* pillGrad = makeVerticalGradient(
                dcTarget_, itemRect.top, itemRect.bottom, pal.activePillTop, pal.activePillBottom);
            if (pillGrad) {
                dcTarget_->FillRoundedRectangle(pillRr, pillGrad);
                pillGrad->Release();
            }
            if (pillBorderBrush) {
                dcTarget_->DrawRoundedRectangle(pillRr, pillBorderBrush, 1.0f);
            }
            if (accentBrush) {
                float barH = 14.0f;
                float cy = (itemRect.top + itemRect.bottom) * 0.5f;
                float barR = state_.squareFrame ? 0.5f : 1.25f;
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
            float chipCy = (itemRect.top + itemRect.bottom) * 0.5f;
            float chipH = 16.0f;
            float chipR = state_.squareFrame ? 2.5f : 5.0f;
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

        if (state_.layout == CandidateLayout::Horizontal) {
            curX += mc.width + candGap;
        } else {
            curY += rowH;
        }
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

    // Position near caret, clamped to the current monitor work area
    POINT caretPt{state_.caretX, state_.caretY};
    HMONITOR mon = MonitorFromPoint(caretPt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);

    int padPx = static_cast<int>(std::round(shadowPad * scale));
    const int clampedCaretH = std::clamp(state_.caretHeight, 12, 64);
    int posX = state_.caretX - padPx;
    int posY = state_.caretY + static_cast<int>(std::round(6.0f * scale)) - padPx;

    if (posX + pxW > mi.rcWork.right) posX = mi.rcWork.right - pxW;
    if (posX < mi.rcWork.left) posX = mi.rcWork.left;
    if (posY + pxH > mi.rcWork.bottom) {
        posY = state_.caretY - clampedCaretH - pxH + padPx - static_cast<int>(std::round(6.0f * scale));
    }
    if (posY + pxH > mi.rcWork.bottom) posY = mi.rcWork.bottom - pxH;
    if (posY < mi.rcWork.top) posY = mi.rcWork.top;

    POINT dstPt{posX, posY};
    SIZE winSz{pxW, pxH};
    POINT srcPt{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

    UpdateLayeredWindow(hwnd_, screenDC, &dstPt, &winSz, cardSurface_.dc(), &srcPt, 0, &blend, ULW_ALPHA);
    SetWindowPos(hwnd_, HWND_TOPMOST, posX, posY, pxW, pxH, SWP_NOACTIVATE | SWP_SHOWWINDOW);

    ReleaseDC(nullptr, screenDC);
}

LRESULT CALLBACK CandidateWindow::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    CandidateWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<CandidateWindow*>(cs->lpCreateParams);
        if (self) self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->handleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CandidateWindow::handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_MOUSEMOVE: {
            if (!hoverTracked_) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
                hoverTracked_ = true;
            }
            UINT dpi = queryDpiForWindow(hwnd);
            if (dpi == 0) dpi = 96;
            float scale = static_cast<float>(dpi) / 96.0f;
            float x = static_cast<float>(static_cast<short>(LOWORD(lParam))) / scale;
            float y = static_cast<float>(static_cast<short>(HIWORD(lParam))) / scale;
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
                if (state_.visible && !state_.items.empty()) renderAndShow();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            hoverTracked_ = false;
            if (hoverRegion_ != -1) {
                hoverRegion_ = -1;
                if (state_.visible && !state_.items.empty()) renderAndShow();
            }
            return 0;
        case WM_MOUSEWHEEL: {
            hidePinPopup();
            const short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (hostHwnd_) {
                if (delta > 0) {
                    PostMessageW(hostHwnd_, WM_APP_CANDIDATE_PAGE, 0, 0);
                } else if (delta < 0) {
                    PostMessageW(hostHwnd_, WM_APP_CANDIDATE_PAGE, 1, 0);
                }
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            hidePinPopup();
            UINT dpi = queryDpiForWindow(hwnd);
            if (dpi == 0) dpi = 96;
            float scale = static_cast<float>(dpi) / 96.0f;
            float x = static_cast<float>(static_cast<short>(LOWORD(lParam))) / scale;
            float y = static_cast<float>(static_cast<short>(HIWORD(lParam))) / scale;
            for (const auto& hr : hitRegions_) {
                if (x >= hr.rect.left && x <= hr.rect.right && y >= hr.rect.top && y <= hr.rect.bottom) {
                    if (!hostHwnd_) break;
                    if (hr.actionType == 0 && hr.index >= 0 && hr.index < 9) {
                        PostMessageW(hostHwnd_, WM_APP_CANDIDATE_CLICK, static_cast<WPARAM>(hr.index), 0);
                    } else if (hr.actionType == 1) {
                        PostMessageW(hostHwnd_, WM_APP_CANDIDATE_PAGE, 0, 0);
                    } else if (hr.actionType == 2) {
                        PostMessageW(hostHwnd_, WM_APP_CANDIDATE_PAGE, 1, 0);
                    }
                    break;
                }
            }
            return 0;
        }
        case WM_RBUTTONDOWN:
            return 0;
        case WM_RBUTTONUP: {
            UINT dpi = queryDpiForWindow(hwnd);
            if (dpi == 0) dpi = 96;
            float scale = static_cast<float>(dpi) / 96.0f;
            float x = static_cast<float>(static_cast<short>(LOWORD(lParam))) / scale;
            float y = static_cast<float>(static_cast<short>(HIWORD(lParam))) / scale;
            for (const auto& hr : hitRegions_) {
                if (hr.actionType == 0 && hr.index >= 0 && hr.index < static_cast<int>(state_.items.size()) &&
                    x >= hr.rect.left && x <= hr.rect.right && y >= hr.rect.top && y <= hr.rect.bottom) {
                    const auto& item = state_.items[hr.index];
                    if (item.kind != engine::CandidateKind::QuickInput &&
                        item.kind != engine::CandidateKind::QuickInputSetup &&
                        item.kind != engine::CandidateKind::DynamicShortcut &&
                        item.kind != engine::CandidateKind::RawInput) {
                        POINT pt{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
                        ClientToScreen(hwnd, &pt);
                        showPinPopup(hr.index, pt);
                    }
                    break;
                }
            }
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void CandidateWindow::showPinPopup(int candIndex, POINT screenPt) {
    if (!hInst_ || candIndex < 0 || candIndex >= static_cast<int>(state_.items.size())) return;

    pinMenuCandIdx_ = candIndex;
    pinMenuHover_ = -1;
    pinHoverTracked_ = false;
    pinMenuRows_.clear();

    const auto& item = state_.items[candIndex];
    for (int s = 0; s < 5; ++s) {
        std::wstring lbl = L"\u56fa\u5b9a\u4e3a\u7b2c " + std::to_wstring(s + 1) + L" \u4f4d";
        if (item.pinnedSlot == s) {
            lbl += L" (\u5f53\u524d)";
        }
        pinMenuRows_.push_back(PinMenuRow{std::move(lbl), static_cast<WORD>(s)});
    }
    if (item.pinnedSlot >= 0) {
        pinMenuRows_.push_back(PinMenuRow{L"\u53d6\u6d88\u56fa\u5b9a", 0xFFFF});
    }
    pinMenuRows_.push_back(PinMenuRow{L"\u4ece\u7528\u6237\u8bcd\u5e93\u79fb\u9664", 0xFFFE});

    if (!pinPopupHwnd_) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = CandidateWindow::pinPopupWndProc;
        wc.hInstance = hInst_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kPinPopupClassName;
        RegisterClassExW(&wc);

        pinPopupHwnd_ = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            kPinPopupClassName,
            L"",
            WS_POPUP,
            screenPt.x, screenPt.y, 180, 180,
            nullptr, nullptr, hInst_, this);
        if (!pinPopupHwnd_) return;
    } else {
        // Place on target monitor before querying per-monitor DPI
        SetWindowPos(pinPopupHwnd_, HWND_TOPMOST, screenPt.x, screenPt.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    }

    UINT dpi = queryDpiForWindow(pinPopupHwnd_);
    if (dpi == 0) dpi = 96;
    const float scale = static_cast<float>(dpi) / 96.0f;

    const float popDipW = kPinPopupWidth;
    const float popDipH = kPinPadY * 2.0f + static_cast<float>(pinMenuRows_.size()) * kPinRowHeight;
    const int cardPxW = static_cast<int>(std::ceil(popDipW * scale));
    const int cardPxH = static_cast<int>(std::ceil(popDipH * scale));
    const int padPx = static_cast<int>(std::round(kPinShadowPad * scale));

    HMONITOR mon = MonitorFromPoint(screenPt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);

    int x = screenPt.x;
    int y = screenPt.y;
    if (x + cardPxW > mi.rcWork.right) x = mi.rcWork.right - cardPxW;
    if (y + cardPxH > mi.rcWork.bottom) y = mi.rcWork.bottom - cardPxH;
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top) y = mi.rcWork.top;

    pinPopupOrigin_ = {x - padPx, y - padPx};
    renderPinPopup();
}

void CandidateWindow::renderPinPopup() {
    ensureD2DResources();
    if (!pinPopupHwnd_ || pinMenuRows_.empty() || !dcTarget_ || !menuFmt_) return;

    UINT dpi = queryDpiForWindow(pinPopupHwnd_);
    if (dpi == 0) dpi = 96;
    const float scale = static_cast<float>(dpi) / 96.0f;

    const float cardW = kPinPopupWidth;
    const float cardH = kPinPadY * 2.0f + static_cast<float>(pinMenuRows_.size()) * kPinRowHeight;
    const float dipW = cardW + kPinShadowPad * 2.0f;
    const float dipH = cardH + kPinShadowPad * 2.0f;
    const int pxW = static_cast<int>(std::ceil(dipW * scale));
    const int pxH = static_cast<int>(std::ceil(dipH * scale));

    HDC screenDC = GetDC(nullptr);
    if (!pinSurface_.ensure(screenDC, pxW, pxH)) {
        ReleaseDC(nullptr, screenDC);
        return;
    }

    RECT bindRc{0, 0, pxW, pxH};
    dcTarget_->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
    dcTarget_->BindDC(pinSurface_.dc(), &bindRc);

    const SkinPalette pal = paletteFor(state_.skin);
    const float cardR = state_.squareFrame ? 6.0f : 10.0f;
    const float innerR = state_.squareFrame ? 5.0f : 9.0f;
    const float itemR = state_.squareFrame ? 4.0f : 7.0f;

    dcTarget_->BeginDraw();
    dcTarget_->Clear(D2D1::ColorF(0, 0, 0, 0));

    const bool isDark = (state_.skin == SkinTheme::ObsidianInk);
    const uint32_t shadowRgb = isDark ? 0x000000 : 0x0F172A;
    const float shadowAlphaBase = isDark ? 0.042f : 0.022f;
    for (int s = 4; s >= 1; --s) {
        float ex = static_cast<float>(s) * 1.8f;
        float sr = cardR + ex * (state_.squareFrame ? 0.30f : 0.45f);
        D2D1_ROUNDED_RECT shadowRect{
            D2D1::RectF(kPinShadowPad - ex, kPinShadowPad - ex + 2.0f,
                        kPinShadowPad + cardW + ex, kPinShadowPad + cardH + ex + 2.0f),
            sr, sr
        };
        ID2D1SolidColorBrush* sb = nullptr;
        dcTarget_->CreateSolidColorBrush(D2D1::ColorF(shadowRgb, shadowAlphaBase * (5 - s)), &sb);
        if (sb) {
            dcTarget_->FillRoundedRectangle(shadowRect, sb);
            sb->Release();
        }
    }

    ID2D1LinearGradientBrush* bgGrad = makeVerticalGradient(
        dcTarget_, kPinShadowPad, kPinShadowPad + cardH, pal.bgTop, pal.bgBottom);
    ID2D1SolidColorBrush* borderBrush = nullptr;
    ID2D1SolidColorBrush* innerBrush = nullptr;
    ID2D1SolidColorBrush* hoverBrush = nullptr;
    ID2D1SolidColorBrush* hoverBorderBrush = nullptr;
    ID2D1SolidColorBrush* txtBrush = nullptr;
    ID2D1SolidColorBrush* activeTxtBrush = nullptr;

    dcTarget_->CreateSolidColorBrush(pal.border, &borderBrush);
    dcTarget_->CreateSolidColorBrush(pal.innerHighlight, &innerBrush);
    dcTarget_->CreateSolidColorBrush(pal.activePillTop, &hoverBrush);
    dcTarget_->CreateSolidColorBrush(pal.activePillBorder, &hoverBorderBrush);
    dcTarget_->CreateSolidColorBrush(pal.candText, &txtBrush);
    dcTarget_->CreateSolidColorBrush(pal.activeCandText, &activeTxtBrush);

    D2D1_ROUNDED_RECT cardRect{
        D2D1::RectF(kPinShadowPad, kPinShadowPad, kPinShadowPad + cardW, kPinShadowPad + cardH),
        cardR, cardR
    };
    if (bgGrad) dcTarget_->FillRoundedRectangle(cardRect, bgGrad);
    if (innerBrush) {
        D2D1_ROUNDED_RECT innerRc{
            D2D1::RectF(kPinShadowPad + 1.0f, kPinShadowPad + 1.0f,
                        kPinShadowPad + cardW - 1.0f, kPinShadowPad + cardH - 1.0f),
            innerR, innerR
        };
        dcTarget_->DrawRoundedRectangle(innerRc, innerBrush, 1.0f);
    }
    if (borderBrush) dcTarget_->DrawRoundedRectangle(cardRect, borderBrush, 1.0f);

    for (size_t i = 0; i < pinMenuRows_.size(); ++i) {
        float rowTop = kPinShadowPad + kPinPadY + static_cast<float>(i) * kPinRowHeight;
        D2D1_RECT_F rowRc = D2D1::RectF(
            kPinShadowPad + 5.0f, rowTop + 1.0f,
            kPinShadowPad + cardW - 5.0f, rowTop + kPinRowHeight - 1.0f);
        const bool hovered = (static_cast<int>(i) == pinMenuHover_);
        if (hovered) {
            D2D1_ROUNDED_RECT pillRr = D2D1::RoundedRect(rowRc, itemR, itemR);
            if (hoverBrush) dcTarget_->FillRoundedRectangle(pillRr, hoverBrush);
            if (hoverBorderBrush) dcTarget_->DrawRoundedRectangle(pillRr, hoverBorderBrush, 1.0f);
        }
        D2D1_RECT_F txtRc = D2D1::RectF(rowRc.left + 10.0f, rowRc.top, rowRc.right - 8.0f, rowRc.bottom);
        ID2D1SolidColorBrush* rowTxtBrush = (hovered && activeTxtBrush) ? activeTxtBrush : txtBrush;
        if (rowTxtBrush) {
            dcTarget_->DrawText(pinMenuRows_[i].label.c_str(), static_cast<UINT32>(pinMenuRows_[i].label.size()),
                                menuFmt_, txtRc, rowTxtBrush);
        }
    }

    dcTarget_->EndDraw();

    if (bgGrad) bgGrad->Release();
    if (borderBrush) borderBrush->Release();
    if (innerBrush) innerBrush->Release();
    if (hoverBrush) hoverBrush->Release();
    if (hoverBorderBrush) hoverBorderBrush->Release();
    if (txtBrush) txtBrush->Release();
    if (activeTxtBrush) activeTxtBrush->Release();

    SIZE winSz{pxW, pxH};
    POINT srcPt{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(pinPopupHwnd_, screenDC, &pinPopupOrigin_, &winSz, pinSurface_.dc(), &srcPt, 0, &blend, ULW_ALPHA);
    SetWindowPos(pinPopupHwnd_, HWND_TOPMOST, pinPopupOrigin_.x, pinPopupOrigin_.y, pxW, pxH,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

    ReleaseDC(nullptr, screenDC);
}

LRESULT CALLBACK CandidateWindow::pinPopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    CandidateWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<CandidateWindow*>(cs->lpCreateParams);
        if (self) self->pinPopupHwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->handlePinPopupMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CandidateWindow::handlePinPopupMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_MOUSEMOVE: {
            if (!pinHoverTracked_) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
                pinHoverTracked_ = true;
            }
            UINT dpi = queryDpiForWindow(hwnd);
            if (dpi == 0) dpi = 96;
            const float scale = static_cast<float>(dpi) / 96.0f;
            const float lx = static_cast<float>(static_cast<short>(LOWORD(lParam))) / scale;
            const float ly = static_cast<float>(static_cast<short>(HIWORD(lParam))) / scale;
            int row = -1;
            if (lx >= kPinShadowPad && lx <= kPinShadowPad + kPinPopupWidth) {
                int candidate = static_cast<int>(std::floor((ly - (kPinShadowPad + kPinPadY)) / kPinRowHeight));
                if (candidate >= 0 && candidate < static_cast<int>(pinMenuRows_.size())) {
                    row = candidate;
                }
            }
            if (row != pinMenuHover_) {
                pinMenuHover_ = row;
                renderPinPopup();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            pinHoverTracked_ = false;
            if (pinMenuHover_ != -1) {
                pinMenuHover_ = -1;
                if (!pinMenuRows_.empty()) renderPinPopup();
            }
            return 0;
        case WM_LBUTTONDOWN: {
            UINT dpi = queryDpiForWindow(hwnd);
            if (dpi == 0) dpi = 96;
            const float scale = static_cast<float>(dpi) / 96.0f;
            const float lx = static_cast<float>(static_cast<short>(LOWORD(lParam))) / scale;
            const float ly = static_cast<float>(static_cast<short>(HIWORD(lParam))) / scale;
            if (lx >= kPinShadowPad && lx <= kPinShadowPad + kPinPopupWidth && pinMenuCandIdx_ >= 0 && hostHwnd_) {
                int row = static_cast<int>(std::floor((ly - (kPinShadowPad + kPinPadY)) / kPinRowHeight));
                if (row >= 0 && row < static_cast<int>(pinMenuRows_.size())) {
                    const WORD actionVal = pinMenuRows_[row].actionVal;
                    if (actionVal == 0xFFFE) {
                        PostMessageW(hostHwnd_, WM_APP_REMOVE_USER_WORD, static_cast<WPARAM>(pinMenuCandIdx_), 0);
                    } else {
                        PostMessageW(hostHwnd_, WM_APP_PIN_CANDIDATE,
                                     MAKEWPARAM(static_cast<WORD>(pinMenuCandIdx_), actionVal), 0);
                    }
                }
            }
            hidePinPopup();
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace liteime::host
