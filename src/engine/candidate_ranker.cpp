#include "engine/engine_internal.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <unordered_map>

namespace liteime::engine {

void BilingualEngine::rebuildCandidates() {
    candidates_.clear();
    segmentedHeader_ = rawBuffer_;
    currentPage_ = 0;
    selectedInPage_ = 0;

    if (rawBuffer_.empty()) return;

    std::string lowerBuf = detail::toLowerAscii(rawBuffer_);

    // 0. "vvv" QuickInput trigger -> masked personal snippets + manage entry
    if (!urlMode_ && lowerBuf == "vvv") {
        segmentedHeader_ = "vvv [快输入]";
        const auto& qItems = userDict_.quickInputs();
        for (size_t i = 0; i < qItems.size(); ++i) {
            const auto& q = qItems[i];
            std::string masked = UserDict::maskQuickValue(q.value);
            std::string display = q.label.empty() ? masked : (q.label + ": " + masked);
            CandidateItem ci;
            ci.text = std::move(display);
            ci.comment = "[快输入]";
            ci.commitText = q.value;
            ci.consumedRawChars = rawBuffer_.size();
            ci.score = 1000.0 - static_cast<double>(i);
            ci.kind = CandidateKind::QuickInput;
            candidates_.push_back(std::move(ci));
        }
        CandidateItem setup;
        setup.text = "⚙ 管理快输入内容...";
        setup.comment = qItems.empty() ? "[未配置·注意隐私]" : "[隐私提醒]";
        setup.consumedRawChars = rawBuffer_.size();
        setup.score = 10.0;
        setup.kind = CandidateKind::QuickInputSetup;
        candidates_.push_back(std::move(setup));
        return;
    }

    // 1. Temporary v-mode or URL mode -> direct raw ASCII candidate
    if (urlMode_ || (rawBuffer_.size() > 1 && rawBuffer_[0] == 'v')) {
        std::string payload = (rawBuffer_[0] == 'v' && !urlMode_) ? rawBuffer_.substr(1) : rawBuffer_;
        CandidateItem rawCi;
        rawCi.text = payload;
        rawCi.comment = urlMode_ ? "[URL]" : "[v模式]";
        rawCi.consumedRawChars = rawBuffer_.size();
        rawCi.score = 1000.0;
        rawCi.kind = CandidateKind::RawInput;
        candidates_.push_back(std::move(rawCi));
        if (hasAnyDict() && !urlMode_) {
            auto enHits = queryEnglishPrefixAll(detail::toLowerAscii(payload), 6);
            for (const auto& e : enHits) {
                if (e.word == payload) continue;
                CandidateItem enCi;
                enCi.text = e.word;
                enCi.comment = "[EN]";
                enCi.consumedRawChars = rawBuffer_.size();
                enCi.score = 900.0;
                enCi.kind = CandidateKind::EnglishWord;
                candidates_.push_back(std::move(enCi));
            }
        }
        return;
    }

    struct ScoredCandidate {
        CandidateItem item;
        int groupRank = 0; // higher = shown earlier
        uint32_t tier = 0; // pure user-frequency ladder tier (0, 1, 2...)
    };
    std::vector<ScoredCandidate> scored;
    std::unordered_map<std::string, size_t> seenIdx;

    auto addScored = [&](std::string text,
                         std::string comment,
                         std::string matchedKey,
                         size_t consumedRawChars,
                         double defaultScore,
                         CandidateKind kind,
                         int groupRank,
                         uint32_t tier,
                         int pinnedSlot = -1) {
        if (text.empty()) return;
        auto it = seenIdx.find(text);
        if (it != seenIdx.end()) {
            auto& cur = scored[it->second];
            if (groupRank > cur.groupRank || (groupRank == cur.groupRank && tier > cur.tier)) {
                cur.item.comment = std::move(comment);
                cur.item.matchedKey = std::move(matchedKey);
                cur.item.consumedRawChars = consumedRawChars;
                cur.item.score = defaultScore;
                cur.item.kind = kind;
                if (pinnedSlot >= 0) cur.item.pinnedSlot = pinnedSlot;
                cur.groupRank = groupRank;
                cur.tier = tier;
            }
            return;
        }
        seenIdx.emplace(text, scored.size());
        CandidateItem ci;
        ci.text = std::move(text);
        ci.comment = std::move(comment);
        ci.matchedKey = std::move(matchedKey);
        ci.consumedRawChars = consumedRawChars;
        ci.score = defaultScore;
        ci.kind = kind;
        ci.pinnedSlot = pinnedSlot;
        scored.push_back(ScoredCandidate{std::move(ci), groupRank, tier});
    };

    // 2. Dynamic shortcuts (rq / sj / xq)
    if (lowerBuf == "rq" || lowerBuf == "sj" || lowerBuf == "xq") {
        std::time_t now = std::time(nullptr);
        std::tm localTm{};
#ifdef _WIN32
        localtime_s(&localTm, &now);
#else
        localtime_r(&now, &localTm);
#endif
        char buf[64]{};
        if (lowerBuf == "rq") {
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                          localTm.tm_year + 1900, localTm.tm_mon + 1, localTm.tm_mday);
            addScored(buf, "[日期]", "", rawBuffer_.size(), 150.0, CandidateKind::DynamicShortcut, 100, 0);
            std::snprintf(buf, sizeof(buf), "%04d年%d月%d日",
                          localTm.tm_year + 1900, localTm.tm_mon + 1, localTm.tm_mday);
            addScored(buf, "[日期]", "", rawBuffer_.size(), 145.0, CandidateKind::DynamicShortcut, 100, 0);
        } else if (lowerBuf == "sj") {
            std::snprintf(buf, sizeof(buf), "%02d:%02d", localTm.tm_hour, localTm.tm_min);
            addScored(buf, "[时间]", "", rawBuffer_.size(), 150.0, CandidateKind::DynamicShortcut, 100, 0);
        } else if (lowerBuf == "xq") {
            static constexpr const char* kWeekdays[] = {
                "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"
            };
            addScored(kWeekdays[localTm.tm_wday % 7], "[星期]", "", rawBuffer_.size(), 150.0,
                      CandidateKind::DynamicShortcut, 100, 0);
        }
    }

