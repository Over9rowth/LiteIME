#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace liteime {

inline std::wstring utf8_to_utf16(std::string_view utf8) {
    std::wstring out;
    size_t i = 0;
    while (i < utf8.size()) {
        uint32_t cp = 0;
        uint8_t c = static_cast<uint8_t>(utf8[i]);
        if (c < 0x80) {
            cp = c;
            ++i;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < utf8.size()) {
            cp = ((c & 0x1F) << 6) | (static_cast<uint8_t>(utf8[i + 1]) & 0x3F);
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < utf8.size()) {
            cp = ((c & 0x0F) << 12) |
                 ((static_cast<uint8_t>(utf8[i + 1]) & 0x3F) << 6) |
                 (static_cast<uint8_t>(utf8[i + 2]) & 0x3F);
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < utf8.size()) {
            cp = ((c & 0x07) << 18) |
                 ((static_cast<uint8_t>(utf8[i + 1]) & 0x3F) << 12) |
                 ((static_cast<uint8_t>(utf8[i + 2]) & 0x3F) << 6) |
                 (static_cast<uint8_t>(utf8[i + 3]) & 0x3F);
            i += 4;
        } else {
            cp = 0xFFFD;
            ++i;
        }

        if (cp <= 0xFFFF) {
            out.push_back(static_cast<wchar_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 | (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 | (cp & 0x3FF)));
        }
    }
    return out;
}

inline std::string utf16_to_utf8(std::wstring_view utf16) {
    std::string out;
    size_t i = 0;
    while (i < utf16.size()) {
        uint32_t cp = static_cast<uint16_t>(utf16[i]);
        ++i;
        if (cp >= 0xD800 && cp <= 0xDBFF && i < utf16.size()) {
            uint32_t lo = static_cast<uint16_t>(utf16[i]);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + (((cp - 0xD800) << 10) | (lo - 0xDC00));
                ++i;
            }
        }
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

} // namespace liteime
