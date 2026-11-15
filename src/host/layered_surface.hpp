#pragma once

#include <windows.h>

namespace liteime::host {

class LayeredSurface {
public:
    LayeredSurface() = default;
    ~LayeredSurface() { release(); }

    LayeredSurface(const LayeredSurface&) = delete;
    LayeredSurface& operator=(const LayeredSurface&) = delete;

    bool ensure(HDC screenDC, int pxW, int pxH) {
        if (pxW <= 0 || pxH <= 0) return false;
        if (memDC_ && bmp_ && width_ == pxW && height_ == pxH) {
            return true;
        }
        release();
        memDC_ = CreateCompatibleDC(screenDC);
        if (!memDC_) return false;

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = pxW;
        bmi.bmiHeader.biHeight = -pxH;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        bmp_ = CreateDIBSection(memDC_, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bmp_) {
            DeleteDC(memDC_);
            memDC_ = nullptr;
            return false;
        }
        oldBmp_ = SelectObject(memDC_, bmp_);
        width_ = pxW;
        height_ = pxH;
        return true;
    }

    void release() {
        if (memDC_) {
            if (oldBmp_) {
                SelectObject(memDC_, oldBmp_);
                oldBmp_ = nullptr;
            }
            DeleteDC(memDC_);
            memDC_ = nullptr;
        }
        if (bmp_) {
            DeleteObject(bmp_);
            bmp_ = nullptr;
        }
        width_ = 0;
        height_ = 0;
    }

    HDC dc() const { return memDC_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    HDC memDC_ = nullptr;
    HBITMAP bmp_ = nullptr;
    HGDIOBJ oldBmp_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

} // namespace liteime::host