    // 3. Pinyin segmentation & Chinese candidates
    auto paths = PinyinTable::segment(lowerBuf, 3);
    bool hasFullExactChinese = false;
    bool hasFuzzyMatches = false;

    if (!paths.empty() && hasAnyDict()) {
        auto buildHeaderForPath = [this](const SegmentPath& path) {
            std::string hdr;
            for (size_t i = 0; i < path.tokens.size(); ++i) {
                const auto& tok = path.tokens[i];
                if (tok.start + tok.length <= rawBuffer_.size()) {
                    if (i > 0) hdr.push_back('\'');
                    hdr.append(rawBuffer_, tok.start, tok.length);
                } else {
                    return std::string{};
                }
            }
            auto stripApos = [](std::string_view s) {
                std::string out;
                out.reserve(s.size());
                for (char c : s) {
                    if (c != '\'') out.push_back(c);
                }
                return out;
            };
            if (!hdr.empty() && stripApos(hdr) == stripApos(rawBuffer_)) {
                return hdr;
            }
            return std::string{};
        };
        const SegmentPath& bestPath = paths[0];
        const bool hasUpper = std::any_of(rawBuffer_.begin(), rawBuffer_.end(),
                                          [](unsigned char c) { return c >= 'A' && c <= 'Z'; });
        if (!hasUpper) {
            std::string hdr = buildHeaderForPath(bestPath);
            if (!hdr.empty()) {
                segmentedHeader_ = std::move(hdr);
            }
        }
        const size_t tokCount = bestPath.tokens.size();
        const std::string fullKey = bestPath.joinedKey();

        // 3a & 3b. Exact full-input Chinese phrases across top segmentation paths (groupRank = 92)
        // When alternative paths have no partial tokens and the same syllable count as paths[0]
        // (e.g. fangan: "fan'gan" vs "fang'an"), use a light tie-break penalty so corpus frequency decides.
        double bestExactPhraseScore = -1e18;
        size_t bestExactPhrasePathIdx = 0;
        for (size_t pIdx = 0; pIdx < paths.size(); ++pIdx) {
            const auto& path = paths[pIdx];
            if (std::any_of(path.tokens.begin(), path.tokens.end(), [](const SyllableToken& t) {
                    return t.isPartial;
                })) {
                continue;
            }
            const double pathPenalty = (pIdx > 0 && path.tokens.size() == tokCount)
                ? static_cast<double>(pIdx) * 2.5
                : static_cast<double>(pIdx) * 15.0;
            const std::string key = path.joinedKey();
            const size_t exactLimit = (path.tokens.size() == 1) ? 120 : 24;
            auto exactHits = queryExactAll(key, exactLimit);
            for (const auto& h : exactHits) {
                hasFullExactChinese = true;
                double sc = 360.0 + (detail::freqToLogScore(h.freq) + 10.0) * 5.0 - pathPenalty;
                uint32_t tier = userDict_.getTier(key, h.word);
                double effectiveScore = sc + static_cast<double>(tier) * 1000.0;
                if (effectiveScore > bestExactPhraseScore) {
                    bestExactPhraseScore = effectiveScore;
                    bestExactPhrasePathIdx = pIdx;
                }
                addScored(h.word, "", key, rawBuffer_.size(), sc, CandidateKind::ChinesePhrase, 92, tier);
            }
            // Also include any user-learned / promoted word for this exact full key not already in top exactHits
            for (const auto& u : userDict_.lookup(key)) {
                uint32_t tier = UserDict::tierForCount(u.count);
                if (tier >= 1 || u.word.size() > 3) {
                    hasFullExactChinese = true;
                    double sc = 330.0 - pathPenalty;
                    double effectiveScore = sc + static_cast<double>(tier) * 1000.0;
                    if (effectiveScore > bestExactPhraseScore) {
                        bestExactPhraseScore = effectiveScore;
                        bestExactPhrasePathIdx = pIdx;
                    }
                    addScored(u.word, "", key, rawBuffer_.size(), sc, CandidateKind::ChinesePhrase, 92, tier);
                }
            }
        }
        if (!hasUpper && bestExactPhrasePathIdx > 0 && bestExactPhrasePathIdx < paths.size()) {
            std::string altHdr = buildHeaderForPath(paths[bestExactPhrasePathIdx]);
            if (!altHdr.empty()) {
                segmentedHeader_ = std::move(altHdr);
            }
        }

        // 3c. Whole-sentence Viterbi candidate when multi-syllable (all non-final tokens full syllables; groupRank = 90)
        const bool isInProgressMultiSyllable = (tokCount >= 2) &&
            std::all_of(bestPath.tokens.begin(), bestPath.tokens.end() - 1,
                        [](const SyllableToken& t) { return !t.isPartial; }) &&
            bestPath.tokens.back().isPartial &&
            PinyinTable::isSyllablePrefix(bestPath.tokens.back().text);
        if (tokCount >= 2 &&
            std::all_of(bestPath.tokens.begin(), bestPath.tokens.end() - 1,
                        [](const SyllableToken& t) { return !t.isPartial; })) {
            double vScore = 0.0;
            std::string sent = buildWholeSentence(bestPath, vScore);
            if (!sent.empty()) {
                uint32_t tier = userDict_.getTier(fullKey, sent);
                addScored(sent, "", fullKey, rawBuffer_.size(), 340.0 + vScore,
                          CandidateKind::ChineseSentence, 90, tier);
            }
        }

        // 3c-prefix. Multi-syllable words matching "<full syllables> <last initial/prefix>*" (e.g. "qingn" -> 青年/请你/请您)
        if (isInProgressMultiSyllable) {
            const std::string initials = bestPath.initialsKey();
            auto addMatchedMulti = [&](const DictCandidate& h) {
                if (detail::countSyllablesInPinyin(h.pinyin) != tokCount) return;
                if (!std::string_view(h.pinyin).starts_with(fullKey)) return;
                uint32_t tier = userDict_.getTier(h.pinyin, h.word);
                double sc = 280.0 + (detail::freqToLogScore(h.freq) + 10.0) * 5.0;
                addScored(h.word, "", h.pinyin, rawBuffer_.size(), sc, CandidateKind::ChinesePhrase, 89, tier);
            };
            for (const auto& h : queryJianpinAll(initials, 64)) {
                addMatchedMulti(h);
            }
            for (const auto& h : queryPinyinPrefixAll(fullKey, 32)) {
                addMatchedMulti(h);
            }
            for (const auto& [uKey, uEntry] : userDict_.lookupByInitials(initials)) {
                if (detail::countSyllablesInPinyin(uKey) != tokCount) continue;
                if (!std::string_view(uKey).starts_with(fullKey)) continue;
                uint32_t tier = UserDict::tierForCount(uEntry.count);
                double sc = 280.0 + static_cast<double>(uEntry.count) * 4.0;
                addScored(uEntry.word, "", uKey, rawBuffer_.size(), sc, CandidateKind::ChinesePhrase, 89, tier);
            }
        }

        // 3d. True Jianpin lookup (groupRank = 88) vs Loose high-freq initial (groupRank = 66)
        if (tokCount >= 2) {
            const std::string initials = bestPath.initialsKey();
            const bool anyPartial = std::any_of(bestPath.tokens.begin(), bestPath.tokens.end(),
                                                [](const SyllableToken& t) { return t.isPartial; });
            const bool allShort = std::all_of(bestPath.tokens.begin(), bestPath.tokens.end(),
                                              [](const SyllableToken& t) { return t.text.size() <= 2; });
            const bool isTrueJianpin = (initials.size() >= 2 && anyPartial && allShort);

            if (isTrueJianpin) {
                auto jpHits = queryJianpinAll(initials, 12);
                for (const auto& h : jpHits) {
                    if (detail::countSyllablesInPinyin(h.pinyin) > tokCount) continue;
                    uint32_t tier = userDict_.getTier(h.pinyin, h.word);
                    double sc = 240.0 + (detail::freqToLogScore(h.freq) + 10.0) * 5.0;
                    addScored(h.word, "", h.pinyin, rawBuffer_.size(), sc, CandidateKind::ChineseJianpin, 88, tier);
                }
                for (const auto& [uKey, uEntry] : userDict_.lookupByInitials(initials)) {
                    if (detail::countSyllablesInPinyin(uKey) > tokCount) continue;
                    uint32_t tier = UserDict::tierForCount(uEntry.count);
                    double sc = 240.0 + static_cast<double>(uEntry.count) * 4.0;
                    addScored(uEntry.word, "", uKey, rawBuffer_.size(), sc, CandidateKind::ChineseJianpin, 88, tier);
                }
            } else if (config_.looseInitialMatch && !anyPartial && initials.size() >= 2) {
                // Optional extension (OFF by default): at most 2 ultra-high-frequency loose-initial
                // words (e.g. wulu -> wl -> 未来 (weilai)), excluding exact-pinyin duplicates & rare words.
                constexpr uint32_t kLooseInitialMinFreq = 40000;
                auto jpHits = queryJianpinAll(initials, 20);
                size_t looseAdded = 0;
                for (const auto& h : jpHits) {
                    if (h.freq < kLooseInitialMinFreq) continue;
                    if (h.pinyin == fullKey) continue;
                    if (detail::countSyllablesInPinyin(h.pinyin) > tokCount) continue;
                    std::string hint = "(" + detail::formatPinyinConcat(h.pinyin) + ")";
                    double sc = 240.0 + (detail::freqToLogScore(h.freq) + 10.0) * 5.0;
                    addScored(h.word, std::move(hint), "", rawBuffer_.size(), sc, CandidateKind::LooseInitial, 66, 0);
                    if (++looseAdded >= 2) break;
                }
            }
        }

        // 3d-fuzzy. Fuzzy Pinyin expansion (single & multi-syllable), strictly after exact & never learned (groupRank = 75)
        for (size_t pIdx = 0; pIdx < std::min<size_t>(paths.size(), 2); ++pIdx) {
            const auto& path = paths[pIdx];
            if (path.tokens.size() == 1 && !path.tokens[0].isPartial) {
                auto fuzzies = PinyinTable::expandFuzzy(path.tokens[0].text, config_.fuzzy);
                for (size_t f = 1; f < fuzzies.size(); ++f) {
                    for (const auto& h : queryExactAll(fuzzies[f], 8)) {
                        hasFuzzyMatches = true;
                        std::string hint = "(" + detail::formatPinyinApostrophe(h.pinyin) + ")";
                        double sc = 220.0 + (detail::freqToLogScore(h.freq) + 10.0) * 5.0 - static_cast<double>(pIdx) * 10.0;
                        addScored(h.word, std::move(hint), "", rawBuffer_.size(), sc, CandidateKind::FuzzyPinyin, 75, 0);
                    }
                }
            } else if (path.tokens.size() >= 2 &&
                       std::none_of(path.tokens.begin(), path.tokens.end(),
                                    [](const SyllableToken& t) { return t.isPartial; })) {
                auto combos = PinyinTable::expandFuzzyTokens(path.tokens, config_.fuzzy, 12);
                for (size_t ci = 0; ci < combos.size(); ++ci) {
                    std::string fKey;
                    for (size_t k = 0; k < combos[ci].size(); ++k) {
                        if (k > 0) fKey.push_back(' ');
                        fKey.append(combos[ci][k]);
                    }
                    for (const auto& h : queryExactAll(fKey, 8)) {
                        hasFuzzyMatches = true;
                        std::string hint = "(" + detail::formatPinyinApostrophe(h.pinyin) + ")";
                        double sc = 220.0 + (detail::freqToLogScore(h.freq) + 10.0) * 5.0 -
                                    static_cast<double>(pIdx) * 10.0 - static_cast<double>(ci) * 1.0;
                        addScored(h.word, std::move(hint), "", rawBuffer_.size(), sc, CandidateKind::FuzzyPinyin, 75, 0);
                    }
                }
            }
        }

        // 3e. Partial prefix phrase candidates:
        // exact full-syllable prefix = 78 > Jianpin prefix = 72; prefix pins = 86 (or 76 if full exact exists)
        const size_t prefixPathLimit = std::min<size_t>(paths.size(), 2);
        for (size_t pIdx = 0; pIdx < prefixPathLimit; ++pIdx) {
            const auto& path = paths[pIdx];
            const size_t pTokCount = path.tokens.size();
            const double pathPenalty = static_cast<double>(pIdx) * 12.0;
            for (size_t len = pTokCount > 1 ? pTokCount - 1 : 0; len >= 2; --len) {
                const bool anyPartialPrefix = std::any_of(
                    path.tokens.begin(), path.tokens.begin() + len,
                    [](const SyllableToken& t) { return t.isPartial; });
                const size_t consumedBytes = path.tokens[len - 1].start + path.tokens[len - 1].length;
                const double lenBonus = static_cast<double>(len) * 8.0;
                const std::string subRaw = (consumedBytes <= lowerBuf.size())
                    ? lowerBuf.substr(0, consumedBytes)
                    : std::string{};

                if (anyPartialPrefix) {
                    if (pIdx != 0) {
                        continue; // Never let alternate broken splits (e.g. "w"+"ei"+"bi") inject fake Jianpin ("we")
                    }
                    const bool prefixAllShort = (len <= 4) && std::all_of(
                        path.tokens.begin(), path.tokens.begin() + len,
                        [](const SyllableToken& t) { return t.text.size() <= 2; });
                    if (prefixAllShort) {
                        std::string subInitials;
                        subInitials.reserve(len);
                        for (size_t k = 0; k < len; ++k) {
                            if (!path.tokens[k].text.empty()) {
                                subInitials.push_back(path.tokens[k].text[0]);
                            }
                        }
                        if (subInitials.size() >= 2) {
                            for (const auto& h : queryJianpinAll(subInitials, 12)) {
                                if (detail::countSyllablesInPinyin(h.pinyin) > len) continue;
                                uint32_t tier = userDict_.getTier(h.pinyin, h.word);
                                double sc = 160.0 + lenBonus + (detail::freqToLogScore(h.freq) + 10.0) * 5.0 - pathPenalty;
                                addScored(h.word, "", h.pinyin, consumedBytes, sc,
                                          CandidateKind::ChineseJianpin, 72, tier);
                            }
                            for (const auto& [uKey, uEntry] : userDict_.lookupByInitials(subInitials)) {
                                if (detail::countSyllablesInPinyin(uKey) > len) continue;
                                uint32_t tier = UserDict::tierForCount(uEntry.count);
                                double sc = 160.0 + lenBonus + static_cast<double>(uEntry.count) * 4.0 - pathPenalty;
                                addScored(uEntry.word, "", uKey, consumedBytes, sc,
                                          CandidateKind::ChineseJianpin, 72, tier);
                            }
                        }
                    }
                } else {
                    std::string subKey;
                    for (size_t k = 0; k < len; ++k) {
                        if (k > 0) subKey.push_back(' ');
                        subKey.append(path.tokens[k].text);
                    }
                    for (const auto& h : queryExactAll(subKey, (len == 2) ? 16 : 8)) {
                        double sc = 140.0 + lenBonus + (detail::freqToLogScore(h.freq) + 10.0) * 5.0 - pathPenalty;
                        addScored(h.word, "", subKey, consumedBytes, sc, CandidateKind::ChinesePhrase,
                                  78, userDict_.getTier(subKey, h.word));
                    }
                    for (const auto& u : userDict_.lookup(subKey)) {
                        uint32_t tier = UserDict::tierForCount(u.count);
                        if (tier >= 1 || u.word.size() > 3) {
                            addScored(u.word, "", subKey, consumedBytes, 130.0 + lenBonus - pathPenalty,
                                      CandidateKind::ChinesePhrase, 78, tier);
                        }
                    }
                }

                // Honor pinned slots saved on this prefix (e.g. "sm" -> "什么" when typing "smguia")
                if (pIdx == 0 && !subRaw.empty()) {
                    for (const auto& pin : userDict_.getPins(subRaw)) {
                        std::string mKey;
                        auto existingIt = seenIdx.find(pin.word);
                        if (existingIt != seenIdx.end()) {
                            mKey = scored[existingIt->second].item.matchedKey;
                        }
                        int pinGroup = hasFullExactChinese ? 76 : 86;
                        uint32_t pinTier = 100u + static_cast<uint32_t>(4 - std::clamp(pin.slot, 0, 4));
                        double pinScore = 500.0 + lenBonus - static_cast<double>(pin.slot) * 10.0;
                        addScored(pin.word, "", std::move(mKey), consumedBytes, pinScore,
                                  CandidateKind::ChineseJianpin, pinGroup, pinTier, pin.slot);
                    }
                }
            }

            // 3f. First-syllable single characters (capped to 1 syllable when raw has 1 token e.g. 'w')
            if (!path.tokens.empty()) {
                const auto& firstTok = path.tokens[0];
                if (pIdx > 0 && (firstTok.isPartial || firstTok.text == bestPath.tokens[0].text)) {
                    continue;
                }
                const size_t firstBytes = firstTok.start + firstTok.length;
                const int singleGroup = (tokCount == 1 && !hasFullExactChinese && !hasFuzzyMatches) ? 90 : 60;
                if (!firstTok.isPartial) {
                    for (const auto& h : queryExactAll(firstTok.text, (pIdx == 0) ? 120 : 30)) {
                        double sc = 90.0 + (detail::freqToLogScore(h.freq) + 10.0) * 5.0 - pathPenalty;
                        addScored(h.word, "", firstTok.text, firstBytes, sc, CandidateKind::ChinesePhrase,
                                  singleGroup, userDict_.getTier(firstTok.text, h.word));
                    }
                    for (const auto& u : userDict_.lookup(firstTok.text)) {
                        uint32_t tier = UserDict::tierForCount(u.count);
                        if (tier >= 1) {
                            addScored(u.word, "", firstTok.text, firstBytes, 80.0 - pathPenalty,
                                      CandidateKind::ChinesePhrase, singleGroup, tier);
                        }
                    }
                    if (scored.size() < 5) {
                        for (const auto& h : queryPinyinPrefixAll(firstTok.text, 20)) {
                            if (detail::countSyllablesInPinyin(h.pinyin) != 1) continue;
                            double sc = 60.0 + (detail::freqToLogScore(h.freq) + 10.0) * 5.0 - pathPenalty;
                            addScored(h.word, "", h.pinyin, firstBytes, sc, CandidateKind::ChinesePhrase,
                                      singleGroup, userDict_.getTier(h.pinyin, h.word));
                        }
                    }
                } else {
                    const size_t capSyllables = (tokCount == 1) ? 1 : tokCount;
                    const auto matchingSyls = PinyinTable::syllablesWithPrefix(firstTok.text);
                    if (!matchingSyls.empty()) {
                        const size_t perSylLimit = (matchingSyls.size() <= 6) ? 30 : 12;
                        for (std::string_view syl : matchingSyls) {
                            for (const auto& h : queryExactAll(syl, perSylLimit)) {
                                double sc = 70.0 + (detail::freqToLogScore(h.freq) + 10.0) * 5.0 - pathPenalty;
                                addScored(h.word, "", h.pinyin, firstBytes, sc, CandidateKind::ChinesePhrase,
                                          singleGroup, userDict_.getTier(h.pinyin, h.word));
                            }
                        }
                        for (const auto& [uKey, uEntry] : userDict_.lookupSingleSyllablePrefix(firstTok.text)) {
                            uint32_t tier = UserDict::tierForCount(uEntry.count);
                            double sc = 75.0 + static_cast<double>(uEntry.count) * 2.0 - pathPenalty;
                            addScored(uEntry.word, "", uKey, firstBytes, sc, CandidateKind::ChinesePhrase,
                                      singleGroup, tier);
                        }
                    } else {
                        for (const auto& h : queryPinyinPrefixAll(firstTok.text, 60)) {
                            if (detail::countSyllablesInPinyin(h.pinyin) > capSyllables) continue;
                            double sc = 70.0 + (detail::freqToLogScore(h.freq) + 10.0) * 5.0 - pathPenalty;
                            addScored(h.word, "", h.pinyin, firstBytes, sc, CandidateKind::ChinesePhrase,
                                      singleGroup, userDict_.getTier(h.pinyin, h.word));
                        }
                    }
                }
            }
        }
    }

