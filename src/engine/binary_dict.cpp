#include "engine/binary_dict.hpp"
#include "engine/pinyin_table.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <unordered_map>

namespace liteime::engine {

namespace {

std::string normalizePinyinField(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    bool lastSpace = true;
    for (size_t i = 0; i < raw.size(); ++i) {
        uint8_t c = static_cast<uint8_t>(raw[i]);
        // Normalize UTF-8 'ü' (0xC3 0xBC) -> 'v'
        if (c == 0xC3 && i + 1 < raw.size() && static_cast<uint8_t>(raw[i + 1]) == 0xBC) {
            out.push_back('v');
            lastSpace = false;
            ++i;
            continue;
        }
        if (c >= 'A' && c <= 'Z') c = static_cast<uint8_t>(c - 'A' + 'a');
        if (c >= 'a' && c <= 'z') {
            out.push_back(static_cast<char>(c));
            lastSpace = false;
        } else if (c == ' ' || c == '\'' || c == '\t') {
            if (!lastSpace) {
                out.push_back(' ');
                lastSpace = true;
            }
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

uint16_t countSyllables(std::string_view pinyin) {
    if (pinyin.empty()) return 0;
    uint16_t cnt = 1;
    for (char c : pinyin) {
        if (c == ' ') ++cnt;
    }
    return cnt;
}

std::string makeInitials(std::string_view pinyin) {
    std::string out;
    bool takeNext = true;
    for (char c : pinyin) {
        if (c == ' ') {
            takeNext = true;
        } else if (takeNext) {
            out.push_back(c);
            takeNext = false;
        }
    }
    return out;
}

std::string toLowerAscii(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

} // namespace

BinaryDict::~BinaryDict() {
    unload();
}

void BinaryDict::unload() {
#ifdef _WIN32
    if (base_ && ownedBuffer_.empty()) {
        UnmapViewOfFile(base_);
    }
    if (mappingHandle_) {
        CloseHandle(static_cast<HANDLE>(mappingHandle_));
        mappingHandle_ = nullptr;
    }
    if (fileHandle_) {
        CloseHandle(static_cast<HANDLE>(fileHandle_));
        fileHandle_ = nullptr;
    }
#endif
    ownedBuffer_.clear();
    base_ = nullptr;
    size_ = 0;
    header_ = nullptr;
    phrases_ = nullptr;
    jianpin_ = nullptr;
    english_ = nullptr;
    strings_ = nullptr;
}

bool BinaryDict::validateAndBindPointers() {
    if (!base_ || size_ < sizeof(DictHeader)) {
        unload();
        return false;
    }
    header_ = reinterpret_cast<const DictHeader*>(base_);
    if (header_->magic != 0x4C494D31 || header_->version != 1) {
        unload();
        return false;
    }

    auto fitsTable = [this](uint64_t offset, uint64_t count, size_t elemSize) {
        if (offset > size_) return false;
        const uint64_t avail = size_ - offset;
        return elemSize > 0 && count <= avail / elemSize;
    };
    const bool ok =
        fitsTable(header_->phraseOffset, header_->phraseCount, sizeof(PhraseEntry)) &&
        fitsTable(header_->jianpinOffset, header_->jianpinCount, sizeof(JianpinEntry)) &&
        fitsTable(header_->englishOffset, header_->englishCount, sizeof(EnglishEntry)) &&
        header_->stringPoolOffset <= size_ &&
        header_->stringPoolSize <= (size_ - header_->stringPoolOffset);
    if (!ok) {
        unload();
        return false;
    }

    phrases_ = reinterpret_cast<const PhraseEntry*>(base_ + header_->phraseOffset);
    jianpin_ = reinterpret_cast<const JianpinEntry*>(base_ + header_->jianpinOffset);
    english_ = reinterpret_cast<const EnglishEntry*>(base_ + header_->englishOffset);
    strings_ = reinterpret_cast<const char*>(base_ + header_->stringPoolOffset);
    return true;
}

bool BinaryDict::loadFile(const std::filesystem::path& path) {
    unload();
#ifdef _WIN32
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart < static_cast<LONGLONG>(sizeof(DictHeader))) {
        CloseHandle(hFile);
        return false;
    }

    HANDLE hMap = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!hMap) {
        CloseHandle(hFile);
        return false;
    }

    const void* view = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!view) {
        CloseHandle(hMap);
        CloseHandle(hFile);
        return false;
    }

    fileHandle_ = hFile;
    mappingHandle_ = hMap;
    base_ = static_cast<const uint8_t*>(view);
    size_ = static_cast<size_t>(fileSize.QuadPart);
#else
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) return false;
    auto sz = ifs.tellg();
    if (sz < static_cast<std::streamoff>(sizeof(DictHeader))) return false;
    ownedBuffer_.resize(static_cast<size_t>(sz));
    ifs.seekg(0);
    if (!ifs.read(reinterpret_cast<char*>(ownedBuffer_.data()), sz)) return false;
    base_ = ownedBuffer_.data();
    size_ = ownedBuffer_.size();
#endif

