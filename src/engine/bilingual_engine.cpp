#include "engine/engine_internal.hpp"
#include "common/ipc_protocol.hpp"

#include <algorithm>

namespace liteime::engine {

namespace {

// Standard Win32 VK codes (kept explicit so engine compiles cleanly in tests too)
constexpr uint32_t kVkBack    = 0x08;
constexpr uint32_t kVkTab     = 0x09;
constexpr uint32_t kVkReturn  = 0x0D;
constexpr uint32_t kVkShift   = 0x10;
constexpr uint32_t kVkControl = 0x11;
constexpr uint32_t kVkEscape  = 0x1B;
constexpr uint32_t kVkSpace   = 0x20;
constexpr uint32_t kVkPrior   = 0x21; // PageUp
constexpr uint32_t kVkNext    = 0x22; // PageDown
constexpr uint32_t kVkLeft    = 0x25;
constexpr uint32_t kVkUp      = 0x26;
constexpr uint32_t kVkRight   = 0x27;
constexpr uint32_t kVkDown    = 0x28;
constexpr uint32_t kVkDelete  = 0x2E;
constexpr uint32_t kVkLShift  = 0xA0;
constexpr uint32_t kVkRShift  = 0xA1;
constexpr uint32_t kVkOem1    = 0xBA; // ;:
constexpr uint32_t kVkOemPlus = 0xBB; // =+
constexpr uint32_t kVkOemComma= 0xBC; // ,<
constexpr uint32_t kVkOemMinus= 0xBD; // -_
constexpr uint32_t kVkOemPeriod=0xBE; // .>
constexpr uint32_t kVkOem2    = 0xBF; // /?
constexpr uint32_t kVkOem3    = 0xC0; // `~
constexpr uint32_t kVkOem4    = 0xDB; // [{
constexpr uint32_t kVkOem5    = 0xDC; // \|
constexpr uint32_t kVkOem6    = 0xDD; // ]}
constexpr uint32_t kVkOem7    = 0xDE; // '"
constexpr uint32_t kVkNumpad0 = 0x60;
constexpr uint32_t kVkNumpad9 = 0x69;
constexpr uint32_t kVkMultiply= 0x6A;
constexpr uint32_t kVkAdd     = 0x6B;
constexpr uint32_t kVkSubtract= 0x6D;
constexpr uint32_t kVkDecimal = 0x6E;
constexpr uint32_t kVkDivide  = 0x6F;

char digitFromVk(uint32_t vk, bool shift) {
    if (!shift && vk >= '0' && vk <= '9') {
        return static_cast<char>(vk);
    }
    if (vk >= kVkNumpad0 && vk <= kVkNumpad9) {
        return static_cast<char>('0' + (vk - kVkNumpad0));
    }
    return '\0';
}

char mapAsciiPrintableVk(uint32_t vk, bool shift, bool caps) {
    if (vk == kVkSpace) return ' ';
    if (vk >= 'A' && vk <= 'Z') {
        bool upper = (shift != caps);
        return static_cast<char>(upper ? vk : (vk - 'A' + 'a'));
    }
    if (vk >= '0' && vk <= '9') {
        static constexpr char kShiftDigits[] = ")!@#$%^&*(";
        return shift ? kShiftDigits[vk - '0'] : static_cast<char>(vk);
    }
    if (vk >= kVkNumpad0 && vk <= kVkNumpad9) {
        return static_cast<char>('0' + (vk - kVkNumpad0));
    }
    switch (vk) {
        case kVkMultiply: return '*';
        case kVkAdd:      return '+';
        case kVkSubtract: return '-';
        case kVkDecimal:  return '.';
        case kVkDivide:   return '/';
        case kVkOem1:     return shift ? ':' : ';';
        case kVkOemPlus:  return shift ? '+' : '=';
        case kVkOemComma: return shift ? '<' : ',';
        case kVkOemMinus: return shift ? '_' : '-';
        case kVkOemPeriod:return shift ? '>' : '.';
        case kVkOem2:     return shift ? '?' : '/';
        case kVkOem3:     return shift ? '~' : '`';
        case kVkOem4:     return shift ? '{' : '[';
        case kVkOem5:     return shift ? '|' : '\\';
        case kVkOem6:     return shift ? '}' : ']';
        case kVkOem7:     return shift ? '"' : '\'';
        default:          return '\0';
    }
}

std::string toFullWidthUtf8(std::string_view utf8) {
    std::string out;
    out.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size(); ++i) {
        unsigned char b = static_cast<unsigned char>(utf8[i]);
        if (b == ' ') {
            out += "\xE3\x80\x80"; // U+3000 IDEOGRAPHIC SPACE
        } else if (b >= 0x21 && b <= 0x7E) {
            uint32_t cp = static_cast<uint32_t>(b) + 0xFEE0u;
            out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            out.push_back(utf8[i]);
        }
    }
    return out;
}

bool isShiftVk(uint32_t vk) {
    return vk == kVkShift || vk == kVkLShift || vk == kVkRShift;
}

bool isUrlTrigger(const std::string& s) {
    return s == "http" || s == "https" || s == "www." || s == "ftp" ||
           s.find("://") != std::string::npos || s.find('@') != std::string::npos;
}

bool canLearnCandidateKind(CandidateKind kind) {
    switch (kind) {
        case CandidateKind::ChineseSentence:
        case CandidateKind::ChinesePhrase:
        case CandidateKind::ChineseJianpin:
        case CandidateKind::EnglishWord:
            return true;
        default:
            return false;
    }
}

} // namespace

