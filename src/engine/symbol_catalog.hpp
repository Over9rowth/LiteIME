#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace liteime::engine {

enum class SymbolLibrary : uint8_t {
    Kaomoji = 1,
    Emoji   = 2,
    Greek   = 3,
    Symbol  = 4,
};

struct SymbolEntry {
    const char* text;           // Actual symbol/kaomoji/emoji UTF-8 text committed on pick
    const char* label;          // Short descriptive comment displayed inside vvvv catalog
    const char* inlineTriggers; // Exact concatenated full-pinyin triggers for normal typing (or "")
    const char* searchKeywords; // Extra pipe-separated pinyin/jianpin/alias keys for vvvv filtering
    SymbolLibrary library;
    int inlineSlot = -1;        // 0-based preferred slot in normal pinyin mode (1=2nd, 3=4th, 4=5th)
};

struct InlineSymbolHit {
    const SymbolEntry* entry = nullptr;
    int preferredSlot = 4;
};

class SymbolCatalog {
public:
    static const std::vector<SymbolEntry>& allEntries();

    // Matches exact inline pinyin triggers (concatenated lowercase, e.g. "wu", "dunhao", "kaixin", "aerfa").
    // Sorted by preferredSlot ASC.
    static std::vector<InlineSymbolHit> matchInline(std::string_view concatPinyin);

    // Searches entries in a specific library (or all entries if filterByLibrary is false).
    // Empty query returns all entries of that library in curated order.
    static std::vector<const SymbolEntry*> queryLibrary(SymbolLibrary lib,
                                                        std::string_view query,
                                                        bool filterByLibrary = true);
};

} // namespace liteime::engine
