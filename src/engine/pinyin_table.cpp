#include "engine/pinyin_table.hpp"

#include <algorithm>
#include <array>
#include <unordered_set>

namespace liteime::engine {

namespace {

constexpr std::array kValidSyllables = {
    "a", "ai", "an", "ang", "ao",
    "ba", "bai", "ban", "bang", "bao", "bei", "ben", "beng", "bi", "bian", "biao", "bie", "bin", "bing", "bo", "bu",
    "ca", "cai", "can", "cang", "cao", "ce", "cen", "ceng", "cha", "chai", "chan", "chang", "chao", "che", "chen", "cheng", "chi", "chong", "chou", "chu", "chua", "chuai", "chuan", "chuang", "chui", "chun", "chuo", "ci", "cong", "cou", "cu", "cuan", "cui", "cun", "cuo",
    "da", "dai", "dan", "dang", "dao", "de", "dei", "den", "deng", "di", "dia", "dian", "diao", "die", "ding", "diu", "dong", "dou", "du", "duan", "dui", "dun", "duo",
    "e", "ei", "en", "eng", "er",
    "fa", "fan", "fang", "fei", "fen", "feng", "fiao", "fo", "fou", "fu",
    "ga", "gai", "gan", "gang", "gao", "ge", "gei", "gen", "geng", "gong", "gou", "gu", "gua", "guai", "guan", "guang", "gui", "gun", "guo",
    "ha", "hai", "han", "hang", "hao", "he", "hei", "hen", "heng", "hong", "hou", "hu", "hua", "huai", "huan", "huang", "hui", "hun", "huo",
    "ji", "jia", "jian", "jiang", "jiao", "jie", "jin", "jing", "jiong", "jiu", "ju", "juan", "jue", "jun",
    "ka", "kai", "kan", "kang", "kao", "ke", "kei", "ken", "keng", "kong", "kou", "ku", "kua", "kuai", "kuan", "kuang", "kui", "kun", "kuo",
    "la", "lai", "lan", "lang", "lao", "le", "lei", "leng", "li", "lia", "lian", "liang", "liao", "lie", "lin", "ling", "liu", "lo", "long", "lou", "lu", "luan", "lue", "lun", "luo", "lv", "lve",
    "ma", "mai", "man", "mang", "mao", "me", "mei", "men", "meng", "mi", "mian", "miao", "mie", "min", "ming", "miu", "mo", "mou", "mu",
    "na", "nai", "nan", "nang", "nao", "ne", "nei", "nen", "neng", "ng", "ni", "nian", "niang", "niao", "nie", "nin", "ning", "niu", "nong", "nou", "nu", "nuan", "nue", "nun", "nuo", "nv", "nve",
    "o", "ou",
    "pa", "pai", "pan", "pang", "pao", "pei", "pen", "peng", "pi", "pian", "piao", "pie", "pin", "ping", "po", "pou", "pu",
    "qi", "qia", "qian", "qiang", "qiao", "qie", "qin", "qing", "qiong", "qiu", "qu", "quan", "que", "qun",
    "ran", "rang", "rao", "re", "ren", "reng", "ri", "rong", "rou", "ru", "rua", "ruan", "rui", "run", "ruo",
    "sa", "sai", "san", "sang", "sao", "se", "sen", "seng", "sha", "shai", "shan", "shang", "shao", "she", "shei", "shen", "sheng", "shi", "shou", "shu", "shua", "shuai", "shuan", "shuang", "shui", "shun", "shuo", "si", "song", "sou", "su", "suan", "sui", "sun", "suo",
    "ta", "tai", "tan", "tang", "tao", "te", "tei", "teng", "ti", "tian", "tiao", "tie", "ting", "tong", "tou", "tu", "tuan", "tui", "tun", "tuo",
    "wa", "wai", "wan", "wang", "wei", "wen", "weng", "wo", "wu",
    "xi", "xia", "xian", "xiang", "xiao", "xie", "xin", "xing", "xiong", "xiu", "xu", "xuan", "xue", "xun",
    "ya", "yan", "yang", "yao", "ye", "yi", "yin", "ying", "yo", "yong", "you", "yu", "yuan", "yue", "yun",
    "za", "zai", "zan", "zang", "zao", "ze", "zei", "zen", "zeng", "zha", "zhai", "zhan", "zhang", "zhao", "zhe", "zhei", "zhen", "zheng", "zhi", "zhong", "zhou", "zhu", "zhua", "zhuai", "zhuan", "zhuang", "zhui", "zhun", "zhuo", "zi", "zong", "zou", "zu", "zuan", "zui", "zun", "zuo"
};

const std::unordered_set<std::string_view>& syllableSet() {
    static const std::unordered_set<std::string_view> set = [] {
        std::unordered_set<std::string_view> s;
        s.reserve(kValidSyllables.size() * 2);
        for (const char* item : kValidSyllables) {
            s.insert(item);
        }
        return s;
    }();
    return set;
}

const std::unordered_set<std::string_view>& prefixSet() {
    static const std::unordered_set<std::string_view> set = [] {
        std::unordered_set<std::string_view> s;
        s.reserve(1024);
        for (const char* item : kValidSyllables) {
            std::string_view sv(item);
            for (size_t len = 1; len <= sv.size(); ++len) {
                s.insert(sv.substr(0, len));
            }
        }
        return s;
    }();
    return set;
}

bool isVowel(char c) {
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'v';
}

} // namespace

std::string SegmentPath::joinedKey() const {
    std::string out;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) out.push_back(' ');
        out.append(tokens[i].text);
    }
    return out;
}