void BilingualEngine::resetComposition() {
    rawBuffer_.clear();
    segmentedHeader_.clear();
    committedPrefixWords_.clear();
    committedPrefixPinyin_.clear();
    prefixSteps_.clear();
    candidates_.clear();
    currentPage_ = 0;
    selectedInPage_ = 0;
    urlMode_ = false;
}

void BilingualEngine::notifyFocusChanged(bool gained) {
    if (!gained) {
        resetComposition();
        shiftDownOnly_ = false;
        doubleQuoteOpen_ = true;
        singleQuoteOpen_ = true;
        userDict_.saveIfDirty();
    }
    lastCommittedChar_ = '\0';
    lastCommittedQuoteKind_ = 0;
}

int BilingualEngine::totalPages() const {
    if (candidates_.empty()) return 0;
    int ps = std::clamp(config_.pageSize, 3, 9);
    return static_cast<int>((candidates_.size() + ps - 1) / ps);
}

std::vector<CandidateItem> BilingualEngine::currentPageItems() const {
    if (candidates_.empty()) return {};
    int ps = std::clamp(config_.pageSize, 3, 9);
    size_t start = static_cast<size_t>(currentPage_) * ps;
    if (start >= candidates_.size()) return {};
    size_t end = std::min(candidates_.size(), start + ps);
    return std::vector<CandidateItem>(candidates_.begin() + start, candidates_.begin() + end);
}

std::string BilingualEngine::formattedHeader() const {
    const std::string& py = segmentedHeader_.empty() ? rawBuffer_ : segmentedHeader_;
    if (committedPrefixWords_.empty()) return py;
    if (py.empty()) return committedPrefixWords_;
    return committedPrefixWords_ + " " + py;
}

std::string BilingualEngine::highlightedCandidateText() const {
    auto page = currentPageItems();
    if (selectedInPage_ >= 0 && static_cast<size_t>(selectedInPage_) < page.size()) {
        const auto& item = page[selectedInPage_];
        if (item.kind == CandidateKind::QuickInput || item.kind == CandidateKind::QuickInputSetup) {
            return rawBuffer_;
        }
        return item.text;
    }
    if (!candidates_.empty()) {
        if (candidates_[0].kind == CandidateKind::QuickInput || candidates_[0].kind == CandidateKind::QuickInputSetup) {
            return rawBuffer_;
        }
        return candidates_[0].text;
    }
    return rawBuffer_;
}