    return validateAndBindPointers();
}

bool BinaryDict::loadFromMemory(std::vector<uint8_t> buffer) {
    unload();
    if (buffer.size() < sizeof(DictHeader)) return false;
    ownedBuffer_ = std::move(buffer);
    base_ = ownedBuffer_.data();
    size_ = ownedBuffer_.size();
    return validateAndBindPointers();
}

std::string_view BinaryDict::strAt(uint32_t off) const {
    if (!strings_ || off >= header_->stringPoolSize) return {};
    const char* p = strings_ + off;
    const size_t maxLen = header_->stringPoolSize - off;
    return std::string_view(p, strnlen(p, maxLen));
}

std::vector<DictCandidate> BinaryDict::lookupExact(std::string_view pinyinKey, size_t limit) const {
    if (!isLoaded() || header_->phraseCount == 0 || pinyinKey.empty()) return {};

    auto it = std::lower_bound(phrases_, phrases_ + header_->phraseCount, pinyinKey,
        [this](const PhraseEntry& e, std::string_view key) {
            return strAt(e.pinyinOff) < key;
        });

    std::vector<DictCandidate> out;
    while (it != phrases_ + header_->phraseCount && strAt(it->pinyinOff) == pinyinKey) {
        out.push_back(DictCandidate{
            std::string(strAt(it->wordOff)),
            std::string(pinyinKey),
            it->freq,
            it->syllableCount
        });
        if (out.size() >= limit) break;
        ++it;
    }
    return out;
}

std::vector<DictCandidate> BinaryDict::lookupPinyinPrefix(std::string_view pinyinPrefix, size_t limit) const {
    if (!isLoaded() || header_->phraseCount == 0 || pinyinPrefix.empty()) return {};

    auto it = std::lower_bound(phrases_, phrases_ + header_->phraseCount, pinyinPrefix,
        [this](const PhraseEntry& e, std::string_view key) {
            return strAt(e.pinyinOff) < key;
        });

    std::vector<DictCandidate> out;
    size_t scanned = 0;
    constexpr size_t kMaxScan = 4096;
    while (it != phrases_ + header_->phraseCount && scanned < kMaxScan) {
        std::string_view py = strAt(it->pinyinOff);
        if (!py.starts_with(pinyinPrefix)) break;
        out.push_back(DictCandidate{
            std::string(strAt(it->wordOff)),
            std::string(py),
            it->freq,
            it->syllableCount
        });
        ++it;
        ++scanned;
    }
    std::sort(out.begin(), out.end(), [](const DictCandidate& a, const DictCandidate& b) {
        return a.freq > b.freq;
    });
    if (out.size() > limit) out.resize(limit);
    return out;
}

std::vector<DictCandidate> BinaryDict::lookupJianpin(std::string_view initials, size_t limit) const {
    if (!isLoaded() || header_->jianpinCount == 0 || initials.size() < 2) return {};

    auto it = std::lower_bound(jianpin_, jianpin_ + header_->jianpinCount, initials,
        [this](const JianpinEntry& e, std::string_view key) {
            return strAt(e.initialsOff) < key;
        });

    std::vector<DictCandidate> out;
    while (it != jianpin_ + header_->jianpinCount && strAt(it->initialsOff) == initials) {
        if (it->phraseIdx >= header_->phraseCount) {
            ++it;
            continue;
        }
        const PhraseEntry& p = phrases_[it->phraseIdx];
        out.push_back(DictCandidate{
            std::string(strAt(p.wordOff)),
            std::string(strAt(p.pinyinOff)),
            it->freq,
            p.syllableCount
        });
        if (out.size() >= limit) break;
        ++it;
    }
    return out;
}

std::vector<DictCandidate> BinaryDict::lookupEnglishPrefix(std::string_view prefixLower, size_t limit) const {
    if (!isLoaded() || header_->englishCount == 0 || prefixLower.empty()) return {};

    auto it = std::lower_bound(english_, english_ + header_->englishCount, prefixLower,
        [this](const EnglishEntry& e, std::string_view key) {
            return strAt(e.lowerOff) < key;
        });

    std::vector<DictCandidate> out;
    size_t scanned = 0;
    constexpr size_t kMaxScan = 600;
    while (it != english_ + header_->englishCount && scanned < kMaxScan) {
        std::string_view lower = strAt(it->lowerOff);
        if (!lower.starts_with(prefixLower)) break;
        // Boost exact length match strongly so "python" beats "pythonic"
        uint32_t score = it->freq;
        if (lower.size() == prefixLower.size()) {
            score = (score > 0x7FFFFFFFu) ? 0xFFFFFFFFu : (score + 100000000u);
        }
        out.push_back(DictCandidate{
            std::string(strAt(it->displayOff)),
            std::string(lower),
            score,
            1
        });
        ++it;
        ++scanned;
    }

    std::sort(out.begin(), out.end(), [](const DictCandidate& a, const DictCandidate& b) {
        if (a.freq != b.freq) return a.freq > b.freq;
        return a.word.size() < b.word.size();
    });
    if (out.size() > limit) out.resize(limit);
    return out;
}

bool BinaryDict::compileToFile(const std::filesystem::path& outPath,
                               std::vector<RawPhraseRecord> phrases,
                               std::vector<RawEnglishRecord> english) {
    // Deduplicate phrases by (pinyin, word), keeping max freq
    std::sort(phrases.begin(), phrases.end(), [](const RawPhraseRecord& a, const RawPhraseRecord& b) {
        if (a.pinyin != b.pinyin) return a.pinyin < b.pinyin;
        if (a.word != b.word) return a.word < b.word;
        return a.freq > b.freq;
    });

    std::vector<RawPhraseRecord> dedupPhrases;
    dedupPhrases.reserve(phrases.size());
    for (auto& rec : phrases) {
        if (rec.pinyin.empty() || rec.word.empty()) continue;
        if (!dedupPhrases.empty() &&
            dedupPhrases.back().pinyin == rec.pinyin &&
            dedupPhrases.back().word == rec.word) {
            dedupPhrases.back().freq = std::max(dedupPhrases.back().freq, rec.freq);
        } else {
            dedupPhrases.push_back(std::move(rec));
        }
    }

    // Sort final phrase table by (pinyin ASC, freq DESC, word ASC)
    std::sort(dedupPhrases.begin(), dedupPhrases.end(), [](const RawPhraseRecord& a, const RawPhraseRecord& b) {
        if (a.pinyin != b.pinyin) return a.pinyin < b.pinyin;
        if (a.freq != b.freq) return a.freq > b.freq;
        return a.word < b.word;
    });

    // Deduplicate english by lowercase key, keeping max freq & canonical display
    for (auto& e : english) {
        e.lower = toLowerAscii(e.lower);
    }
    std::sort(english.begin(), english.end(), [](const RawEnglishRecord& a, const RawEnglishRecord& b) {
        if (a.lower != b.lower) return a.lower < b.lower;
        return a.freq > b.freq;
    });
    std::vector<RawEnglishRecord> dedupEnglish;
    dedupEnglish.reserve(english.size());
    for (auto& rec : english) {
        if (rec.lower.empty() || rec.display.empty()) continue;
        if (!dedupEnglish.empty() && dedupEnglish.back().lower == rec.lower) {
            if (rec.freq > dedupEnglish.back().freq) {
                dedupEnglish.back() = std::move(rec);
            }
        } else {
            dedupEnglish.push_back(std::move(rec));
        }
    }

    // Build interned string pool
    std::string stringPool;
    stringPool.reserve(dedupPhrases.size() * 18 + dedupEnglish.size() * 14);
    std::unordered_map<std::string, uint32_t> internMap;
    internMap.reserve(dedupPhrases.size() + dedupEnglish.size());

    auto intern = [&](const std::string& s) -> uint32_t {
        auto it = internMap.find(s);
        if (it != internMap.end()) return it->second;
        uint32_t off = static_cast<uint32_t>(stringPool.size());
        stringPool.append(s);
        stringPool.push_back('\0');
        internMap.emplace(s, off);
        return off;
    };

    std::vector<PhraseEntry> phraseEntries;
    phraseEntries.reserve(dedupPhrases.size());

    struct TempJianpin {
        std::string initials;
        uint32_t phraseIdx;
        uint32_t freq;
    };
    std::vector<TempJianpin> tempJianpin;

    for (uint32_t i = 0; i < dedupPhrases.size(); ++i) {
        const auto& r = dedupPhrases[i];
        PhraseEntry pe{};
        pe.pinyinOff = intern(r.pinyin);
        pe.wordOff = intern(r.word);
        pe.freq = r.freq;
        pe.syllableCount = r.syllableCount;
        pe.flags = 0;
        phraseEntries.push_back(pe);

        if (r.syllableCount >= 2 && r.syllableCount <= 8 && r.freq >= 10) {
            std::string init = makeInitials(r.pinyin);
            if (init.size() >= 2) {
                tempJianpin.push_back({std::move(init), i, r.freq});
            }
        }
    }

    std::sort(tempJianpin.begin(), tempJianpin.end(), [](const TempJianpin& a, const TempJianpin& b) {
        if (a.initials != b.initials) return a.initials < b.initials;
        return a.freq > b.freq;
    });

    std::vector<JianpinEntry> jianpinEntries;
    jianpinEntries.reserve(tempJianpin.size());
    for (const auto& j : tempJianpin) {
        jianpinEntries.push_back(JianpinEntry{intern(j.initials), j.phraseIdx, j.freq});
    }

    std::vector<EnglishEntry> englishEntries;
    englishEntries.reserve(dedupEnglish.size());
    for (const auto& e : dedupEnglish) {
        englishEntries.push_back(EnglishEntry{intern(e.lower), intern(e.display), e.freq, 0});
    }

    DictHeader hdr{};
    hdr.magic = 0x4C494D31;
    hdr.version = 1;
    hdr.phraseCount = static_cast<uint32_t>(phraseEntries.size());
    hdr.jianpinCount = static_cast<uint32_t>(jianpinEntries.size());
    hdr.englishCount = static_cast<uint32_t>(englishEntries.size());
    hdr.stringPoolSize = static_cast<uint32_t>(stringPool.size());

    uint64_t offset = sizeof(DictHeader);
    hdr.phraseOffset = offset;
    offset += phraseEntries.size() * sizeof(PhraseEntry);
    hdr.jianpinOffset = offset;
    offset += jianpinEntries.size() * sizeof(JianpinEntry);
    hdr.englishOffset = offset;
    offset += englishEntries.size() * sizeof(EnglishEntry);
    hdr.stringPoolOffset = offset;

    std::error_code ec;
    std::filesystem::create_directories(outPath.parent_path(), ec);

    std::ofstream ofs(outPath, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;

    ofs.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    if (!phraseEntries.empty())
        ofs.write(reinterpret_cast<const char*>(phraseEntries.data()), phraseEntries.size() * sizeof(PhraseEntry));
    if (!jianpinEntries.empty())
        ofs.write(reinterpret_cast<const char*>(jianpinEntries.data()), jianpinEntries.size() * sizeof(JianpinEntry));
    if (!englishEntries.empty())
        ofs.write(reinterpret_cast<const char*>(englishEntries.data()), englishEntries.size() * sizeof(EnglishEntry));
    if (!stringPool.empty())
        ofs.write(stringPool.data(), stringPool.size());

    return ofs.good();
}

bool BinaryDict::parseDictYaml(const std::filesystem::path& path,
                               std::vector<RawPhraseRecord>& outPhrases,
                               std::vector<RawEnglishRecord>& outEnglish,
                               bool isEnglishDict) {
    std::ifstream ifs(path);
    if (!ifs) return false;

    std::string line;
    bool hasYamlHeader = false;
    bool pastHeader = false;
    // Detect if file starts with YAML frontmatter "---"
    if (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "---") {
            hasYamlHeader = true;
        } else {
            ifs.clear();
            ifs.seekg(0);
            pastHeader = true;
        }
    }

    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (hasYamlHeader && !pastHeader) {
            if (line == "...") pastHeader = true;
            continue;
        }
        if (line.empty() || line[0] == '#') continue;

        auto isAllDigits = [](const std::string& s) -> bool {
            if (s.empty()) return false;
            for (char c : s) {
                if (c < '0' || c > '9') return false;
            }
            return true;
        };

        auto isValidEnglishToken = [](const std::string& s) -> bool {
            if (s.empty() || s.size() > 48) return false;
            bool hasAlpha = false;
            for (char c : s) {
                if (c >= 'a' && c <= 'z') {
                    hasAlpha = true;
                } else if (!((c >= '0' && c <= '9') || c == '-' || c == '_' || c == '\'' ||
                             c == '.' || c == '+' || c == '#')) {
                    return false;
                }
            }
            return hasAlpha;
        };

        // Split by '\t'
        size_t t1 = line.find('\t');
        if (t1 == std::string::npos) {
            std::string word = line;
            while (!word.empty() && (word.back() == ' ' || word.back() == '\t')) word.pop_back();
            std::string lower = toLowerAscii(word);
            if (isValidEnglishToken(lower)) {
                outEnglish.push_back({std::move(lower), std::move(word), isEnglishDict ? 100u : 50000u});
            }
            continue;
        }

        std::string col1 = line.substr(0, t1);
        size_t t2 = line.find('\t', t1 + 1);
        std::string col2 = (t2 == std::string::npos) ? line.substr(t1 + 1) : line.substr(t1 + 1, t2 - t1 - 1);
        std::string col3 = (t2 == std::string::npos) ? "" : line.substr(t2 + 1);

        // Trim any extra tab column
        if (size_t t3 = col3.find('\t'); t3 != std::string::npos) col3.resize(t3);

        auto parseFreq = [](const std::string& s, uint32_t defVal) -> uint32_t {
            if (s.empty()) return defVal;
            uint64_t v = 0;
            for (char c : s) {
                if (c < '0' || c > '9') return defVal;
                v = v * 10 + static_cast<uint64_t>(c - '0');
            }
            if (v > 0xFFFFFFFFull) v = 0xFFFFFFFFull;
            return static_cast<uint32_t>(v == 0 ? 1 : v);
        };

        std::string lower1 = toLowerAscii(col1);
        const bool autoEnglishLine = !isEnglishDict && isValidEnglishToken(lower1) &&
            (isAllDigits(col2) || toLowerAscii(col2) == lower1);

        if (isEnglishDict || autoEnglishLine) {
            // Supports: word \t freq  OR  word \t code [\t freq]
            uint32_t defFreq = isEnglishDict ? 100u : 50000u;
            uint32_t freq = col3.empty() ? parseFreq(col2, defFreq) : parseFreq(col3, defFreq);
            if (isValidEnglishToken(lower1)) {
                outEnglish.push_back({std::move(lower1), std::move(col1), freq});
            }
        } else {
            std::string py = normalizePinyinField(col2);
            if (py.empty() || col1.empty()) continue;
            uint16_t sylCnt = countSyllables(py);
            uint32_t freq = parseFreq(col3, 50);
            outPhrases.push_back({std::move(col1), std::move(py), freq, sylCnt});
        }
    }
    return true;
}

} // namespace liteime::engine