std::string SegmentPath::preeditDisplay() const {
    std::string out;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) out.push_back('\'');
        out.append(tokens[i].text);
    }
    return out;
}

std::string SegmentPath::initialsKey() const {
    std::string out;
    for (const auto& t : tokens) {
        if (!t.text.empty()) {
            out.push_back(t.text[0]);
        }
    }
    return out;
}

std::string PinyinTable::canonicalizeSyllable(std::string_view token) {
    if (token == "nue") return "nve";
    if (token == "lue") return "lve";
    if (token.size() >= 2 &&
        (token[0] == 'j' || token[0] == 'q' || token[0] == 'x' || token[0] == 'y') &&
        token[1] == 'v') {
        std::string out(token);
        out[1] = 'u';
        return out;
    }
    return std::string(token);
}

bool PinyinTable::isValidSyllable(std::string_view s) {
    return syllableSet().count(canonicalizeSyllable(s)) > 0;
}

bool PinyinTable::isValidInitial(std::string_view s) {
    if (s.size() == 1) {
        char c = s[0];
        return c >= 'a' && c <= 'z' && c != 'i' && c != 'u' && c != 'v';
    }
    return s == "zh" || s == "ch" || s == "sh";
}

bool PinyinTable::isSyllablePrefix(std::string_view s) {
    return prefixSet().count(canonicalizeSyllable(s)) > 0;
}

std::string PinyinTable::extractInitial(std::string_view syllable) {
    if (syllable.empty()) return {};
    if (syllable.size() >= 2 &&
        (syllable.substr(0, 2) == "zh" || syllable.substr(0, 2) == "ch" || syllable.substr(0, 2) == "sh")) {
        return std::string(syllable.substr(0, 2));
    }
    return std::string(syllable.substr(0, 1));
}

std::vector<std::string_view> PinyinTable::syllablesWithPrefix(std::string_view prefix) {
    std::vector<std::string_view> out;
    if (prefix.empty()) return out;
    const std::string norm = canonicalizeSyllable(prefix);
    for (const char* item : kValidSyllables) {
        std::string_view sv(item);
        if (sv.starts_with(norm)) {
            out.push_back(sv);
        }
    }
    return out;
}