KeyActionResult BilingualEngine::buildComposeResult() const {
    KeyActionResult res;
    res.consumed = true;
    res.showCandidates = !candidates_.empty();
    res.headerBuffer = formattedHeader();
    switch (config_.inlinePreedit) {
        case InlinePreeditMode::None:
            res.preeditText.clear();
            break;
        case InlinePreeditMode::CommittedPlusPinyin:
            res.preeditText = committedPrefixWords_ + (segmentedHeader_.empty() ? rawBuffer_ : segmentedHeader_);
            break;
        case InlinePreeditMode::CandidatePreview:
        default:
            res.preeditText = committedPrefixWords_ + highlightedCandidateText();
            break;
    }
    return res;
}

KeyActionResult BilingualEngine::nextPage() {
    if (!isComposing()) return {};
    int tp = totalPages();
    if (currentPage_ + 1 < tp) {
        ++currentPage_;
        selectedInPage_ = 0;
    }
    return buildComposeResult();
}

KeyActionResult BilingualEngine::prevPage() {
    if (!isComposing()) return {};
    if (currentPage_ > 0) {
        --currentPage_;
        selectedInPage_ = 0;
    }
    return buildComposeResult();
}

KeyActionResult BilingualEngine::selectCandidateOnPage(int indexInPage) {
    int ps = std::clamp(config_.pageSize, 3, 9);
    size_t globalIdx = static_cast<size_t>(currentPage_) * ps + static_cast<size_t>(indexInPage);
    if (indexInPage < 0 || indexInPage >= ps || globalIdx >= candidates_.size()) {
        return {};
    }
    return commitCandidateAtGlobalIndex(globalIdx);
}

bool BilingualEngine::pinCandidateOnPage(int indexInPage, int slot0To4) {
    int ps = std::clamp(config_.pageSize, 3, 9);
    size_t globalIdx = static_cast<size_t>(currentPage_) * ps + static_cast<size_t>(indexInPage);
    if (rawBuffer_.empty() || indexInPage < 0 || indexInPage >= ps || globalIdx >= candidates_.size()) {
        return false;
    }
    const auto& item = candidates_[globalIdx];
    if (item.kind == CandidateKind::QuickInput || item.kind == CandidateKind::QuickInputSetup ||
        item.kind == CandidateKind::DynamicShortcut || item.kind == CandidateKind::RawInput) {
        return false;
    }
    userDict_.setPin(rawBuffer_, slot0To4, item.text);
    userDict_.savePins();
    rebuildCandidates();
    return true;
}

bool BilingualEngine::unpinCandidateOnPage(int indexInPage) {
    int ps = std::clamp(config_.pageSize, 3, 9);
    size_t globalIdx = static_cast<size_t>(currentPage_) * ps + static_cast<size_t>(indexInPage);
    if (rawBuffer_.empty() || indexInPage < 0 || indexInPage >= ps || globalIdx >= candidates_.size()) {
        return false;
    }
    if (!userDict_.unpin(rawBuffer_, candidates_[globalIdx].text)) {
        return false;
    }
    userDict_.savePins();
    rebuildCandidates();
    return true;
}

bool BilingualEngine::removeUserWordOnPage(int indexInPage) {
    int ps = std::clamp(config_.pageSize, 3, 9);
    size_t globalIdx = static_cast<size_t>(currentPage_) * ps + static_cast<size_t>(indexInPage);
    if (indexInPage < 0 || indexInPage >= ps || globalIdx >= candidates_.size()) {
        return false;
    }
    const CandidateItem item = candidates_[globalIdx];
    if (item.kind == CandidateKind::QuickInput || item.kind == CandidateKind::QuickInputSetup ||
        item.kind == CandidateKind::DynamicShortcut || item.kind == CandidateKind::RawInput) {
        return false;
    }

    const std::string chosenCommit = item.commitText.empty() ? item.text : item.commitText;
    bool changed = false;
    if (!item.matchedKey.empty()) {
        changed |= userDict_.removeEntry(item.matchedKey, chosenCommit);
        if (item.text != chosenCommit) {
            changed |= userDict_.removeEntry(item.matchedKey, item.text);
        }
    }
    if (!rawBuffer_.empty()) {
        const std::string normRaw = UserDict::normalizePinKey(rawBuffer_);
        if (!normRaw.empty() && normRaw != item.matchedKey) {
            changed |= userDict_.removeEntry(normRaw, chosenCommit);
            if (item.text != chosenCommit) {
                changed |= userDict_.removeEntry(normRaw, item.text);
            }
        }
        if (userDict_.unpin(rawBuffer_, item.text)) {
            userDict_.savePins();
            changed = true;
        }
    }
    if (!changed) return false;

    const int prevPage = currentPage_;
    rebuildCandidates();
    const int tp = totalPages();
    if (tp > 0) {
        currentPage_ = std::clamp(prevPage, 0, tp - 1);
        auto page = currentPageItems();
        if (selectedInPage_ >= static_cast<int>(page.size())) {
            selectedInPage_ = page.empty() ? 0 : static_cast<int>(page.size()) - 1;
        }
    }
    return true;
}

