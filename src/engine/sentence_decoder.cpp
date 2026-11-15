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

std::string BilingualEngine::buildWholeSentence(const SegmentPath& path, double& outScore) const {
    const size_t n = path.tokens.size();
    if (n == 0 || !hasAnyDict()) {
        outScore = -1e9;
        return {};
    }

    struct DpNode {
        double score = -1e18;
        int prev = -1;
        std::string word;
    };
    std::vector<DpNode> dp(n + 1);
    dp[0].score = 0.0;

    for (size_t i = 0; i < n; ++i) {
        if (dp[i].score <= -1e17) continue;

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

            std::string bestWord;
            uint32_t bestFreq = 0;

            if (!hasPartial) {
                for (const auto& u : userDict_.lookup(key)) {
                    uint32_t tier = UserDict::tierForCount(u.count);
                    if (tier >= 1) {
                        bestWord = u.word;
                        bestFreq = 500000u + tier * 200000u;
                        break;
                    }
                }
            }

            if (bestWord.empty()) {
                if (!hasPartial) {
                    auto exactHits = queryExactAll(key, 1);
                    if (!exactHits.empty()) {
                        bestWord = exactHits[0].word;
                        bestFreq = exactHits[0].freq;
                    } else if (span == 1) {
                        auto fuzzies = PinyinTable::expandFuzzy(path.tokens[i].text, config_.fuzzy);
                        for (size_t f = 1; f < fuzzies.size(); ++f) {
                            auto fHits = queryExactAll(fuzzies[f], 1);
                            if (!fHits.empty() && fHits[0].freq > bestFreq) {
                                bestWord = fHits[0].word;
                                bestFreq = fHits[0].freq / 2;
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
                        bestWord = h.word;
                        bestFreq = std::max<uint32_t>(1, h.freq / 2);
                        break;
                    }
                } else {
                    auto pHits = queryPinyinPrefixAll(path.tokens[i].text, 12);
                    for (const auto& h : pHits) {
                        if (detail::countSyllablesInPinyin(h.pinyin) == 1) {
                            bestWord = h.word;
                            bestFreq = std::max<uint32_t>(1, h.freq / 4);
                            break;
                        }
                    }
                }
            }

            if (bestWord.empty()) continue;

            // Multi-syllable phrases naturally avoid paying multiple unigram penalties;
            // add a modest cohesion bonus for legitimate multi-char words.
            double cohesionBonus = (span > 1) ? (static_cast<double>(span - 1) * 0.6) : 0.0;
            double edgeScore = detail::freqToLogScore(bestFreq) + cohesionBonus;
            if (dp[i].score + edgeScore > dp[i + span].score) {
                dp[i + span].score = dp[i].score + edgeScore;
                dp[i + span].prev = static_cast<int>(i);
                dp[i + span].word = std::move(bestWord);
            }
        }

        // Fallback if token couldn't match anything at all
        if (dp[i + 1].score <= -1e17) {
            dp[i + 1].score = dp[i].score - 100.0;
            dp[i + 1].prev = static_cast<int>(i);
            dp[i + 1].word = path.tokens[i].text;
        }
    }

    if (dp[n].score <= -1e17) {
        outScore = -1e9;
        return {};
    }

    std::vector<std::string> parts;
    for (int cur = static_cast<int>(n); cur > 0; cur = dp[cur].prev) {
        parts.push_back(dp[cur].word);
    }
    std::reverse(parts.begin(), parts.end());
    std::string sentence;
    for (const auto& p : parts) sentence += p;
    outScore = dp[n].score;
    return sentence;
}

} // namespace liteime::engine
