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
    };
    struct EdgeOption {
        std::string word;
        double baseScore = 0.0;
    };

    std::vector<std::vector<DpNode>> dp(n + 1);
    std::string initialCtx;
    if (!prefixSteps_.empty()) {
        initialCtx = prefixSteps_.back().word;
    } else if (!lastCommittedWord_.empty()) {
        initialCtx = lastCommittedWord_;
    }
    dp[0].push_back(DpNode{0.0, -1, -1, initialCtx});

    for (size_t i = 0; i < n; ++i) {
        if (dp[i].empty()) continue;

        const size_t maxSpan = std::min<size_t>(8, n - i);
        for (size_t span = maxSpan; span >= 1; --span) {
            bool hasPartial = false;
            bool nonFinalPartial = false;
            std::string key;
            for (size_t k = 0; k < span; ++k) {
                if (k > 0) key.push_back(' ');
                key.append(path.tokens[i + k].text);
                if (path.tokens[i + k].isPartial) {
                    hasPartial = true;
                    if (i + k + 1 != n) nonFinalPartial = true;
                }
            }
            // Allow multi-syllable tail-partial span only when reaching end of sentence (e.g. "shu ru f" -> "输入法")
            if (span > 1 && (nonFinalPartial || (hasPartial && (i + span != n)))) {
                continue;
            }

            std::vector<EdgeOption> options;
            auto pushOption = [&options](std::string word, uint32_t freq, double cohesionBonus) {
                if (word.empty()) return;
                for (const auto& existing : options) {
                    if (existing.word == word) return;
                }
                double sc = detail::freqToLogScore(freq) + cohesionBonus;
                options.push_back(EdgeOption{std::move(word), sc});
            };
            const double cohesionBonus = (span > 1) ? (static_cast<double>(span - 1) * 0.6) : 0.0;

            if (!hasPartial) {
                for (const auto& u : userDict_.lookup(key)) {
                    uint32_t tier = UserDict::tierForCount(u.count);
                    if (tier >= 1) {
                        pushOption(u.word, 500000u + tier * 200000u, cohesionBonus);
                        if (options.size() >= kEdgeTopK) break;
                    }
                }
            }

            if (options.size() < kEdgeTopK) {
                if (!hasPartial) {
                    auto exactHits = queryExactAll(key, kEdgeTopK);
                    for (const auto& h : exactHits) {
                        pushOption(h.word, h.freq, cohesionBonus);
                        if (options.size() >= kEdgeTopK) break;
                    }
                    if (options.empty() && span == 1) {
                        auto fuzzies = PinyinTable::expandFuzzy(path.tokens[i].text, config_.fuzzy);
                        for (size_t f = 1; f < fuzzies.size(); ++f) {
                            auto fHits = queryExactAll(fuzzies[f], 2);
                            for (const auto& fh : fHits) {
                                pushOption(fh.word, std::max<uint32_t>(1, fh.freq / 2), cohesionBonus);
                            }
                        }
                    }
                } else if (span > 1) {
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
                        pushOption(h.word, std::max<uint32_t>(1, h.freq / 2), cohesionBonus);
                        if (options.size() >= 2) break;
                    }
                } else {
                    auto pHits = queryPinyinPrefixAll(path.tokens[i].text, 12);
                    for (const auto& h : pHits) {
                        if (detail::countSyllablesInPinyin(h.pinyin) == 1) {
                            pushOption(h.word, std::max<uint32_t>(1, h.freq / 4), cohesionBonus);
                            if (options.size() >= 2) break;
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
                            }
                            replaced = true;
                            break;
                        }
                    }
                    if (!replaced) {
                        nextBeam.push_back(DpNode{candScore, static_cast<int>(i), static_cast<int>(bIdx), opt.word});
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
                path.tokens[i].text});
        }
    }

    if (dp[n].empty()) {
        outScore = -1e9;
        return {};
    }

    std::vector<std::string> parts;
    int curNode = static_cast<int>(n);
    int curBeam = 0;
    while (curNode > 0) {
        const auto& st = dp[curNode][curBeam];
        parts.push_back(st.word);
        curNode = st.prevNode;
        curBeam = st.prevBeam;
        if (curBeam < 0) break;
    }
    std::reverse(parts.begin(), parts.end());
    std::string sentence;
    for (const auto& p : parts) sentence += p;
    outScore = dp[n][0].score;
    return sentence;
}

} // namespace liteime::engine