std::vector<SegmentPath> PinyinTable::segment(std::string_view input, size_t maxPaths) {
    if (input.empty()) return {};

    // Split on manual apostrophe segments first, preserving positions
    struct Chunk {
        std::string_view sv;
        uint16_t offset;
    };
    std::vector<Chunk> chunks;
    size_t pos = 0;
    while (pos < input.size()) {
        if (input[pos] == '\'') {
            ++pos;
            continue;
        }
        size_t next = input.find('\'', pos);
        if (next == std::string_view::npos) next = input.size();
        if (next > pos) {
            chunks.push_back({input.substr(pos, next - pos), static_cast<uint16_t>(pos)});
        }
        pos = next;
    }

    if (chunks.empty()) return {};

    // Segment a single contiguous alphabetic chunk into top-K paths via DP + beam.
    auto segmentChunk = [](std::string_view sv, uint16_t baseOffset, size_t beamWidth) -> std::vector<SegmentPath> {
        const size_t n = sv.size();
        std::vector<std::vector<SegmentPath>> dp(n + 1);
        dp[0].push_back(SegmentPath{{}, 0});

        for (size_t i = 0; i < n; ++i) {
            if (dp[i].empty()) continue;

            if (dp[i].size() > beamWidth * 4) {
                std::sort(dp[i].begin(), dp[i].end(), [](const SegmentPath& a, const SegmentPath& b) {
                    if (a.score != b.score) return a.score > b.score;
                    return a.tokens.size() < b.tokens.size();
                });
                dp[i].resize(beamWidth * 4);
            }

            const size_t maxLen = std::min<size_t>(6, n - i);
            bool matchedFull = false;

            for (size_t len = maxLen; len >= 1; --len) {
                std::string_view sub = sv.substr(i, len);
                std::string normSub = canonicalizeSyllable(sub);
                bool full = syllableSet().count(normSub) > 0;
                bool atEnd = (i + len == n);
                bool partialInitial = !full && (isValidInitial(normSub) || (atEnd && prefixSet().count(normSub) > 0));

                if (!full && !partialInitial) continue;
                if (full) matchedFull = true;

                // Score heuristic: strongly prefer fewer, full syllables; discourage 1-letter vowels unless necessary
                int stepScore = 0;
                if (full) {
                    stepScore = static_cast<int>(len * 14) - 8;
                    if (len == 1 && isVowel(sub[0])) stepScore -= 6;
                    // Lookahead penalty if splitting leaves an awkward consonant-less orphan when longer exists
                    if (i + len < n && sv[i + len] == 'g' && (normSub.ends_with("an") || normSub.ends_with("en") || normSub.ends_with("in"))) {
                        stepScore -= 10;
                    }
                } else {
                    // Partial/initial token (jianpin or typing in progress)
                    stepScore = atEnd ? static_cast<int>(len * 4) - 10 : -14;
                }

                SyllableToken tok{std::move(normSub), !full, static_cast<uint16_t>(baseOffset + i), static_cast<uint16_t>(len)};
                for (const auto& prev : dp[i]) {
                    SegmentPath nextPath = prev;
                    nextPath.tokens.push_back(tok);
                    nextPath.score += stepScore;
                    dp[i + len].push_back(std::move(nextPath));
                }
            }

            // Fallback single character if nothing matched (keeps engine resilient on typos/English)
            if (dp[i + 1].empty() && !matchedFull) {
                SyllableToken tok{std::string(sv.substr(i, 1)), true, static_cast<uint16_t>(baseOffset + i), 1};
                for (const auto& prev : dp[i]) {
                    SegmentPath nextPath = prev;
                    nextPath.tokens.push_back(tok);
                    nextPath.score -= 30;
                    dp[i + 1].push_back(std::move(nextPath));
                }
            }

            // Prune beam at i+1..n? Prune dp[i+1] right after visiting i? Wait: future steps can write to i+len.
        }

        // Deduplicate & sort dp[n]
        auto& finals = dp[n];
        std::sort(finals.begin(), finals.end(), [](const SegmentPath& a, const SegmentPath& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.tokens.size() < b.tokens.size();
        });

        std::vector<SegmentPath> unique;
        std::unordered_set<std::string> seen;
        for (auto& p : finals) {
            std::string key = p.joinedKey();
            if (seen.insert(key).second) {
                unique.push_back(std::move(p));
                if (unique.size() >= beamWidth) break;
            }
        }
        return unique;
    };

    std::vector<SegmentPath> combined = segmentChunk(chunks[0].sv, chunks[0].offset, maxPaths);
    for (size_t c = 1; c < chunks.size(); ++c) {
        auto subPaths = segmentChunk(chunks[c].sv, chunks[c].offset, maxPaths);
        std::vector<SegmentPath> nextCombined;
        for (const auto& a : combined) {
            for (const auto& b : subPaths) {
                SegmentPath merged = a;
                merged.tokens.insert(merged.tokens.end(), b.tokens.begin(), b.tokens.end());
                merged.score += b.score;
                nextCombined.push_back(std::move(merged));
            }
        }
        std::sort(nextCombined.begin(), nextCombined.end(), [](const SegmentPath& a, const SegmentPath& b) {
            return a.score > b.score;
        });
        if (nextCombined.size() > maxPaths) nextCombined.resize(maxPaths);
        combined = std::move(nextCombined);
    }

    return combined;
}