void BilingualEngine::flushPendingLearnedPhrase() {
    if (!committedPrefixWords_.empty() && !committedPrefixPinyin_.empty()) {
        // Trim trailing space on pinyin key
        std::string key = committedPrefixPinyin_;
        while (!key.empty() && key.back() == ' ') key.pop_back();
        if (key.find(' ') != std::string::npos) {
            userDict_.recordSelection(key, committedPrefixWords_);
        }
    }
}

void BilingualEngine::undoLastPrefixStep() {
    if (prefixSteps_.empty()) return;
    const PrefixStep step = std::move(prefixSteps_.back());
    prefixSteps_.pop_back();

    if (committedPrefixWords_.size() >= step.word.size()) {
        committedPrefixWords_.resize(committedPrefixWords_.size() - step.word.size());
    } else {
        committedPrefixWords_.clear();
    }

    if (!step.pinyinKey.empty()) {
        if (committedPrefixPinyin_.size() > step.pinyinKey.size() &&
            committedPrefixPinyin_.compare(committedPrefixPinyin_.size() - step.pinyinKey.size(),
                                           step.pinyinKey.size(), step.pinyinKey) == 0) {
            committedPrefixPinyin_.resize(committedPrefixPinyin_.size() - step.pinyinKey.size());
            if (!committedPrefixPinyin_.empty() && committedPrefixPinyin_.back() == ' ') {
                committedPrefixPinyin_.pop_back();
            }
        } else if (committedPrefixPinyin_ == step.pinyinKey) {
            committedPrefixPinyin_.clear();
        }
    }
    rawBuffer_ = step.rawConsumed;
}

KeyActionResult BilingualEngine::finalizeCommit(const std::string& text, bool resetModeFlags, bool isQuickInput) {
    KeyActionResult res;
    res.consumed = true;
    res.isQuickInput = isQuickInput;
    res.commitText = (config_.fullWidth && !isQuickInput) ? toFullWidthUtf8(text) : text;
    lastCommittedQuoteKind_ = 0;
    if (!text.empty()) {
        char last = text.back();
        lastCommittedChar_ = (static_cast<unsigned char>(last) < 0x80) ? last : '\0';
    }
    if (resetModeFlags) {
        resetComposition();
    }
    res.showCandidates = false;
    return res;
}