    // 3g. Typo auto-correction when raw input contains broken/invalid partial sequences (never learned; groupRank = 72)
    const bool isInProgressValidPrefix = !paths.empty() && paths[0].tokens.size() >= 2 &&
        std::all_of(paths[0].tokens.begin(), paths[0].tokens.end() - 1,
                    [](const SyllableToken& t) { return !t.isPartial; }) &&
        PinyinTable::isSyllablePrefix(paths[0].tokens.back().text);
    if (config_.typoCorrection && hasAnyDict() && lowerBuf.size() >= 3) {
        const bool hasBrokenPartial = paths.empty() || (!isInProgressValidPrefix && std::any_of(
            paths[0].tokens.begin(), paths[0].tokens.end(), [](const SyllableToken& t) { return t.isPartial; }));
        if (hasBrokenPartial) {
            auto typoPaths = PinyinTable::segmentWithTypos(lowerBuf, 3);
            for (size_t tIdx = 0; tIdx < typoPaths.size(); ++tIdx) {
                const auto& tPath = typoPaths[tIdx];
                if (std::any_of(tPath.tokens.begin(), tPath.tokens.end(),
                                [](const SyllableToken& t) { return t.isPartial; })) {
                    continue;
                }
                const std::string tKey = tPath.joinedKey();
                const std::string tHint = "(" + tPath.preeditDisplay() + ")";
                auto tHits = queryExactAll(tKey, 8);
                for (const auto& h : tHits) {
                    double sc = 260.0 + (detail::freqToLogScore(h.freq) + 10.0) * 5.0 - static_cast<double>(tIdx) * 15.0;
                    addScored(h.word, tHint, "", rawBuffer_.size(), sc, CandidateKind::TypoCorrection, 72, 0);
                }
                if (tHits.empty() && tPath.tokens.size() >= 2) {
                    double vScore = 0.0;
                    std::string sent = buildWholeSentence(tPath, vScore);
                    if (!sent.empty()) {
                        addScored(sent, tHint, "", rawBuffer_.size(), 240.0 + vScore,
                                  CandidateKind::TypoCorrection, 72, 0);
                    }
                }
            }
        }
    }

