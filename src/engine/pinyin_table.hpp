#pragma once

#include "common/config.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace liteime::engine {

struct SyllableToken {
    std::string text;       // normalized syllable or initial, e.g. "ni" or "z"
    bool isPartial = false; // true if only an initial or incomplete prefix (jianpin)
    uint16_t start = 0;     // byte offset in raw input
    uint16_t length = 0;    // byte length in raw input
};

struct SegmentPath {
    std::vector<SyllableToken> tokens;
    int score = 0;
    bool hasTypo = false;

    std::string joinedKey() const;       // e.g. "ni hao"
    std::string preeditDisplay() const;  // e.g. "ni'hao"
    std::string initialsKey() const;     // e.g. "nh"
};

class PinyinTable {
public:
    // Normalizes keyboard v/u aliases (j/q/x/y + v* -> u*, nue->nve, lue->lve; keeps nv/lv distinct from nu/lu).
    static std::string canonicalizeSyllable(std::string_view token);
    static bool isValidSyllable(std::string_view s);
    static bool isValidInitial(std::string_view s);
    static bool isSyllablePrefix(std::string_view s);
    static std::string extractInitial(std::string_view syllable);
    static std::vector<std::string_view> syllablesWithPrefix(std::string_view prefix);

    // Segments lowercase ASCII input (allowing ' as manual separator) into top-K paths.
    static std::vector<SegmentPath> segment(std::string_view input, size_t maxPaths = 4);

    // Segments lowercase input under the configured PinyinScheme (Quanpin or Shuangpin).
    static std::vector<SegmentPath> segmentScheme(std::string_view input,
                                                  PinyinScheme scheme,
                                                  size_t maxPaths = 4);

    // Decodes a 2-key Shuangpin syllable code into canonical full pinyin syllables.
    static std::vector<std::string> decodeShuangpinPair(char c1, char c2, PinyinScheme scheme);
    static std::string decodeShuangpinInitialKey(char c1, PinyinScheme scheme);

    // Segments input allowing obvious single-syllable typo corrections when raw text has invalid sequences.
    static std::vector<SegmentPath> segmentWithTypos(std::string_view input, size_t maxPaths = 3);

    // Returns valid corrected syllables for an invalid syllable token (empty if rawToken is already valid).
    static std::vector<std::string> suggestSyllableTypos(std::string_view rawToken);

    // Expands a single syllable using FuzzyConfig (always includes original first).
    static std::vector<std::string> expandFuzzy(std::string_view syllable, const FuzzyConfig& fuzzy);

    // Expands multi-syllable tokens into distinct fuzzy syllable sequences (excluding the original).
    static std::vector<std::vector<std::string>> expandFuzzyTokens(
        const std::vector<SyllableToken>& tokens,
        const FuzzyConfig& fuzzy,
        size_t maxCombinations = 12);
};

} // namespace liteime::engine
