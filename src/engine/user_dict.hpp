#pragma once

#include "common/config.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace liteime::engine {

struct UserEntry {
    std::string word;
    uint32_t count = 1;
    uint64_t lastUsed = 0;
    bool isPhrase = false;
};

struct PinnedEntry {
    int slot = 0; // 0..4 (display 1..5)
    std::string word;
};

struct QuickInputItem {
    std::string label;
    std::string value;
};

class UserDict {
public:
    bool load(const std::filesystem::path& path);
    bool save() const;
    bool saveIfDirty() const;
    bool dirty() const { return dirty_ || bigramDirty_; }
    void markDirty() { dirty_ = true; }

    void recordSelection(std::string_view pinyinKey, std::string_view word, bool isAssembledPhrase = false);
    bool removeEntry(std::string_view pinyinKey, std::string_view word);

    // Dynamic user bigram co-occurrence learning ("prevWord -> curWord")
    void recordBigram(std::string_view prevWord, std::string_view curWord);
    double getBigramBonus(std::string_view prevWord, std::string_view curWord) const;
    bool loadBigrams(const std::filesystem::path& path);
    bool saveBigrams() const;
    std::filesystem::path bigramsPath() const;

    std::vector<UserEntry> lookup(std::string_view pinyinKey) const;
    std::vector<std::pair<std::string, UserEntry>> lookupSingleSyllablePrefix(std::string_view prefix) const;
    std::vector<std::pair<std::string, UserEntry>> lookupByInitials(std::string_view initials) const;
    // Matches multi-syllable user entries against mixed full-syllable / partial-initial tokens:
    // each pair is (normalizedTokenText, isPartial).
    static bool matchesTokenPattern(std::string_view pinyinKey,
                                    const std::vector<std::pair<std::string_view, bool>>& patternTokens,
                                    size_t* extendedPartialCount = nullptr);
    std::vector<std::pair<std::string, UserEntry>> lookupByTokenPattern(
        const std::vector<std::pair<std::string_view, bool>>& patternTokens) const;
    uint32_t getBoost(std::string_view pinyinKey, std::string_view word) const;
    uint32_t getTier(std::string_view pinyinKey, std::string_view word) const;

    // Tier thresholds: tier 0 (<2), tier 1 (>=2), tier 2 (>=5, +3), tier 3 (>=9, +4),
    // tier 4 (>=14, +5), tier 5 (>=19, +5), ... (per-tier increment capped at 5).
    static uint32_t tierForCount(uint32_t count);

    // Custom candidate pinning to slots 0..4 (1..5) keyed by normalized raw input.
    static std::string normalizePinKey(std::string_view rawInput);
    void setPin(std::string_view rawKey, int slot, std::string_view word);
    bool unpin(std::string_view rawKey, std::string_view word);
    int getPinnedSlot(std::string_view rawKey, std::string_view word) const;
    std::vector<PinnedEntry> getPins(std::string_view rawKey) const;
    bool loadPins(const std::filesystem::path& path);
    bool savePins() const;

    // Quick-input personal snippets ("vvv") stored locally and masked in UI preview.
    static constexpr size_t kMaxQuickInputBytes = 4095;
    const std::vector<QuickInputItem>& quickInputs() const { return quickInputs_; }
    bool setQuickInputs(std::vector<QuickInputItem> items);
    static std::string maskQuickValue(std::string_view value);
    bool loadQuickInputs(const std::filesystem::path& path);
    bool saveQuickInputs() const;

    std::filesystem::path pinsPath() const;
    std::filesystem::path quickInputsPath() const;
    std::filesystem::path configPath() const;

    static bool loadConfig(const std::filesystem::path& path, EngineConfig& cfg);
    static bool saveConfig(const std::filesystem::path& path, const EngineConfig& cfg);

    void clear();

private:
    std::filesystem::path path_;
    uint64_t clock_ = 1;
    mutable bool dirty_ = false;
    mutable bool bigramDirty_ = false;
    std::unordered_map<std::string, std::vector<UserEntry>> entries_;
    std::unordered_map<std::string, std::vector<PinnedEntry>> pins_;
    std::unordered_map<std::string, std::unordered_map<std::string, uint32_t>> bigrams_;
    std::vector<QuickInputItem> quickInputs_;
};

} // namespace liteime::engine