    // Preserve explicit trailing apostrophe separator (e.g. "dan'" before typing 'g')
    if (!rawBuffer_.empty() && rawBuffer_.back() == '\'' &&
        (segmentedHeader_.empty() || segmentedHeader_.back() != '\'')) {
        segmentedHeader_.push_back('\'');
    }

    // 4. Bilingual mixed English candidates (surface common/custom English words visibly on Page 1)
    std::string page1SurfaceEnWord;
    size_t page1SurfaceTargetIdx = 2;
    auto isTwoLetterWhitelist = [](std::string_view s) {
        static constexpr std::string_view kList[] = {
            "ok", "ai", "ui", "ux", "ip", "id", "os", "pc",
            "pr", "qa", "vs", "io", "db", "js", "ts", "go"
        };
        for (std::string_view w : kList) {
            if (s == w) return true;
        }
        return false;
    };
    if (config_.mixedEnglish && hasAnyDict() && lowerBuf.size() >= 2) {
        auto enHits = queryEnglishPrefixAll(lowerBuf, 8);
        const bool partialFallback = !paths.empty() && std::any_of(
            paths[0].tokens.begin(), paths[0].tokens.end(), [](const SyllableToken& t) { return t.isPartial; });
        for (size_t i = 0; i < enHits.size(); ++i) {
            const std::string lowerEn = detail::toLowerAscii(enHits[i].word);
            const bool exactEn = (lowerEn == lowerBuf);
            const bool isCommonEn = (enHits[i].freq >= 1000);
            const uint32_t enTier = userDict_.getTier(lowerEn, enHits[i].word);
            const bool isDemotedTwoCharEn =
                (exactEn && lowerBuf.size() == 2 && !hasFullExactChinese && enTier == 0 && !isTwoLetterWhitelist(lowerEn));
            int enGroup = 55;
            if (exactEn) {
                if (hasFullExactChinese) {
                    enGroup = 80;
                } else if (isDemotedTwoCharEn) {
                    enGroup = 82;
                } else {
                    enGroup = 95;
                }
            } else if (!hasFullExactChinese && lowerBuf.size() >= 3 && (partialFallback || isCommonEn)) {
                enGroup = isCommonEn ? 74 : 64;
            } else if (isCommonEn && lowerBuf.size() >= 3) {
                enGroup = 62;
            }
            double enScore = exactEn
                ? (260.0 + (isCommonEn ? 30.0 : 0.0) + (detail::freqToLogScore(enHits[i].freq) + 10.0))
                : (115.0 + (detail::freqToLogScore(enHits[i].freq) + 10.0) * 2.5);
            addScored(enHits[i].word, "[EN]", lowerEn, rawBuffer_.size(), enScore,
                      CandidateKind::EnglishWord, enGroup, enTier);
            if (page1SurfaceEnWord.empty() && (isCommonEn || enTier >= 1) &&
                (exactEn || (!hasFullExactChinese && lowerBuf.size() >= 3))) {
                page1SurfaceEnWord = enHits[i].word;
                page1SurfaceTargetIdx = isDemotedTwoCharEn ? 3 : 2;
            }
        }
    }

