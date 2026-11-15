#pragma once

#include "common/config.hpp"
#include "engine/bigram_model.hpp"
#include "engine/binary_dict.hpp"
#include "engine/pinyin_table.hpp"
#include "engine/user_dict.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace liteime::engine {

enum class CandidateKind : uint8_t {
    ChineseSentence = 0,
    ChinesePhrase = 1,
    ChineseJianpin = 2,
    EnglishWord = 3,
    DynamicShortcut = 4,
    RawInput = 5,
    FuzzyPinyin = 6,
    LooseInitial = 7,
    TypoCorrection = 8,
    QuickInput = 9,
    QuickInputSetup = 10,
};

struct CandidateItem {
    std::string text;
    std::string comment;       // optional hint, e.g. "[EN]", "(wu'ru)", "(weilai)"
    std::string matchedKey;    // pinyin key matched (for user dict learning)
    std::string commitText;    // actual committed text when different from display text (e.g. masked QuickInput)
    size_t consumedRawChars = 0; // bytes consumed from rawBuffer_
    double score = 0.0;
    CandidateKind kind = CandidateKind::ChinesePhrase;
    int pinnedSlot = -1;       // 0..4 if custom-pinned for current raw input, else -1
};

struct KeyActionResult {
    bool consumed = false;
    bool showCandidates = false;
    bool openQuickInputDialog = false;
    bool isQuickInput = false;
    std::string preeditText;    // inline text shown in target app
    std::string headerBuffer;   // pinyin/code shown at top of candidate window
    std::string commitText;
};

class BilingualEngine {
public:
    BilingualEngine() = default;

    void setDictionary(const BinaryDict* dict) { dict_ = dict; }
    void clearExtraDictionaries() { extraDicts_.clear(); }
    void addExtraDictionary(const BinaryDict* dict);

    UserDict& userDict() { return userDict_; }
    const UserDict& userDict() const { return userDict_; }

    BigramModel& bigramModel() { return bigram_; }
    const BigramModel& bigramModel() const { return bigram_; }

    EngineConfig& config() { return config_; }
    const EngineConfig& config() const { return config_; }

    // Main key processing entry point.
    // vk uses standard Windows Virtual-Key codes (e.g. 'A'..'Z', VK_SPACE=0x20, VK_RETURN=0x0D, VK_SHIFT=0x10).
    KeyActionResult handleKey(uint32_t vk, uint32_t modifiers, bool isKeyUp, bool testOnly = false);

    // Direct UI actions (e.g. clicking or right-click pinning a candidate on the current page)
    KeyActionResult selectCandidateOnPage(int indexInPage);
    bool pinCandidateOnPage(int indexInPage, int slot0To4);
    bool unpinCandidateOnPage(int indexInPage);
    bool removeUserWordOnPage(int indexInPage);
    KeyActionResult nextPage();
    KeyActionResult prevPage();
    void resetComposition();
    void notifyFocusChanged(bool gained);

    bool isComposing() const { return !rawBuffer_.empty() || !committedPrefixWords_.empty(); }
    int currentPage() const { return currentPage_; }
    int totalPages() const;
    int selectedIndexInPage() const { return selectedInPage_; }
    std::vector<CandidateItem> currentPageItems() const;
    const std::vector<CandidateItem>& allCandidates() const { return candidates_; }
    const std::string& rawBuffer() const { return rawBuffer_; }
    const std::string& segmentedHeader() const { return segmentedHeader_; }
    std::string formattedHeader() const;

private:
    struct PrefixStep {
        std::string word;
        std::string pinyinKey;
        std::string rawConsumed;
    };

    std::vector<DictCandidate> queryExactAll(std::string_view key, size_t limit) const;
    std::vector<DictCandidate> queryPinyinPrefixAll(std::string_view prefix, size_t limit) const;
    std::vector<DictCandidate> queryJianpinAll(std::string_view initials, size_t limit) const;
    std::vector<DictCandidate> queryEnglishPrefixAll(std::string_view prefix, size_t limit) const;
    bool hasAnyDict() const;

    void rebuildCandidates();
    std::string buildWholeSentence(const SegmentPath& path, double& outScore) const;
    double contextBigramBonus(std::string_view prevWord, std::string_view curWord) const;
    std::string mapPunctuation(uint32_t vk, bool shift) const;
    std::string highlightedCandidateText() const;
    KeyActionResult buildComposeResult() const;
    KeyActionResult commitCandidateAtGlobalIndex(size_t idx);
    KeyActionResult finalizeCommit(const std::string& text,
                                   bool resetModeFlags = true,
                                   bool isQuickInput = false,
                                   std::string nextBigramCtx = {});
    void flushPendingLearnedPhrase();
    void undoLastPrefixStep();

    const BinaryDict* dict_ = nullptr;
    std::vector<const BinaryDict*> extraDicts_;
    UserDict userDict_;
    BigramModel bigram_;
    EngineConfig config_;

    std::string rawBuffer_;
    std::string segmentedHeader_;
    std::string committedPrefixWords_;
    std::string committedPrefixPinyin_;
    std::string lastCommittedWord_;
    std::vector<PrefixStep> prefixSteps_;
    std::vector<CandidateItem> candidates_;
    int currentPage_ = 0;
    int selectedInPage_ = 0;

    bool shiftDownOnly_ = false;
    bool doubleQuoteOpen_ = true;
    bool singleQuoteOpen_ = true;
    bool urlMode_ = false;
    char lastCommittedChar_ = '\0';
    int lastCommittedQuoteKind_ = 0; // 0=none, 1=double, 2=single
};

} // namespace liteime::engine
