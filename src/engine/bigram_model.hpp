#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <unordered_map>

namespace liteime::engine {

class BigramModel {
public:
    BigramModel();

    void clear();
    void loadBuiltins();
    bool loadFile(const std::filesystem::path& path);

    void addPair(std::string_view prevWord, std::string_view curWord, float bonus);
    double scoreTransition(std::string_view prevWord, std::string_view curWord) const;

    size_t size() const { return pairs_.size(); }

private:
    static uint64_t makePairKey(std::string_view prevWord, std::string_view curWord);
    std::unordered_map<uint64_t, float> pairs_;
};

} // namespace liteime::engine
