#include "host/status_window.hpp"
#include "host/host_messages.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace liteime::host {

namespace {
constexpr const wchar_t* kStatusClassName = L"LiteIME_StatusPill";
constexpr float kPillW = 140.0f;
constexpr float kPillH = 32.0f;
constexpr float kPad = 10.0f;
} // namespace

StatusWindow::~StatusWindow() {
    releaseResources();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void StatusWindow::ensureResources() {
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
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        d2dFactory_->CreateDCRenderTarget(&props, &dcTarget_);
    }
    if (dwFactory_ && !textFmt_) {
        dwFactory_->CreateTextFormat(L"Microsoft YaHei UI", nullptr,
                                     DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                     DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"zh-CN", &textFmt_);
        if (textFmt_) {
            textFmt_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            textFmt_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
}

void StatusWindow::releaseResources() {
    surface_.release();
    if (textFmt_) { textFmt_->Release(); textFmt_ = nullptr; }
    if (dcTarget_) { dcTarget_->Release(); dcTarget_ = nullptr; }
    if (dwFactory_) { dwFactory_->Release(); dwFactory_ = nullptr; }
    if (d2dFactory_) { d2dFactory_->Release(); d2dFactory_ = nullptr; }
}

bool StatusWindow::create(HINSTANCE hInst, HWND ownerHostHwnd) {
    hostHwnd_ = ownerHostHwnd;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = StatusWindow::wndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    wc.lpszClassName = kStatusClassName;
    RegisterClassExW(&wc);

    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int initX = wa.right - 220;
    int initY = wa.bottom - 64;

    hwnd_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kStatusClassName,
        L"",
        WS_POPUP,
        initX, initY, 160, 44,
        nullptr, nullptr, hInst, this);
    return hwnd_ != nullptr;
}

void StatusWindow::setVisible(bool visible) {
    visible_ = visible;
    if (!visible_) {
        hoverZone_ = -1;
        hoverTracked_ = false;
    }
    if (!hwnd_) return;
    ShowWindow(hwnd_, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
}

void StatusWindow::update(const EngineConfig& cfg) {
    cfg_ = cfg;
    visible_ = cfg.showStatusBar;
    if (!visible_) {
        hoverZone_ = -1;
        hoverTracked_ = false;
        if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
        return;
    }
    render();
}

void StatusWindow::render() {
    ensureResources();
    if (!hwnd_ || !dcTarget_ || !textFmt_) return;

    UINT dpi = queryDpiForWindow(hwnd_);
    if (dpi == 0) dpi = 96;
    const float scale = static_cast<float>(dpi) / 96.0f;
    const int pxW = static_cast<int>(std::ceil((kPillW + kPad * 2.0f) * scale));
    const int pxH = static_cast<int>(std::ceil((kPillH + kPad * 2.0f) * scale));

    HDC screenDC = GetDC(nullptr);
    if (!surface_.ensure(screenDC, pxW, pxH)) {
        ReleaseDC(nullptr, screenDC);
        return;
    }

    RECT bindRc{0, 0, pxW, pxH};
    dcTarget_->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
    dcTarget_->BindDC(surface_.dc(), &bindRc);

    D2D1_COLOR_F bgTop, bgBot, borderCol, innerCol, accentCol, textCol, divCol;
    switch (cfg_.skin) {
        case SkinTheme::MatchaJade:
            bgTop = D2D1::ColorF(0xFCFEFD, 0.96f);
            bgBot = D2D1::ColorF(0xEEF6F1, 0.96f);
            borderCol = D2D1::ColorF(0xCBE0D4, 1.0f);
            innerCol = D2D1::ColorF(0xFFFFFF, 0.85f);
            accentCol = D2D1::ColorF(0x0F766E, 1.0f);
            textCol = D2D1::ColorF(0x334155, 1.0f);
            divCol = D2D1::ColorF(0xDCE8E1, 1.0f);
            break;
        case SkinTheme::SakuraCream:
            bgTop = D2D1::ColorF(0xFFFCFD, 0.96f);
            bgBot = D2D1::ColorF(0xFDF0F5, 0.96f);
            borderCol = D2D1::ColorF(0xEED4DF, 1.0f);
            innerCol = D2D1::ColorF(0xFFFFFF, 0.85f);
            accentCol = D2D1::ColorF(0xDB2777, 1.0f);
            textCol = D2D1::ColorF(0x472E3B, 1.0f);
            divCol = D2D1::ColorF(0xF3DFEA, 1.0f);
            break;
        case SkinTheme::ObsidianInk:
            bgTop = D2D1::ColorF(0x222836, 0.95f);
            bgBot = D2D1::ColorF(0x161A23, 0.95f);
            borderCol = D2D1::ColorF(0x3E4C63, 1.0f);
            innerCol = D2D1::ColorF(0xFFFFFF, 0.13f);
            accentCol = D2D1::ColorF(0x38BDF8, 1.0f);
            textCol = D2D1::ColorF(0xCBD5E1, 1.0f);
            divCol = D2D1::ColorF(0x2E3648, 1.0f);
            break;
        case SkinTheme::CloudMist:
        default:
            bgTop = D2D1::ColorF(0xFFFFFF, 0.96f);
            bgBot = D2D1::ColorF(0xF1F6FC, 0.96f);
            borderCol = D2D1::ColorF(0xD3DFEE, 1.0f);
            innerCol = D2D1::ColorF(0xFFFFFF, 0.90f);
            accentCol = D2D1::ColorF(0x2563EB, 1.0f);
            textCol = D2D1::ColorF(0x475569, 1.0f);
            divCol = D2D1::ColorF(0xE2E8F0, 1.0f);
            break;
    }

    dcTarget_->BeginDraw();
    dcTarget_->Clear(D2D1::ColorF(0, 0, 0, 0));

    const float outerR = cfg_.squareFrame ? 6.0f : (kPillH * 0.5f);
    const float innerR = cfg_.squareFrame ? 5.0f : ((kPillH - 2.0f) * 0.5f);
    const float modeR = cfg_.squareFrame ? 3.5f : 10.0f;

    // Soft floating shadow (darker ambient drop shadow in ObsidianInk)
    const bool isDark = (cfg_.skin == SkinTheme::ObsidianInk);
    const uint32_t shadowRgb = isDark ? 0x000000 : 0x0F172A;
    const float shadowBaseAlpha = isDark ? 0.045f : 0.024f;
    for (int s = 4; s >= 1; --s) {
        float ex = static_cast<float>(s) * 1.6f;
        float sr = outerR + ex * (cfg_.squareFrame ? 0.30f : 0.50f);
        D2D1_ROUNDED_RECT shadowRect{
            D2D1::RectF(kPad - ex, kPad - ex + 2.0f, kPad + kPillW + ex, kPad + kPillH + ex + 2.0f),
            sr, sr
        };
        ID2D1SolidColorBrush* sb = nullptr;
        dcTarget_->CreateSolidColorBrush(D2D1::ColorF(shadowRgb, shadowBaseAlpha * (5 - s)), &sb);
        if (sb) {
            dcTarget_->FillRoundedRectangle(shadowRect, sb);
            sb->Release();
        }
    }

    ID2D1LinearGradientBrush* bgGrad = nullptr;
    {
        D2D1_GRADIENT_STOP stops[2] = {{0.0f, bgTop}, {1.0f, bgBot}};
        ID2D1GradientStopCollection* col = nullptr;
        if (SUCCEEDED(dcTarget_->CreateGradientStopCollection(stops, 2, &col)) && col) {
            dcTarget_->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, kPad), D2D1::Point2F(0, kPad + kPillH)),
                col, &bgGrad);
            col->Release();
        }
    }

    ID2D1SolidColorBrush* borderBrush = nullptr;
    ID2D1SolidColorBrush* innerBrush = nullptr;
    ID2D1SolidColorBrush* accentBrush = nullptr;
    ID2D1SolidColorBrush* textBrush = nullptr;
    ID2D1SolidColorBrush* divBrush = nullptr;
    ID2D1SolidColorBrush* whiteBrush = nullptr;
    ID2D1SolidColorBrush* mutedPillBrush = nullptr;
    dcTarget_->CreateSolidColorBrush(borderCol, &borderBrush);
    dcTarget_->CreateSolidColorBrush(innerCol, &innerBrush);
    dcTarget_->CreateSolidColorBrush(accentCol, &accentBrush);
    dcTarget_->CreateSolidColorBrush(textCol, &textBrush);
    dcTarget_->CreateSolidColorBrush(divCol, &divBrush);
    dcTarget_->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, 1.0f), &whiteBrush);
    dcTarget_->CreateSolidColorBrush(D2D1::ColorF(accentCol.r, accentCol.g, accentCol.b, 0.16f), &mutedPillBrush);

    D2D1_ROUNDED_RECT pill{
        D2D1::RectF(kPad, kPad, kPad + kPillW, kPad + kPillH),
        outerR, outerR
    };
    if (bgGrad) dcTarget_->FillRoundedRectangle(pill, bgGrad);
    D2D1_ROUNDED_RECT innerPill{
        D2D1::RectF(kPad + 1.0f, kPad + 1.0f, kPad + kPillW - 1.0f, kPad + kPillH - 1.0f),
        innerR, innerR
    };
    if (innerBrush) dcTarget_->DrawRoundedRectangle(innerPill, innerBrush, 1.0f);
    if (borderBrush) dcTarget_->DrawRoundedRectangle(pill, borderBrush, 1.0f);

    // 4 clean balanced zones: [中/EN box] | [。/. centered] | [layout icon] | [theme swatch]
    const float wZone = kPillW / 4.0f;
    const float cy = kPad + kPillH * 0.5f;
    if (hoverZone_ >= 0 && hoverZone_ < 4 && mutedPillBrush) {
        float hzL = kPad + wZone * static_cast<float>(hoverZone_) + 2.5f;
        float hzR = kPad + wZone * static_cast<float>(hoverZone_ + 1) - 2.5f;
        float hzRadius = cfg_.squareFrame ? 3.5f : 10.0f;
        D2D1_ROUNDED_RECT hoverRc{
            D2D1::RectF(hzL, kPad + 3.0f, hzR, kPad + kPillH - 3.0f),
            hzRadius, hzRadius
        };
        dcTarget_->FillRoundedRectangle(hoverRc, mutedPillBrush);
    }
    for (int i = 1; i < 4; ++i) {
        if (divBrush) {
            float x = kPad + wZone * i;
            dcTarget_->DrawLine(D2D1::Point2F(x, cy - 6.0f), D2D1::Point2F(x, cy + 6.0f), divBrush, 1.0f);
        }
    }

    // Zone 0: Active CN/EN badge
    {
        D2D1_ROUNDED_RECT modePill{
            D2D1::RectF(kPad + 4.0f, kPad + 4.0f, kPad + wZone - 3.0f, kPad + kPillH - 4.0f),
            modeR, modeR
        };
        if (cfg_.chineseMode && accentBrush) {
            dcTarget_->FillRoundedRectangle(modePill, accentBrush);
        } else if (mutedPillBrush) {
            dcTarget_->FillRoundedRectangle(modePill, mutedPillBrush);
        }
        const wchar_t* modeLbl = cfg_.chineseMode ? L"\u4e2d" : L"EN";
        ID2D1SolidColorBrush* lblBrush = cfg_.chineseMode ? whiteBrush : accentBrush;
        if (lblBrush) {
            dcTarget_->DrawText(modeLbl, static_cast<UINT32>(wcslen(modeLbl)), textFmt_, modePill.rect, lblBrush);
        }
    }

    // Zone 1: Optically centered punctuation symbol (CN hollow ring '。' vs EN dot '.')
    if (textBrush) {
        float cx = kPad + wZone * 1.5f;
        if (cfg_.chinesePunct) {
            dcTarget_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 4.0f, 4.0f), textBrush, 1.75f);
        } else {
            dcTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 2.4f, 2.4f), textBrush);
        }
    }

    // Zone 2: Clean vector layout icon (2 horizontal boxes vs 3 stacked rows)
    if (textBrush) {
        float cx = kPad + wZone * 2.5f;
        float layoutR = cfg_.squareFrame ? 1.0f : 2.0f;
        if (cfg_.layout == CandidateLayout::Horizontal) {
            D2D1_ROUNDED_RECT r1{D2D1::RectF(cx - 7.5f, cy - 2.5f, cx - 1.0f, cy + 2.5f), layoutR, layoutR};
            D2D1_ROUNDED_RECT r2{D2D1::RectF(cx + 1.0f, cy - 2.5f, cx + 7.5f, cy + 2.5f), layoutR, layoutR};
            dcTarget_->DrawRoundedRectangle(r1, textBrush, 1.4f);
            dcTarget_->DrawRoundedRectangle(r2, textBrush, 1.4f);
        } else {
            for (int row = -1; row <= 1; ++row) {
                float y = cy + static_cast<float>(row) * 4.2f;
                dcTarget_->DrawLine(D2D1::Point2F(cx - 6.0f, y), D2D1::Point2F(cx + 6.0f, y), textBrush, 1.6f);
            }
        }
    }

    // Zone 3: Theme swatch (crisp rounded square in squareFrame mode, circular dot in capsule mode)
    if (accentBrush) {
        float cx = kPad + wZone * 3.5f - 1.0f;
        if (cfg_.squareFrame) {
            if (mutedPillBrush) {
                D2D1_ROUNDED_RECT outerSq{D2D1::RectF(cx - 7.0f, cy - 7.0f, cx + 7.0f, cy + 7.0f), 2.5f, 2.5f};
                dcTarget_->FillRoundedRectangle(outerSq, mutedPillBrush);
            }
            D2D1_ROUNDED_RECT innerSq{D2D1::RectF(cx - 4.5f, cy - 4.5f, cx + 4.5f, cy + 4.5f), 1.5f, 1.5f};
            dcTarget_->FillRoundedRectangle(innerSq, accentBrush);
            if (whiteBrush) {
                dcTarget_->DrawRoundedRectangle(innerSq, whiteBrush, 1.0f);
            }
        } else {
            if (mutedPillBrush) {
                dcTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 8.0f, 8.0f), mutedPillBrush);
            }
            dcTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 4.8f, 4.8f), accentBrush);
            if (whiteBrush) {
                dcTarget_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 4.8f, 4.8f), whiteBrush, 1.0f);
            }
        }
    }

    dcTarget_->EndDraw();

    if (bgGrad) bgGrad->Release();
    if (borderBrush) borderBrush->Release();
    if (innerBrush) innerBrush->Release();
    if (accentBrush) accentBrush->Release();
    if (textBrush) textBrush->Release();
    if (divBrush) divBrush->Release();
    if (whiteBrush) whiteBrush->Release();
    if (mutedPillBrush) mutedPillBrush->Release();

    RECT curRc{};
    GetWindowRect(hwnd_, &curRc);
    POINT dstPt{curRc.left, curRc.top};
    if (!posInitialized_) {
        posInitialized_ = true;
        if (cfg_.statusPosX > -9000 && cfg_.statusPosY > -9000) {
            dstPt = {cfg_.statusPosX, cfg_.statusPosY};
        }
        HMONITOR mon = MonitorFromPoint(dstPt, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(mon, &mi)) {
            dstPt.x = std::clamp(static_cast<int>(dstPt.x), static_cast<int>(mi.rcWork.left),
                                 std::max(static_cast<int>(mi.rcWork.left), static_cast<int>(mi.rcWork.right) - pxW));
            dstPt.y = std::clamp(static_cast<int>(dstPt.y), static_cast<int>(mi.rcWork.top),
                                 std::max(static_cast<int>(mi.rcWork.top), static_cast<int>(mi.rcWork.bottom) - pxH));
        }
    }
    SIZE winSz{pxW, pxH};
    POINT srcPt{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

    UpdateLayeredWindow(hwnd_, screenDC, &dstPt, &winSz, surface_.dc(), &srcPt, 0, &blend, ULW_ALPHA);
    SetWindowPos(hwnd_, HWND_TOPMOST, dstPt.x, dstPt.y, pxW, pxH, SWP_NOACTIVATE | SWP_SHOWWINDOW);

    ReleaseDC(nullptr, screenDC);
}

