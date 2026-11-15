#pragma once

#include "common/config.hpp"
#include "host/layered_surface.hpp"
#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>

namespace liteime::host {

class StatusWindow {
public:
    StatusWindow() = default;
    ~StatusWindow();

    bool create(HINSTANCE hInst, HWND ownerHostHwnd);
    void update(const EngineConfig& cfg);
    void setVisible(bool visible);
    void clampToWorkArea();
    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void render();
    void ensureResources();
    void releaseResources();

    HWND hwnd_ = nullptr;
    HWND hostHwnd_ = nullptr;
    EngineConfig cfg_{};
    bool visible_ = true;
    bool dragging_ = false;
    bool posInitialized_ = false;
    POINT dragStartCursor_{};
    POINT dragStartWin_{};
    bool didMove_ = false;
    int hoverZone_ = -1;
    bool hoverTracked_ = false;
    LayeredSurface surface_;

    ID2D1Factory* d2dFactory_ = nullptr;
    IDWriteFactory* dwFactory_ = nullptr;
    ID2D1DCRenderTarget* dcTarget_ = nullptr;
    IDWriteTextFormat* textFmt_ = nullptr;
};

} // namespace liteime::host
