#include "engine/engine_internal.hpp"

#include <algorithm>
#include <unordered_set>

namespace liteime::engine {

namespace {

template <typename Fn>
std::vector<DictCandidate> mergeDictQueries(
    const BinaryDict* primary,
    const std::vector<const BinaryDict*>& extras,
    size_t limit,
    Fn&& fn) {
    if (extras.empty()) {
        return (primary && primary->isLoaded()) ? fn(primary, limit) : std::vector<DictCandidate>{};
    }

    std::vector<DictCandidate> all;
    if (primary && primary->isLoaded()) {
        auto h = fn(primary, limit);
        all.insert(all.end(), std::make_move_iterator(h.begin()), std::make_move_iterator(h.end()));
    }
    for (const BinaryDict* d : extras) {
        if (d && d->isLoaded()) {
            auto h = fn(d, limit);
            all.insert(all.end(), std::make_move_iterator(h.begin()), std::make_move_iterator(h.end()));
        }
    }
    std::stable_sort(all.begin(), all.end(), [](const DictCandidate& a, const DictCandidate& b) {
        return a.freq > b.freq;
    });
    std::vector<DictCandidate> out;
    out.reserve(std::min(limit, all.size()));
    std::unordered_set<std::string> seen;
    for (auto& h : all) {
        if (seen.insert(h.word).second) {
            out.push_back(std::move(h));
            if (out.size() >= limit) break;
        }
    }
    return out;
}

} // namespace

void BilingualEngine::addExtraDictionary(const BinaryDict* dict) {
    if (!dict) return;
    if (std::find(extraDicts_.begin(), extraDicts_.end(), dict) == extraDicts_.end()) {
        extraDicts_.push_back(dict);
    }
}

bool BilingualEngine::hasAnyDict() const {
    if (dict_ && dict_->isLoaded()) return true;
    for (const BinaryDict* d : extraDicts_) {
        if (d && d->isLoaded()) return true;
    }
    return false;
}

std::vector<DictCandidate> BilingualEngine::queryExactAll(std::string_view key, size_t limit) const {
    return mergeDictQueries(dict_, extraDicts_, limit, [&](const BinaryDict* d, size_t lim) {
        return d->lookupExact(key, lim);
    });
}

std::vector<DictCandidate> BilingualEngine::queryPinyinPrefixAll(std::string_view prefix, size_t limit) const {
    return mergeDictQueries(dict_, extraDicts_, limit, [&](const BinaryDict* d, size_t lim) {
        return d->lookupPinyinPrefix(prefix, lim);
    });
}

std::vector<DictCandidate> BilingualEngine::queryJianpinAll(std::string_view initials, size_t limit) const {
    return mergeDictQueries(dict_, extraDicts_, limit, [&](const BinaryDict* d, size_t lim) {
        return d->lookupJianpin(initials, lim);
    });
}

std::vector<DictCandidate> BilingualEngine::queryEnglishPrefixAll(std::string_view prefix, size_t limit) const {
    return mergeDictQueries(dict_, extraDicts_, limit, [&](const BinaryDict* d, size_t lim) {
        return d->lookupEnglishPrefix(prefix, lim);
    });
}

double BilingualEngine::contextBigramBonus(std::string_view prevWord, std::string_view curWord) const {
    if (prevWord.empty() || curWord.empty()) return 0.0;
    double bStatic = bigram_.scoreTransition(prevWord, curWord);
    double bUser = userDict_.getBigramBonus(prevWord, curWord);
    return bStatic + bUser;
}

std::string BilingualEngine::buildWholeSentence(const SegmentPath& path, double& outScore) const {
    std::string unusedResolved;
    return buildWholeSentenceWithKey(path, outScore, unusedResolved);
}

std::string BilingualEngine::buildWholeSentenceWithKey(const SegmentPath& path,
                                                       double& outScore,
                                                       std::string& outResolvedPinyin) const {
    outResolvedPinyin.clear();
    const size_t n = path.tokens.size();
    if (n == 0 || !hasAnyDict()) {
        outScore = -1e9;
        return {};
    }

    constexpr size_t kBeamWidth = 4;
    constexpr size_t kEdgeTopK = 4;

    struct DpNode {
        double score = -1e18;
        int prevNode = -1;
        int prevBeam = -1;
        std::string word;
        std::string pinyinKey;
        bool usedFallback = false;
    };
    struct EdgeOption {
        std::string word;
        std::string pinyinKey;
        double baseScore = 0.0;
    };

    std::vector<std::vector<DpNode>> dp(n + 1);
    std::string initialCtx;
    if (!prefixSteps_.empty()) {
        initialCtx = prefixSteps_.back().word;
    } else if (!lastCommittedWord_.empty()) {
        initialCtx = lastCommittedWord_;
    }
    dp[0].push_back(DpNode{0.0, -1, -1, initialCtx, {}, false});

    for (size_t i = 0; i < n; ++i) {
        if (dp[i].empty()) continue;

        const size_t maxSpan = std::min<size_t>(8, n - i);
        for (size_t span = maxSpan; span >= 1; --span) {
            bool hasPartial = false;
            bool nonFinalPartial = false;
            bool hasSingleCharToken = false;
            bool hasPartialBeforeFull = false;
            std::string key;
            for (size_t k = 0; k < span; ++k) {
                const auto& curTok = path.tokens[i + k];
                if (k > 0) key.push_back(' ');
                key.append(curTok.text);
                if (curTok.text.size() == 1) {
                    hasSingleCharToken = true;
                }
                if (curTok.isPartial) {
                    hasPartial = true;
                    if (k + 1 < span) nonFinalPartial = true;
                } else if (hasPartial) {
                    hasPartialBeforeFull = true;
                }
            }
            // Allow mixed/Jianpin multi-syllable spans up to 4 syllables; avoid mid-sentence
            // "[full..., partial]" expansions before full syllables or "[partial, full]" typo shapes.
            if (span > 4 && hasPartial) {
                continue;
            }
            if (hasPartialBeforeFull) {
                continue;
            }
            if (span > 1 && hasPartial && !nonFinalPartial &&
                (i + span < n) && !path.tokens[i + span].isPartial) {
                continue;
            }

            std::vector<EdgeOption> options;
            auto pushOption = [&options](std::string word, std::string pKey, uint32_t freq, double bonus) {
                if (word.empty()) return;
                for (const auto& existing : options) {
                    if (existing.word == word) return;
                }
                double sc = detail::freqToLogScore(freq) + bonus;
                options.push_back(EdgeOption{std::move(word), std::move(pKey), sc});
            };
            const double cohesionBonus = (span > 1) ? (static_cast<double>(span - 1) * 0.6) : 0.0;

            if (!hasPartial) {
                for (const auto& u : userDict_.lookup(key)) {
                    uint32_t tier = UserDict::tierForCount(u.count);
                    if (tier >= 1) {
                        pushOption(u.word, key, 500000u + tier * 200000u, cohesionBonus);
                        if (options.size() >= kEdgeTopK) break;
                    }
                }
                if (options.size() < kEdgeTopK) {
                    auto exactHits = queryExactAll(key, kEdgeTopK);
                    for (const auto& h : exactHits) {
                        pushOption(h.word, h.pinyin.empty() ? key : h.pinyin, h.freq, cohesionBonus);
                        if (options.size() >= kEdgeTopK) break;
                    }
                }
                if (options.empty() && span == 1) {
                    auto fuzzies = PinyinTable::expandFuzzy(path.tokens[i].text, config_.fuzzy);
                    for (size_t f = 1; f < fuzzies.size(); ++f) {
                        auto fHits = queryExactAll(fuzzies[f], 2);
                        for (const auto& fh : fHits) {
                            pushOption(fh.word, fh.pinyin.empty() ? fuzzies[f] : fh.pinyin,
                                       std::max<uint32_t>(1, fh.freq / 2), cohesionBonus);
                        }
                    }
                }
            }

            if ((hasPartial || (span >= 2 && span <= 4 && options.empty() && hasSingleCharToken)) &&
                options.size() < kEdgeTopK) {
                if (span > 1) {
                    std::vector<std::pair<std::string_view, bool>> patTokens;
                    patTokens.reserve(span);
                    std::string subInitials;
                    subInitials.reserve(span);
                    bool validPattern = true;
                    for (size_t k = 0; k < span; ++k) {
                        const auto& tok = path.tokens[i + k];
                        if (tok.text.empty()) {
                            validPattern = false;
                            break;
                        }
                        patTokens.emplace_back(tok.text, tok.isPartial);
                        subInitials.push_back(tok.text[0]);
                    }
                    if (validPattern) {
                        for (const auto& [uKey, uEntry] : userDict_.lookupByTokenPattern(patTokens)) {
                            const uint32_t tier = UserDict::tierForCount(uEntry.count);
                            if (tier == 0 && !uEntry.isPhrase) continue;
                            size_t extCount = 0;
                            UserDict::matchesTokenPattern(uKey, patTokens, &extCount);
                            pushOption(uEntry.word, uKey, 450000u + tier * 200000u,
                                       cohesionBonus - static_cast<double>(extCount) * 0.8);
                            if (options.size() >= kEdgeTopK) break;
                        }

                        if (!nonFinalPartial && options.size() < kEdgeTopK) {
                            std::string exactPrefix;
                            for (size_t k = 0; k + 1 < span; ++k) {
                                if (k > 0) exactPrefix.push_back(' ');
                                exactPrefix.append(path.tokens[i + k].text);
                            }
                            const std::string& tailPrefix = path.tokens[i + span - 1].text;
                            auto pHits = queryPinyinPrefixAll(key, 8);
                            for (const auto& h : pHits) {
                                if (detail::countSyllablesInPinyin(h.pinyin) != span) continue;
                                size_t lastSp = h.pinyin.rfind(' ');
                                if (lastSp == std::string::npos) continue;
                                std::string_view pView(h.pinyin);
                                if (pView.substr(0, lastSp) != exactPrefix) continue;
                                if (!pView.substr(lastSp + 1).starts_with(tailPrefix)) continue;
                                pushOption(h.word, h.pinyin, h.freq, cohesionBonus - 0.4);
                                if (options.size() >= kEdgeTopK) break;
                            }
                        }

                        if (options.size() < kEdgeTopK && subInitials.size() >= 2) {
                            auto jpHits = queryJianpinAll(subInitials, 64);
                            for (const auto& h : jpHits) {
                                size_t extCount = 0;
                                if (!UserDict::matchesTokenPattern(h.pinyin, patTokens, &extCount)) continue;
                                pushOption(h.word, h.pinyin, h.freq,
                                           cohesionBonus - static_cast<double>(extCount) * 0.4);
                                if (options.size() >= kEdgeTopK) break;
                            }
                        }
                    }
                } else {
                    const bool inTrailingPartialZone = std::all_of(
                        path.tokens.begin() + static_cast<std::ptrdiff_t>(i),
                        path.tokens.end(),
                        [](const SyllableToken& t) { return t.isPartial; });
                    if (inTrailingPartialZone) {
                        const auto matchingSyls = PinyinTable::syllablesWithPrefix(path.tokens[i].text);
                        if (!matchingSyls.empty()) {
                            std::vector<DictCandidate> pool;
                            pool.reserve(matchingSyls.size() * 2);
                            for (std::string_view syl : matchingSyls) {
                                auto hits = queryExactAll(syl, 2);
                                for (auto& h : hits) {
                                    pool.push_back(std::move(h));
                                }
                            }
                            std::sort(pool.begin(), pool.end(), [](const DictCandidate& a, const DictCandidate& b) {
                                return a.freq > b.freq;
                            });
                            for (const auto& h : pool) {
                                pushOption(h.word, h.pinyin, h.freq, -2.4);
                                if (options.size() >= 3) break;
                            }
                        } else {
                            auto pHits = queryPinyinPrefixAll(path.tokens[i].text, 12);
                            for (const auto& h : pHits) {
                                if (detail::countSyllablesInPinyin(h.pinyin) == 1) {
                                    pushOption(h.word, h.pinyin, h.freq, -2.4);
                                    if (options.size() >= 2) break;
                                }
                            }
                        }
                    }
                }
            }

            if (options.empty()) continue;

            auto& nextBeam = dp[i + span];
            for (size_t bIdx = 0; bIdx < dp[i].size(); ++bIdx) {
                const auto& prevState = dp[i][bIdx];
                for (const auto& opt : options) {
                    const double transBonus = prevState.word.empty()
                        ? 0.0
                        : contextBigramBonus(prevState.word, opt.word);
                    const double candScore = prevState.score + opt.baseScore + transBonus;

                    // Keep only distinct last-words in beam (or overwrite weaker path with same last-word)
                    bool replaced = false;
                    for (auto& existing : nextBeam) {
                        if (existing.word == opt.word) {
                            if (candScore > existing.score) {
                                existing.score = candScore;
                                existing.prevNode = static_cast<int>(i);
                                existing.prevBeam = static_cast<int>(bIdx);
                                existing.pinyinKey = opt.pinyinKey;
                                existing.usedFallback = false;
                            }
                            replaced = true;
                            break;
                        }
                    }
                    if (!replaced) {
                        nextBeam.push_back(DpNode{
                            candScore, static_cast<int>(i), static_cast<int>(bIdx),
                            opt.word, opt.pinyinKey, false});
                    }
                }
            }
            std::sort(nextBeam.begin(), nextBeam.end(), [](const DpNode& a, const DpNode& b) {
                return a.score > b.score;
            });
            if (nextBeam.size() > kBeamWidth) {
                nextBeam.resize(kBeamWidth);
            }
        }

        // Fallback if nothing could advance out of node i
        if (dp[i + 1].empty()) {
            const auto& prevState = dp[i][0];
            dp[i + 1].push_back(DpNode{
                prevState.score - 100.0,
                static_cast<int>(i),
                0,
                path.tokens[i].text,
                path.tokens[i].text,
                true});
        }
    }

    if (dp[n].empty()) {
        outScore = -1e9;
        return {};
    }

    std::vector<std::string> parts;
    std::vector<std::string> pinyinParts;
    bool anyFallback = false;
    size_t multiSyllableEdges = 0;
    size_t partialSingleEdges = 0;
    int curNode = static_cast<int>(n);
    int curBeam = 0;
    while (curNode > 0) {
        const auto& st = dp[curNode][curBeam];
        const int edgeSpan = curNode - st.prevNode;
        if (edgeSpan >= 2) ++multiSyllableEdges;
        if (edgeSpan == 1 && st.prevNode >= 0 && path.tokens[static_cast<size_t>(st.prevNode)].isPartial) {
            ++partialSingleEdges;
        }
        parts.push_back(st.word);
        if (!st.pinyinKey.empty()) {
            pinyinParts.push_back(st.pinyinKey);
        }
        if (st.usedFallback) anyFallback = true;
        curNode = st.prevNode;
        curBeam = st.prevBeam;
        if (curBeam < 0) break;
    }
    // Reject speculative single-char wildcard bridges when no multi-syllable phrase anchors the sentence
    // (e.g. "xianzguia" should keep prefix "西安" rather than inventing "先只鬼啊").
    if (partialSingleEdges > 0 && multiSyllableEdges == 0) {
        outScore = -1e9;
        return {};
    }
    std::reverse(parts.begin(), parts.end());
    std::reverse(pinyinParts.begin(), pinyinParts.end());
    std::string sentence;
    for (const auto& p : parts) sentence += p;
    if (!anyFallback) {
        for (size_t k = 0; k < pinyinParts.size(); ++k) {
            if (k > 0) outResolvedPinyin.push_back(' ');
            outResolvedPinyin.append(pinyinParts[k]);
        }
    }
    outScore = dp[n][0].score;
    return sentence;
}

} // namespace liteime::engine
