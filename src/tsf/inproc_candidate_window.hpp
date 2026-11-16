#pragma once

#include "common/ipc_protocol.hpp"
#include "common/layered_surface.hpp"
#include <d2d1.h>
#include <dwrite.h>
#include <cstdint>
#include <functional>
#include <vector>

namespace liteime::tsf {

class InProcCandidateWindow {
public:
    using SelectCb = std::function<void(int)>;
    using PageCb = std::function<void(bool)>;

    InProcCandidateWindow() = default;
    ~InProcCandidateWindow();

    InProcCandidateWindow(const InProcCandidateWindow&) = delete;
    InProcCandidateWindow& operator=(const InProcCandidateWindow&) = delete;

    void update(const ipc::Response& resp,
                int caretX, int caretY, int caretHeight,
                uint64_t ownerHint,
                SelectCb onSelect,
                PageCb onPage);
    void moveCaret(int caretX, int caretY, int caretHeight);
    void hide();
    void destroy();
    bool isVisible() const { return visible_ && hwnd_ && IsWindowVisible(hwnd_); }

private:
    struct HitRegion {
        D2D1_RECT_F rect{};
        int kind = 0; // 0=candidate, 1=prev, 2=next
        int index = 0;
    };

    HWND resolveOwnerRoot(uint64_t ownerHint) const;
    bool ensureWindow(HWND ownerRoot);
    bool ensureD2D();
    void releaseD2D();
    void renderAndShow();
    void positionWindow(int pxW, int pxH, float scale);

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;
    HWND ownerRoot_ = nullptr;
    bool visible_ = false;
    int caretX_ = 300;
    int caretY_ = 300;
    int caretH_ = 22;
    int lastPxW_ = 0;
    int lastPxH_ = 0;
    float lastScale_ = 1.0f;
    int hoverRegion_ = -1;
    bool hoverTracked_ = false;

    ipc::Response lastResp_{};
    SelectCb onSelect_;
    PageCb onPage_;
    std::vector<HitRegion> hitRegions_;

    HMODULE d2dMod_ = nullptr;
    HMODULE dwriteMod_ = nullptr;
    ID2D1Factory* d2dFactory_ = nullptr;
    ID2D1DCRenderTarget* dcTarget_ = nullptr;
    IDWriteFactory* dwFactory_ = nullptr;
    IDWriteTextFormat* textFmt_ = nullptr;
    IDWriteTextFormat* smallFmt_ = nullptr;
    IDWriteTextFormat* indexFmt_ = nullptr;
    liteime::host::LayeredSurface surface_;
};

} // namespace liteime::tsf
