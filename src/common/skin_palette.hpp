#pragma once

#include "common/config.hpp"
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>

namespace liteime::host {

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

inline SkinPalette paletteFor(SkinTheme skin) {
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

inline ID2D1LinearGradientBrush* makeVerticalGradient(
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

inline D2D1_SIZE_F measureDWriteText(IDWriteFactory* dw, IDWriteTextFormat* fmt, const std::wstring& s) {
    if (!dw || !fmt || s.empty()) return {0.0f, 20.0f};
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

} // namespace liteime::host