KeyActionResult BilingualEngine::commitCandidateAtGlobalIndex(size_t idx) {
    if (idx >= candidates_.size()) {
        return finalizeCommit(committedPrefixWords_ + rawBuffer_);
    }

    const CandidateItem chosen = candidates_[idx];
    const bool isQuick = (chosen.kind == CandidateKind::QuickInput);

    if (chosen.kind == CandidateKind::QuickInputSetup) {
        resetComposition();
        KeyActionResult res;
        res.consumed = true;
        res.showCandidates = false;
        res.openQuickInputDialog = true;
        return res;
    }

    const std::string chosenCommit = chosen.commitText.empty() ? chosen.text : chosen.commitText;
    const bool allowLearn = canLearnCandidateKind(chosen.kind) && !chosen.matchedKey.empty();

    // If user chose an English word, dynamic shortcut, quick input, or full match
    if (chosen.consumedRawChars >= rawBuffer_.size() || chosen.consumedRawChars == 0) {
        if (allowLearn) {
            userDict_.recordSelection(chosen.matchedKey, chosenCommit);
            if (!committedPrefixWords_.empty()) {
                committedPrefixWords_ += chosenCommit;
                if (!committedPrefixPinyin_.empty() && committedPrefixPinyin_.back() != ' ')
                    committedPrefixPinyin_.push_back(' ');
                committedPrefixPinyin_ += chosen.matchedKey;
                flushPendingLearnedPhrase();
                std::string full = committedPrefixWords_;
                return finalizeCommit(full, true, isQuick);
            }
        }
        std::string full = committedPrefixWords_ + chosenCommit;
        return finalizeCommit(full, true, isQuick);
    }

    // Partial selection (e.g. selected "我想" out of "woxiangkan"): keep remaining rawBuffer_!
    size_t eraseLen = chosen.consumedRawChars;
    while (eraseLen < rawBuffer_.size() && rawBuffer_[eraseLen] == '\'') {
        ++eraseLen;
    }
    prefixSteps_.push_back(PrefixStep{
        chosenCommit,
        allowLearn ? chosen.matchedKey : std::string{},
        rawBuffer_.substr(0, eraseLen)
    });
    committedPrefixWords_ += chosenCommit;
    if (allowLearn) {
        if (!committedPrefixPinyin_.empty() && committedPrefixPinyin_.back() != ' ')
            committedPrefixPinyin_.push_back(' ');
        committedPrefixPinyin_ += chosen.matchedKey;
    }
    rawBuffer_.erase(0, eraseLen);

    if (rawBuffer_.empty()) {
        flushPendingLearnedPhrase();
        std::string full = committedPrefixWords_;
        return finalizeCommit(full);
    }

    rebuildCandidates();
    return buildComposeResult();
}

std::string BilingualEngine::mapPunctuation(uint32_t vk, bool shift) const {
    if (!config_.chineseMode || !config_.chinesePunct) {
        return {};
    }

    switch (vk) {
        case kVkOemComma:  return shift ? "《" : "，";
        case kVkOemPeriod:
            if (shift) return "》";
            if (config_.smartDecimal && lastCommittedChar_ >= '0' && lastCommittedChar_ <= '9') {
                return ".";
            }
            return "。";
        case kVkOem1:      return shift ? "：" : "；";
        case kVkOem2:      return shift ? "？" : "/";
        case kVkOem5:      return shift ? "|" : "、";
        case kVkOem4:      return shift ? "{" : "【";
        case kVkOem6:      return shift ? "}" : "】";
        case kVkOemMinus:  return shift ? "——" : "-";
        case kVkOem3:      return shift ? "~" : "·";
        case kVkOem7:
            if (shift) {
                return doubleQuoteOpen_ ? "“" : "”";
            }
            return singleQuoteOpen_ ? "‘" : "’";
        case '1': return shift ? "！" : "";
        case '4': return shift ? "￥" : "";
        case '6': return shift ? "……" : "";
        case '9': return shift ? "（" : "";
        case '0': return shift ? "）" : "";
        default:  return {};
    }
}

