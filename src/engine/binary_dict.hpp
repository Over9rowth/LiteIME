#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace liteime::engine {

#pragma pack(push, 1)
struct DictHeader {
    uint32_t magic = 0x4C494D31; // 'LIM1'
    uint32_t version = 1;
    uint32_t phraseCount = 0;
    uint32_t jianpinCount = 0;
    uint32_t englishCount = 0;
    uint32_t stringPoolSize = 0;
    uint64_t phraseOffset = 0;
    uint64_t jianpinOffset = 0;
    uint64_t englishOffset = 0;
    uint64_t stringPoolOffset = 0;
};

struct PhraseEntry {
    uint32_t pinyinOff;
    uint32_t wordOff;
    uint32_t freq;
    uint16_t syllableCount;
    uint16_t flags;
};

struct JianpinEntry {
    uint32_t initialsOff;
    uint32_t phraseIdx;
    uint32_t freq;
};

struct EnglishEntry {
    uint32_t lowerOff;
    uint32_t displayOff;
    uint32_t freq;
    uint32_t reserved;
};
#pragma pack(pop)

struct DictCandidate {
    std::string word;
    std::string pinyin;
    uint32_t freq = 0;
    uint16_t syllableCount = 0;
};

struct RawPhraseRecord {
    std::string word;
    std::string pinyin; // space-separated lowercase syllables
    uint32_t freq = 1;
    uint16_t syllableCount = 1;
};

struct RawEnglishRecord {
    std::string lower;
    std::string display;
    uint32_t freq = 1;
};

class BinaryDict {
public:
    BinaryDict() = default;
    ~BinaryDict();

    BinaryDict(const BinaryDict&) = delete;
    BinaryDict& operator=(const BinaryDict&) = delete;

    bool loadFile(const std::filesystem::path& path);
    bool loadFromMemory(std::vector<uint8_t> buffer);
    void unload();

    bool isLoaded() const { return base_ != nullptr; }

    // Exact lookup for a space-separated pinyin key (e.g. "ni" or "ni hao")
    std::vector<DictCandidate> lookupExact(std::string_view pinyinKey, size_t limit = 32) const;

    // Prefix lookup for partial last syllable (e.g. "ni h" matches "ni hao", "ni hui")
    std::vector<DictCandidate> lookupPinyinPrefix(std::string_view pinyinPrefix, size_t limit = 16) const;

    // Jianpin initials lookup (e.g. "nh" -> "你好", "srf" -> "输入法")
    std::vector<DictCandidate> lookupJianpin(std::string_view initials, size_t limit = 16) const;

    // English prefix lookup (case-insensitive)
    std::vector<DictCandidate> lookupEnglishPrefix(std::string_view prefixLower, size_t limit = 12) const;

    uint32_t phraseCount() const { return header_ ? header_->phraseCount : 0; }
    uint32_t jianpinCount() const { return header_ ? header_->jianpinCount : 0; }
    uint32_t englishCount() const { return header_ ? header_->englishCount : 0; }

    // Compiles raw records into binary dictionary file.
    static bool compileToFile(const std::filesystem::path& outPath,
                              std::vector<RawPhraseRecord> phrases,
                              std::vector<RawEnglishRecord> english);

    // Parses a Rime .dict.yaml or .tsv file into phrase/english records.
    static bool parseDictYaml(const std::filesystem::path& path,
                              std::vector<RawPhraseRecord>& outPhrases,
                              std::vector<RawEnglishRecord>& outEnglish,
                              bool isEnglishDict);

private:
    std::string_view strAt(uint32_t off) const;

    const uint8_t* base_ = nullptr;
    size_t size_ = 0;
    const DictHeader* header_ = nullptr;
    const PhraseEntry* phrases_ = nullptr;
    const JianpinEntry* jianpin_ = nullptr;
    const EnglishEntry* english_ = nullptr;
    const char* strings_ = nullptr;

    std::vector<uint8_t> ownedBuffer_;
#ifdef _WIN32
    void* fileHandle_ = nullptr;
    void* mappingHandle_ = nullptr;
#endif
};

} // namespace liteime::engine