    // Fallback raw input if list is empty
    if (scored.empty()) {
        addScored(rawBuffer_, "[原文]", "", rawBuffer_.size(), 10.0, CandidateKind::RawInput, 10, 0);
    }

    // Sort cleanly: GroupRank DESC -> Tier DESC -> Default Dictionary Score DESC (default order preserved within each tier)
    std::stable_sort(scored.begin(), scored.end(), [](const ScoredCandidate& a, const ScoredCandidate& b) {
        if (a.groupRank != b.groupRank) return a.groupRank > b.groupRank;
        if (a.tier != b.tier) return a.tier > b.tier;
        return a.item.score > b.item.score;
    });

    candidates_.reserve(scored.size());
    for (auto& sc : scored) {
        candidates_.push_back(std::move(sc.item));
    }

    // Keep top common/user English candidate visible on Page 1 even when Chinese homophones share the spelling (e.g. "you", "can", "hel")
    if (!page1SurfaceEnWord.empty() && candidates_.size() > 2) {
        auto it = std::find_if(candidates_.begin(), candidates_.end(), [&](const CandidateItem& ci) {
            return ci.kind == CandidateKind::EnglishWord && ci.text == page1SurfaceEnWord;
        });
        if (it != candidates_.end()) {
            size_t curIdx = static_cast<size_t>(std::distance(candidates_.begin(), it));
            size_t targetIdx = std::min<size_t>(page1SurfaceTargetIdx, candidates_.size() - 1);
            if (curIdx > targetIdx) {
                CandidateItem promoted = std::move(*it);
                candidates_.erase(it);
                candidates_.insert(candidates_.begin() + static_cast<std::ptrdiff_t>(targetIdx), std::move(promoted));
            }
        }
    }