void StatusWindow::clampToWorkArea() {
    if (!hwnd_) return;
    RECT rc{};
    GetWindowRect(hwnd_, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;
    POINT pt{rc.left, rc.top};
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return;
    int nx = std::clamp(static_cast<int>(rc.left), static_cast<int>(mi.rcWork.left),
                        std::max(static_cast<int>(mi.rcWork.left), static_cast<int>(mi.rcWork.right) - w));
    int ny = std::clamp(static_cast<int>(rc.top), static_cast<int>(mi.rcWork.top),
                        std::max(static_cast<int>(mi.rcWork.top), static_cast<int>(mi.rcWork.bottom) - h));
    if (nx != rc.left || ny != rc.top) {
        SetWindowPos(hwnd_, HWND_TOPMOST, nx, ny, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

LRESULT CALLBACK StatusWindow::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    StatusWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<StatusWindow*>(cs->lpCreateParams);
        if (self) self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<StatusWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->handleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT StatusWindow::handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_LBUTTONDOWN: {
            dragging_ = true;
            didMove_ = false;
            GetCursorPos(&dragStartCursor_);
            RECT rc{};
            GetWindowRect(hwnd, &rc);
            dragStartWin_ = {rc.left, rc.top};
            SetCapture(hwnd);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!hoverTracked_) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
                hoverTracked_ = true;
            }
            if (dragging_) {
                POINT cur{};
                GetCursorPos(&cur);
                int dx = cur.x - dragStartCursor_.x;
                int dy = cur.y - dragStartCursor_.y;
                if (std::abs(dx) > 4 || std::abs(dy) > 4) didMove_ = true;
                if (didMove_) {
                    SetWindowPos(hwnd, HWND_TOPMOST, dragStartWin_.x + dx, dragStartWin_.y + dy,
                                 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
                }
            } else {
                UINT dpi = queryDpiForWindow(hwnd);
                if (dpi == 0) dpi = 96;
                float scale = static_cast<float>(dpi) / 96.0f;
                float x = static_cast<float>(static_cast<short>(LOWORD(lParam))) / scale - kPad;
                float y = static_cast<float>(static_cast<short>(HIWORD(lParam))) / scale - kPad;
                int zone = -1;
                if (x >= 0.0f && x <= kPillW && y >= 0.0f && y <= kPillH) {
                    zone = std::clamp(static_cast<int>(x / (kPillW / 4.0f)), 0, 3);
                }
                if (zone != hoverZone_) {
                    hoverZone_ = zone;
                    if (visible_) render();
                }
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            hoverTracked_ = false;
            if (hoverZone_ != -1) {
                hoverZone_ = -1;
                if (visible_) render();
            }
            return 0;
        case WM_LBUTTONUP: {
            if (dragging_) {
                dragging_ = false;
                ReleaseCapture();
                if (didMove_) {
                    clampToWorkArea();
                    if (hostHwnd_) {
                        RECT rc{};
                        GetWindowRect(hwnd, &rc);
                        PostMessageW(hostHwnd_, WM_APP_STATUS_POS_CHANGED,
                                     static_cast<WPARAM>(static_cast<intptr_t>(rc.left)),
                                     static_cast<LPARAM>(static_cast<intptr_t>(rc.top)));
                    }
                } else if (hostHwnd_) {
                    UINT dpi = queryDpiForWindow(hwnd);
                    if (dpi == 0) dpi = 96;
                    float scale = static_cast<float>(dpi) / 96.0f;
                    float x = static_cast<float>(LOWORD(lParam)) / scale - kPad;
                    int zone = static_cast<int>(x / (kPillW / 4.0f));
                    if (zone == 0) PostMessageW(hostHwnd_, WM_APP_ENGINE_ACTION, static_cast<WPARAM>(HostAction::ToggleChineseEnglish), 0);
                    else if (zone == 1) PostMessageW(hostHwnd_, WM_APP_ENGINE_ACTION, static_cast<WPARAM>(HostAction::TogglePunct), 0);
                    else if (zone == 2) PostMessageW(hostHwnd_, WM_APP_ENGINE_ACTION, static_cast<WPARAM>(HostAction::ToggleLayout), 0);
                    else if (zone == 3) PostMessageW(hostHwnd_, WM_APP_ENGINE_ACTION, static_cast<WPARAM>(HostAction::CycleSkin), 0);
                }
            }
            return 0;
        }
        case WM_RBUTTONUP: {
            if (hostHwnd_) {
                PostMessageW(hostHwnd_, WM_APP_TRAY_CALLBACK, 0, WM_RBUTTONUP);
            }
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace liteime::host
