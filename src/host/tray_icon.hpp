#pragma once

#include "common/config.hpp"
#include <windows.h>

namespace liteime::host {

class TrayIcon {
public:
    TrayIcon() = default;
    ~TrayIcon();

    bool create(HWND hostHwnd, HINSTANCE hInst);
    void destroy();
    void showContextMenu(const EngineConfig& cfg);
    void updateTip(const EngineConfig& cfg);

private:
    HWND hostHwnd_ = nullptr;
    HICON icon_ = nullptr;
    bool added_ = false;
    bool hasLastTip_ = false;
    bool lastTipChinese_ = true;
    SkinTheme lastTipSkin_ = SkinTheme::CloudMist;
    bool lastTipSquare_ = true;
};

} // namespace liteime::host