    // 5. Apply custom right-click pinned slots (0..4 => display 1..5), setting pinnedSlot for independent badge rendering
    auto pins = userDict_.getPins(rawBuffer_);
    if (!pins.empty()) {
        struct PendingPin {
            int slot;
            CandidateItem item;
        };
        std::vector<PendingPin> toInsert;
        toInsert.reserve(pins.size());
        for (const auto& pin : pins) {
            CandidateItem extracted;
            auto it = std::find_if(candidates_.begin(), candidates_.end(), [&](const CandidateItem& ci) {
                return ci.text == pin.word;
            });
            if (it != candidates_.end()) {
                extracted = std::move(*it);
                candidates_.erase(it);
            } else {
                extracted.text = pin.word;
                extracted.consumedRawChars = rawBuffer_.size();
                extracted.score = 999.0;
                extracted.kind = CandidateKind::ChinesePhrase;
            }
            extracted.pinnedSlot = pin.slot;
            toInsert.push_back(PendingPin{pin.slot, std::move(extracted)});
        }
        std::sort(toInsert.begin(), toInsert.end(), [](const PendingPin& a, const PendingPin& b) {
            return a.slot < b.slot;
        });
        for (auto& p : toInsert) {
            size_t pos = std::min<size_t>(static_cast<size_t>(std::max(0, p.slot)), candidates_.size());
            candidates_.insert(candidates_.begin() + static_cast<std::ptrdiff_t>(pos), std::move(p.item));
        }
    }
}

} // namespace liteime::engine
