#pragma once

#include "engine/bilingual_engine.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace liteime::engine::detail {

inline std::string toLowerAscii(std::string_view sv) {
    std::string out(sv);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

inline double freqToLogScore(uint32_t freq) {
    // Negative log-likelihood relative to ~1e9 corpus scale:
    // fewer, longer tokens incur fewer negative penalties (true Viterbi LM math).
    return std::log10(static_cast<double>(std::max<uint32_t>(freq, 1)) + 1.0) - 9.5;
}

inline size_t countSyllablesInPinyin(std::string_view pinyin) {
    if (pinyin.empty()) return 0;
    size_t count = 1;
    for (char c : pinyin) {
        if (c == ' ') ++count;
    }
    return count;
}

inline std::string formatPinyinApostrophe(std::string_view pinyin) {
    std::string out(pinyin);
    for (char& c : out) {
        if (c == ' ') c = '\'';
    }
    return out;
}

inline std::string formatPinyinConcat(std::string_view pinyin) {
    std::string out;
    out.reserve(pinyin.size());
    for (char c : pinyin) {
        if (c != ' ') out.push_back(c);
    }
    return out;
}

} // namespace liteime::engine::detail