KeyActionResult BilingualEngine::handleKey(uint32_t vk, uint32_t modifiers, bool isKeyUp, bool testOnly) {
    KeyActionResult res;
    const bool shift = (modifiers & ipc::ModShift) != 0;
    const bool ctrl = (modifiers & ipc::ModCtrl) != 0;
    const bool alt = (modifiers & ipc::ModAlt) != 0;
    const bool caps = (modifiers & ipc::ModCaps) != 0;

    // 1. Shift tap detection for seamless CN <-> EN toggle
    if (isShiftVk(vk)) {
        if (!isKeyUp) {
            if (!ctrl && !alt && !testOnly) {
                shiftDownOnly_ = true;
            }
            return res; // Do not consume Shift itself
        } else {
            if (shiftDownOnly_ && config_.shiftTapToggle && !ctrl && !alt) {
                if (!testOnly) {
                    shiftDownOnly_ = false;
                    config_.chineseMode = !config_.chineseMode;
                    if (isComposing()) {
                        std::string raw = committedPrefixWords_ + rawBuffer_;
                        return finalizeCommit(raw);
                    }
                }
            }
            if (!testOnly) shiftDownOnly_ = false;
            return res;
        }
    }

    if (!isKeyUp) {
        // Any non-Shift keydown (even during TSF OnTestKeyDown) cancels a pure Shift tap
        shiftDownOnly_ = false;
    } else {
        // Keep candidate UI alive when host forwards non-Shift KeyUp events while composing
        if (isComposing() && !candidates_.empty()) {
            res = buildComposeResult();
            res.consumed = false;
        }
        return res;
    }

    // Ctrl+Delete / Ctrl+Shift+1..9 while composing: remove/reset mislearned user entry or pin
    if (isComposing() && ctrl && !alt) {
        if (!shift && vk == kVkDelete) {
            if (testOnly) { res.consumed = true; return res; }
            removeUserWordOnPage(selectedInPage_);
            return buildComposeResult();
        }
        if (shift && vk >= '1' && vk <= '9') {
            int idx = static_cast<int>(vk - '1');
            int ps = std::clamp(config_.pageSize, 3, 9);
            if (idx < ps && static_cast<size_t>(currentPage_) * ps + idx < candidates_.size()) {
                if (testOnly) { res.consumed = true; return res; }
                removeUserWordOnPage(idx);
                return buildComposeResult();
            }
        }
    }

    // Never intercept Ctrl+... or Alt+... shortcuts
    if (ctrl || alt) {
        return res;
    }

    // 2. English mode OR Caps Lock active when idle -> convert to full-width if enabled, else pass-through
    if (!config_.chineseMode || (caps && !isComposing())) {
        if (config_.fullWidth) {
            char ascii = mapAsciiPrintableVk(vk, shift, caps);
            if (ascii != '\0') {
                if (testOnly) {
                    res.consumed = true;
                    return res;
                }
                std::string s(1, ascii);
                return finalizeCommit(s, false);
            }
        }
        if (!testOnly) {
            char d = digitFromVk(vk, shift);
            lastCommittedChar_ = (d != '\0') ? d : '\0';
            lastCommittedQuoteKind_ = 0;
        }
        return res;
    }

    // 3. When actively composing
    if (isComposing()) {
        // Letters A-Z: Caps Lock takes priority and commits buffered raw text + typed letter immediately
        if (vk >= 'A' && vk <= 'Z') {
            if (testOnly) { res.consumed = true; return res; }
            if (caps) {
                char ch = static_cast<char>(shift ? (vk - 'A' + 'a') : vk);
                return finalizeCommit(committedPrefixWords_ + rawBuffer_ + ch);
            }
            char ch = static_cast<char>(shift ? vk : (vk - 'A' + 'a'));
            const size_t maxBuf = urlMode_ ? 128 : 64;
            if (rawBuffer_.size() < maxBuf) {
                rawBuffer_.push_back(ch);
                if (config_.autoUrlEnglish && isUrlTrigger(detail::toLowerAscii(rawBuffer_))) {
                    urlMode_ = true;
                }
                rebuildCandidates();
            }
            return buildComposeResult();
        }

        // Trigger URL/email mode cleanly when '.' completes "www." or '@' follows a local-part prefix
        if (!urlMode_ && config_.autoUrlEnglish && !rawBuffer_.empty()) {
            if (!shift && vk == kVkOemPeriod &&
                isUrlTrigger(detail::toLowerAscii(rawBuffer_) + '.')) {
                if (testOnly) { res.consumed = true; return res; }
                urlMode_ = true;
                rawBuffer_.push_back('.');
                rebuildCandidates();
                return buildComposeResult();
            }
            if (shift && vk == '2') { // '@'
                if (testOnly) { res.consumed = true; return res; }
                urlMode_ = true;
                rawBuffer_.push_back('@');
                rebuildCandidates();
                return buildComposeResult();
            }
        }

        // Apostrophe separator (e.g. xi'an, dan'g) or URL chars in urlMode_
        if (vk == kVkOem7 && !shift) {
            if (testOnly) { res.consumed = true; return res; }
            if (!rawBuffer_.empty() && rawBuffer_.back() != '\'' && rawBuffer_.size() < 64) {
                rawBuffer_.push_back('\'');
                rebuildCandidates();
            }
            return buildComposeResult();
        }

        if (urlMode_ && rawBuffer_.size() < 128) {
            char urlCh = '\0';
            char npDigit = digitFromVk(vk, shift);
            if (npDigit != '\0') {
                urlCh = npDigit;
            } else if (!shift) {
                if (vk == kVkOemPeriod) urlCh = '.';
                else if (vk == kVkOem2) urlCh = '/';
                else if (vk == kVkOemMinus) urlCh = '-';
                else if (vk == kVkOemPlus) urlCh = '=';
            } else {
                if (vk == '2') urlCh = '@';
                else if (vk == kVkOem1) urlCh = ':';
                else if (vk == kVkOemMinus) urlCh = '_';
                else if (vk == kVkOem2) urlCh = '?';
                else if (vk == '7') urlCh = '&';
            }
            if (urlCh != '\0') {
                if (testOnly) { res.consumed = true; return res; }
                rawBuffer_.push_back(urlCh);
                rebuildCandidates();
                return buildComposeResult();
            }
        }

        // Backspace
        if (vk == kVkBack) {
            if (testOnly) { res.consumed = true; return res; }
            if (!rawBuffer_.empty()) {
                rawBuffer_.pop_back();
                if (!isUrlTrigger(detail::toLowerAscii(rawBuffer_))) urlMode_ = false;
                if (rawBuffer_.empty() && !prefixSteps_.empty()) {
                    undoLastPrefixStep();
                }
            } else if (!prefixSteps_.empty()) {
                undoLastPrefixStep();
            } else if (!committedPrefixWords_.empty()) {
                committedPrefixWords_.clear();
                committedPrefixPinyin_.clear();
            }
            if (!isComposing()) {
                res.consumed = true;
                res.showCandidates = false;
                return res;
            }
            rebuildCandidates();
            return buildComposeResult();
        }

        // Tab / Shift+Tab while composing: cycle pages (or candidate selection on single page)
        if (vk == kVkTab) {
            if (testOnly) { res.consumed = true; return res; }
            const int tp = totalPages();
            if (tp > 1) {
                if (shift) {
                    if (currentPage_ > 0) return prevPage();
                    currentPage_ = tp - 1;
                    selectedInPage_ = 0;
                    return buildComposeResult();
                }
                if (currentPage_ + 1 < tp) return nextPage();
                currentPage_ = 0;
                selectedInPage_ = 0;
                return buildComposeResult();
            }
            auto page = currentPageItems();
            if (!page.empty()) {
                if (shift) {
                    selectedInPage_ = (selectedInPage_ > 0) ? (selectedInPage_ - 1) : (static_cast<int>(page.size()) - 1);
                } else {
                    selectedInPage_ = (selectedInPage_ + 1 < static_cast<int>(page.size())) ? (selectedInPage_ + 1) : 0;
                }
            }
            return buildComposeResult();
        }

        // Escape clears composition
        if (vk == kVkEscape) {
            if (testOnly) { res.consumed = true; return res; }
            resetComposition();
            res.consumed = true;
            res.showCandidates = false;
            return res;
        }

        // Enter commits raw English/ASCII buffer directly (Sogou signature feature!)
        if (vk == kVkReturn) {
            if (testOnly) { res.consumed = true; return res; }
            std::string raw = committedPrefixWords_ + rawBuffer_;
            if (raw.size() > 1 && raw[0] == 'v' && !urlMode_) {
                raw.erase(0, 1);
            }
            return finalizeCommit(raw);
        }

        // Space commits highlighted candidate on current page
        if (vk == kVkSpace) {
            if (testOnly) { res.consumed = true; return res; }
            return selectCandidateOnPage(selectedInPage_);
        }

        // Number keys 1..9 select candidate on current page
        if (!shift && vk >= '1' && vk <= '9') {
            int idx = static_cast<int>(vk - '1');
            int ps = std::clamp(config_.pageSize, 3, 9);
            if (idx < ps && static_cast<size_t>(currentPage_) * ps + idx < candidates_.size()) {
                if (testOnly) { res.consumed = true; return res; }
                return selectCandidateOnPage(idx);
            }
        }

        // Page navigation: = / . / ] / PageDown (next), - / , / [ / PageUp (prev)
        if (vk == kVkOemPlus || vk == kVkNext || (!shift && (vk == kVkOemPeriod || vk == kVkOem6))) {
            if (testOnly) { res.consumed = true; return res; }
            return nextPage();
        }
        if (vk == kVkOemMinus || vk == kVkPrior || (!shift && (vk == kVkOemComma || vk == kVkOem4))) {
            if (testOnly) { res.consumed = true; return res; }
            return prevPage();
        }

        // Arrow navigation among candidates on page
        if (vk == kVkRight || vk == kVkDown) {
            if (testOnly) { res.consumed = true; return res; }
            auto page = currentPageItems();
            if (selectedInPage_ + 1 < static_cast<int>(page.size())) {
                ++selectedInPage_;
                return buildComposeResult();
            }
            return nextPage();
        }
        if (vk == kVkLeft || vk == kVkUp) {
            if (testOnly) { res.consumed = true; return res; }
            if (selectedInPage_ > 0) {
                --selectedInPage_;
                return buildComposeResult();
            }
            return prevPage();
        }

        // Punctuation while composing (skipped when Caps Lock active so English punctuation wins)
        std::string punct = caps ? std::string{} : mapPunctuation(vk, shift);
        if (!punct.empty()) {
            if (testOnly) { res.consumed = true; return res; }
            if (vk == kVkOem7) {
                if (shift) doubleQuoteOpen_ = !doubleQuoteOpen_;
                else singleQuoteOpen_ = !singleQuoteOpen_;
            }
            KeyActionResult committed = selectCandidateOnPage(selectedInPage_);
            if (committed.openQuickInputDialog) {
                return committed;
            }
            committed.commitText += punct;
            lastCommittedChar_ = (punct.size() == 1) ? punct[0] : '\0';
            lastCommittedQuoteKind_ = (vk == kVkOem7) ? (shift ? 1 : 2) : 0;
            return committed;
        }

        // Any other unhandled key while composing: pass through
        return res;
    }

    // 4. Not currently composing (idle in Chinese mode)
    if (!shift && vk >= 'A' && vk <= 'Z') {
        if (testOnly) { res.consumed = true; return res; }
        lastCommittedQuoteKind_ = 0;
        char ch = static_cast<char>(vk - 'A' + 'a');
        rawBuffer_.push_back(ch);
        rebuildCandidates();
        return buildComposeResult();
    }

    // Track digits (including numpad 0..9) so `3.14` outputs half-width '.'
    char idleDigit = digitFromVk(vk, shift);
    if (idleDigit != '\0') {
        if (config_.fullWidth) {
            if (testOnly) { res.consumed = true; return res; }
            std::string s(1, idleDigit);
            return finalizeCommit(s, false);
        }
        if (!testOnly) {
            lastCommittedChar_ = idleDigit;
            lastCommittedQuoteKind_ = 0;
        }
        return res;
    }

    // Chinese punctuation when idle
    std::string punct = mapPunctuation(vk, shift);
    if (!punct.empty()) {
        if (testOnly) { res.consumed = true; return res; }
        if (vk == kVkOem7) {
            if (shift) doubleQuoteOpen_ = !doubleQuoteOpen_;
            else singleQuoteOpen_ = !singleQuoteOpen_;
            KeyActionResult out = finalizeCommit(punct, false);
            lastCommittedQuoteKind_ = shift ? 1 : 2;
            return out;
        }
        return finalizeCommit(punct, false);
    }

    if (config_.fullWidth) {
        char ascii = mapAsciiPrintableVk(vk, shift, caps);
        if (ascii != '\0') {
            if (testOnly) { res.consumed = true; return res; }
            std::string s(1, ascii);
            return finalizeCommit(s, false);
        }
    }

    if (!testOnly) {
        if (vk == kVkBack) {
            if (lastCommittedQuoteKind_ == 1) doubleQuoteOpen_ = !doubleQuoteOpen_;
            else if (lastCommittedQuoteKind_ == 2) singleQuoteOpen_ = !singleQuoteOpen_;
        }
        lastCommittedQuoteKind_ = 0;
        lastCommittedChar_ = '\0';
    }
    return res;
}

} // namespace liteime::engine