std::vector<std::string> PinyinTable::expandFuzzy(std::string_view syllable, const FuzzyConfig& fuzzy) {
    const std::string canon = canonicalizeSyllable(syllable);
    std::vector<std::string> results;
    results.push_back(canon);

    auto addIfValid = [&](const std::string& cand) {
        if (cand != canon && syllableSet().count(cand) > 0) {
            if (std::find(results.begin(), results.end(), cand) == results.end()) {
                results.push_back(cand);
            }
        }
    };

    // Initial expansions
    std::vector<std::string> initialVariants;
    initialVariants.push_back(canon);

    auto trySwapPrefix = [&](std::string_view a, std::string_view b) {
        if (std::string_view(canon).starts_with(a)) {
            initialVariants.push_back(std::string(b) + canon.substr(a.size()));
        } else if (std::string_view(canon).starts_with(b)) {
            initialVariants.push_back(std::string(a) + canon.substr(b.size()));
        }
    };

    if (fuzzy.zh_z) trySwapPrefix("zh", "z");
    if (fuzzy.ch_c) trySwapPrefix("ch", "c");
    if (fuzzy.sh_s) trySwapPrefix("sh", "s");
    if (fuzzy.n_l) trySwapPrefix("n", "l");
    if (fuzzy.r_l) trySwapPrefix("r", "l");
    if (fuzzy.f_h) trySwapPrefix("f", "h");

    // Final expansions on each initial variant
    for (const auto& base : initialVariants) {
        addIfValid(base);
        auto trySwapSuffix = [&](std::string_view a, std::string_view b) {
            if (base.ends_with(a)) {
                addIfValid(base.substr(0, base.size() - a.size()) + std::string(b));
            } else if (base.ends_with(b)) {
                addIfValid(base.substr(0, base.size() - b.size()) + std::string(a));
            }
        };
        if (fuzzy.an_ang) trySwapSuffix("ang", "an");
        if (fuzzy.en_eng) trySwapSuffix("eng", "en");
        if (fuzzy.in_ing) trySwapSuffix("ing", "in");
        if (fuzzy.iang_ian) trySwapSuffix("iang", "ian");
        if (fuzzy.uang_uan) trySwapSuffix("uang", "uan");
    }

    return results;
}

std::vector<std::string> PinyinTable::suggestSyllableTypos(std::string_view rawToken) {
    std::vector<std::string> results;
    if (rawToken.size() < 2 || rawToken.size() > 8 || isValidSyllable(rawToken)) {
        return results;
    }

    // Start from v/ü-normalized token so typos inside jv/qv/xv/yv tokens (e.g. qvna -> quna -> quan) still resolve.
    std::string base = canonicalizeSyllable(rawToken);
    auto addIfValid = [&](const std::string& cand) {
        if (cand != base && syllableSet().count(cand) > 0) {
            if (std::find(results.begin(), results.end(), cand) == results.end()) {
                results.push_back(cand);
            }
        }
    };

    // 2. Adjacent character transposition (e.g. zhogn->zhong, zohng->zhong, hzong->zhong, xui->xiu, goa->gao)
    for (size_t i = 0; i + 1 < base.size(); ++i) {
        if (base[i] == base[i + 1]) continue;
        std::string cand = base;
        std::swap(cand[i], cand[i + 1]);
        addIfValid(cand);
    }

    // 3. Missing 'n' before trailing 'g' (*ag/*eg/*ig/*og/*iog/*iag/*uag -> *ng, e.g. zhog->zhong, xiag->xiang)
    if (base.size() >= 3 && base.back() == 'g') {
        std::string cand = base.substr(0, base.size() - 1) + "ng";
        addIfValid(cand);
    }

    // 4. Missing 'g' after trailing "on" (*on -> *ong, e.g. zhon->zhong, don->dong)
    if (base.size() >= 3 && base.ends_with("on")) {
        addIfValid(base + "g");
    }

    // 5. Accidental double-key bounce (e.g. zzhong->zhong, zhongg->zhong)
    for (size_t i = 0; i + 1 < base.size(); ++i) {
        if (base[i] == base[i + 1]) {
            std::string cand = base.substr(0, i) + base.substr(i + 1);
            addIfValid(cand);
        }
    }

    // 6. Single stray inserted character inside 4+ char invalid token (e.g. zhonhg->zhong, zxhong->zhong)
    if (base.size() >= 4) {
        for (size_t i = 0; i < base.size(); ++i) {
            std::string cand = base.substr(0, i) + base.substr(i + 1);
            if (cand.size() >= 3) {
                addIfValid(cand);
            }
        }
    }

    // 7. Trailing 'm' -> 'n' / "mg" -> "ng" adjacent-key typo (e.g. sam->san, zem->zen, xiam->xian, zhomg->zhong)
    if (base.size() >= 2 && base.back() == 'm') {
        addIfValid(base.substr(0, base.size() - 1) + "n");
    }
    if (base.size() >= 3 && base.ends_with("mg")) {
        addIfValid(base.substr(0, base.size() - 2) + "ng");
    }

    return results;
}

