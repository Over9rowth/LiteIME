#pragma once

#include "common/config.hpp"
#include "engine/bilingual_engine.hpp"
#include "host/layered_surface.hpp"

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>
#include <vector>

namespace liteime::host {

struct CandidateViewState {
    bool visible = false;
    std::string headerBuffer;
    std::vector<engine::CandidateItem> items;
    int selectedIndex = 0;
    int page = 0;
    int totalPages = 0;
    bool chineseMode = true;
    SkinTheme skin = SkinTheme::CloudMist;
    CandidateLayout layout = CandidateLayout::Horizontal;
    bool squareFrame = true;
    engine::SymbolCatalogMode catalogMode = engine::SymbolCatalogMode::None;
    int caretX = 200;
    int caretY = 200;
    int caretHeight = 20;
};

class CandidateWindow {
public:
    CandidateWindow() = default;
    ~CandidateWindow();

    bool create(HINSTANCE hInst);
    void setHostHwnd(HWND hostHwnd) { hostHwnd_ = hostHwnd; }
    void update(const CandidateViewState& state);
    void hide();
    HWND hwnd() const { return hwnd_; }

private:
    struct HitRegion {
        D2D1_RECT_F rect{};
        int actionType = 0; // 0 = candidate index, 1 = prev page, 2 = next page
        int index = 0;
    };

    struct PinMenuRow {
        std::wstring label;
        WORD actionVal = 0; // 0..4 => pin slot 1..5, 0xFFFF => unpin, 0xFFFE => remove user word
    };

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK pinPopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handlePinPopupMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void showPinPopup(int candIndex, POINT screenPt);
    void hidePinPopup();
    void renderPinPopup();
    void renderAndShow();
    void renderSymbolGrid();
    void presentCardSurface(HDC screenDC, float shadowPad, float scale, int pxW, int pxH);
    void ensureD2DResources();
    void releaseD2DResources();

    HWND hwnd_ = nullptr;
    HWND hostHwnd_ = nullptr;
    HWND pinPopupHwnd_ = nullptr;
    HINSTANCE hInst_ = nullptr;
    int pinMenuCandIdx_ = -1;
    int pinMenuHover_ = -1;
    bool pinHoverTracked_ = false;
    POINT pinPopupOrigin_{0, 0};
    std::vector<PinMenuRow> pinMenuRows_;
    CandidateViewState state_;
    std::vector<HitRegion> hitRegions_;
    int hoverRegion_ = -1;
    bool hoverTracked_ = false;

    ID2D1Factory* d2dFactory_ = nullptr;
    IDWriteFactory* dwFactory_ = nullptr;
    ID2D1DCRenderTarget* dcTarget_ = nullptr;
    IDWriteTextFormat* textFmt_ = nullptr;
    IDWriteTextFormat* smallFmt_ = nullptr;
    IDWriteTextFormat* indexFmt_ = nullptr;
    IDWriteTextFormat* menuFmt_ = nullptr;
    LayeredSurface cardSurface_;
    LayeredSurface pinSurface_;
};

} // namespace liteime::host