std::vector<SegmentPath> PinyinTable::segmentWithTypos(std::string_view input, size_t maxPaths) {
    if (input.empty()) return {};

    struct Chunk {
        std::string_view sv;
        uint16_t offset;
    };
    std::vector<Chunk> chunks;
    size_t pos = 0;
    while (pos < input.size()) {
        if (input[pos] == '\'') {
            ++pos;
            continue;
        }
        size_t next = input.find('\'', pos);
        if (next == std::string_view::npos) next = input.size();
        if (next > pos) {
            chunks.push_back({input.substr(pos, next - pos), static_cast<uint16_t>(pos)});
        }
        pos = next;
    }
    if (chunks.empty()) return {};

    auto segmentChunkTypo = [](std::string_view sv, uint16_t baseOffset, size_t beamWidth) -> std::vector<SegmentPath> {
        const size_t n = sv.size();
        struct State {
            std::vector<SyllableToken> tokens;
            int score = 0;
            uint8_t typoCount = 0;
        };
        std::vector<std::vector<State>> dp(n + 1);
        dp[0].push_back(State{{}, 0, 0});

        for (size_t i = 0; i < n; ++i) {
            if (dp[i].empty()) continue;

            // Keep DP compact
            if (dp[i].size() > beamWidth * 3) {
                std::sort(dp[i].begin(), dp[i].end(), [](const State& a, const State& b) {
                    return a.score > b.score;
                });
                dp[i].resize(beamWidth * 3);
            }

            // Valid syllables
            const size_t maxValidLen = std::min<size_t>(6, n - i);
            for (size_t len = maxValidLen; len >= 1; --len) {
                std::string_view sub = sv.substr(i, len);
                std::string normSub = canonicalizeSyllable(sub);
                if (syllableSet().count(normSub) == 0) continue;
                int stepScore = static_cast<int>(len * 14) - 8;
                if (len == 1 && isVowel(sub[0])) stepScore -= 6;
                SyllableToken tok{std::move(normSub), false, static_cast<uint16_t>(baseOffset + i), static_cast<uint16_t>(len)};
                for (const auto& prev : dp[i]) {
                    State nxt = prev;
                    nxt.tokens.push_back(tok);
                    nxt.score += stepScore;
                    dp[i + len].push_back(std::move(nxt));
                }
            }

            // Typo-corrected syllables (allow at most 2 typos per chunk)
            const size_t maxTypoLen = std::min<size_t>(7, n - i);
            for (size_t len = 3; len <= maxTypoLen; ++len) {
                std::string_view sub = sv.substr(i, len);
                if (isValidSyllable(sub)) continue;
                // Do not "correct" a valid incomplete prefix at the very end of the chunk (e.g. typing "zhon")
                // except non-prefix typos like "zhogn" or middle tokens before another syllable.
                bool atEnd = (i + len == n);
                if (atEnd && isSyllablePrefix(sub)) continue;

                auto fixes = suggestSyllableTypos(sub);
                if (fixes.empty()) continue;
                for (size_t fi = 0; fi < std::min<size_t>(fixes.size(), 2); ++fi) {
                    const std::string& fixed = fixes[fi];
                    int stepScore = static_cast<int>(fixed.size() * 14) - 28 - static_cast<int>(fi * 4);
                    SyllableToken tok{fixed, false, static_cast<uint16_t>(baseOffset + i), static_cast<uint16_t>(len)};
                    for (const auto& prev : dp[i]) {
                        if (prev.typoCount >= 2) continue;
                        State nxt = prev;
                        nxt.tokens.push_back(tok);
                        nxt.score += stepScore;
                        nxt.typoCount = static_cast<uint8_t>(prev.typoCount + 1);
                        dp[i + len].push_back(std::move(nxt));
                    }
                }
            }

            // Trailing partial initial only at end (not mid-word)
            for (size_t len = 1; len <= std::min<size_t>(2, n - i); ++len) {
                if (i + len != n) continue;
                std::string_view sub = sv.substr(i, len);
                std::string normSub = canonicalizeSyllable(sub);
                if (syllableSet().count(normSub) > 0) continue;
                if (isValidInitial(normSub) || prefixSet().count(normSub) > 0) {
                    SyllableToken tok{std::move(normSub), true, static_cast<uint16_t>(baseOffset + i), static_cast<uint16_t>(len)};
                    for (const auto& prev : dp[i]) {
                        State nxt = prev;
                        nxt.tokens.push_back(tok);
                        nxt.score += static_cast<int>(len * 4) - 10;
                        dp[i + len].push_back(std::move(nxt));
                    }
                }
            }
        }

        std::vector<SegmentPath> out;
        std::sort(dp[n].begin(), dp[n].end(), [](const State& a, const State& b) {
            if (a.typoCount != b.typoCount) return a.typoCount < b.typoCount;
            if (a.score != b.score) return a.score > b.score;
            return a.tokens.size() < b.tokens.size();
        });
        std::unordered_set<std::string> seen;
        for (auto& st : dp[n]) {
            SegmentPath p{std::move(st.tokens), st.score, st.typoCount > 0};
            if (seen.insert(p.joinedKey()).second) {
                out.push_back(std::move(p));
                if (out.size() >= beamWidth) break;
            }
        }
        return out;
    };

    std::vector<SegmentPath> combined = segmentChunkTypo(chunks[0].sv, chunks[0].offset, maxPaths);
    for (size_t c = 1; c < chunks.size(); ++c) {
        auto subPaths = segmentChunkTypo(chunks[c].sv, chunks[c].offset, maxPaths);
        std::vector<SegmentPath> nextCombined;
        for (const auto& a : combined) {
            for (const auto& b : subPaths) {
                SegmentPath merged = a;
                merged.tokens.insert(merged.tokens.end(), b.tokens.begin(), b.tokens.end());
                merged.score += b.score;
                merged.hasTypo = merged.hasTypo || b.hasTypo;
                nextCombined.push_back(std::move(merged));
            }
        }
        std::sort(nextCombined.begin(), nextCombined.end(), [](const SegmentPath& a, const SegmentPath& b) {
            return a.score > b.score;
        });
        if (nextCombined.size() > maxPaths) nextCombined.resize(maxPaths);
        combined = std::move(nextCombined);
    }

    combined.erase(std::remove_if(combined.begin(), combined.end(), [](const SegmentPath& p) {
        return !p.hasTypo;
    }), combined.end());
    return combined;
}

std::vector<std::vector<std::string>> PinyinTable::expandFuzzyTokens(
    const std::vector<SyllableToken>& tokens,
    const FuzzyConfig& fuzzy,
    size_t maxCombinations) {
    if (tokens.empty() || maxCombinations == 0) return {};

    std::vector<std::vector<std::string>> perToken;
    perToken.reserve(tokens.size());
    bool anyAlternative = false;
    for (const auto& t : tokens) {
        if (t.isPartial) {
            perToken.push_back({t.text});
        } else {
            auto ex = expandFuzzy(t.text, fuzzy);
            if (ex.size() > 1) anyAlternative = true;
            perToken.push_back(std::move(ex));
        }
    }
    if (!anyAlternative) return {};

    std::vector<std::vector<std::string>> combos;
    combos.push_back({});
    for (const auto& options : perToken) {
        std::vector<std::vector<std::string>> next;
        for (const auto& prefix : combos) {
            for (const auto& opt : options) {
                auto copy = prefix;
                copy.push_back(opt);
                next.push_back(std::move(copy));
                if (next.size() >= maxCombinations + 1) break;
            }
            if (next.size() >= maxCombinations + 1) break;
        }
        combos = std::move(next);
    }

    // Remove the exact original sequence (always first because expandFuzzy puts original first)
    if (!combos.empty()) {
        std::vector<std::string> orig;
        for (const auto& t : tokens) orig.push_back(t.text);
        combos.erase(std::remove(combos.begin(), combos.end(), orig), combos.end());
    }
    if (combos.size() > maxCombinations) {
        combos.resize(maxCombinations);
    }
    return combos;
}

} // namespace liteime::engine
