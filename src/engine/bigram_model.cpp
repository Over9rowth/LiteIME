#include "engine/bigram_model.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>

namespace liteime::engine {

namespace {

uint64_t fnv1a64(std::string_view sv, uint64_t seed) {
    uint64_t h = 14695981039346656037ULL ^ seed;
    for (unsigned char c : sv) {
        h ^= static_cast<uint64_t>(c);
        h *= 1099511628211ULL;
    }
    return h;
}

uint64_t splitMix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

struct BuiltinBigram {
    const char* prev;
    const char* cur;
    float bonus;
};

constexpr BuiltinBigram kBuiltinBigrams[] = {
    // High-frequency pronouns / subjects + predicates / auxiliaries
    {"我", "在", 2.2f}, {"我", "是", 2.0f}, {"我", "想", 2.1f}, {"我", "要", 1.9f}, {"我", "会", 1.9f},
    {"我", "们", 2.5f}, {"我", "去", 1.8f}, {"我", "看", 1.7f}, {"我", "说", 1.8f}, {"我", "觉得", 2.3f},
    {"你", "好", 2.6f}, {"你", "们", 2.5f}, {"你", "是", 2.0f}, {"你", "在", 2.0f}, {"你", "想", 2.0f},
    {"你", "要", 1.9f}, {"你", "看", 1.8f}, {"你", "觉得", 2.2f},
    {"他", "们", 2.5f}, {"他", "是", 2.0f}, {"他", "在", 2.0f}, {"她", "们", 2.4f}, {"它", "们", 2.2f},

    // Verbs + objects / complements (homophone disambiguation anchors)
    {"在", "写", 2.2f}, {"在", "做", 2.2f}, {"在", "看", 2.0f}, {"在", "家", 2.1f}, {"在", "这", 2.0f},
    {"在", "那", 1.9f}, {"在", "网上", 2.2f}, {"在", "这里", 2.2f}, {"在", "那里", 2.1f},
    {"写", "一个", 2.3f}, {"写", "代码", 2.6f}, {"写", "文章", 2.3f}, {"写", "字", 2.1f},
    {"做", "一个", 2.2f}, {"做", "事情", 2.4f}, {"做", "什么", 2.5f}, {"做", "完", 2.2f}, {"做", "好", 2.2f},
    {"有", "事", 2.4f}, {"有", "时候", 2.5f}, {"有", "一点", 2.4f}, {"有", "点", 2.2f}, {"有", "问题", 2.5f},
    {"有", "什么", 2.4f}, {"有", "没有", 2.6f}, {"有", "一个", 2.3f}, {"有", "很多", 2.3f},
    {"没", "事", 2.6f}, {"没", "关系", 2.6f}, {"没", "问题", 2.6f}, {"没", "办法", 2.5f}, {"没", "想到", 2.4f},
    {"没有", "事", 2.2f}, {"没有", "办法", 2.5f}, {"没有", "问题", 2.5f}, {"没有", "时间", 2.4f},

    // Quantifiers + nouns / common collocations
    {"一", "个", 2.6f}, {"一", "些", 2.4f}, {"一", "点", 2.4f}, {"一", "下", 2.5f}, {"一", "起", 2.5f},
    {"一", "直", 2.4f}, {"一个", "人", 2.6f}, {"一个", "问题", 2.3f}, {"一个", "输入法", 2.5f}, {"一个", "例子", 2.3f},
    {"这", "个", 2.6f}, {"这", "样", 2.5f}, {"这", "里", 2.5f}, {"这", "些", 2.4f}, {"这", "是", 2.4f},
    {"那", "个", 2.5f}, {"那", "么", 2.5f}, {"那", "里", 2.4f}, {"那", "些", 2.3f}, {"那", "是", 2.3f},
    {"这", "次", 2.3f}, {"这", "件事", 2.5f}, {"做", "这件事", 2.4f},

    // Modifiers / adverbs + verbs / adjectives
    {"很", "好", 2.5f}, {"很", "多", 2.4f}, {"很", "快", 2.3f}, {"很", "高", 2.1f}, {"很", "大", 2.2f},
    {"真", "好", 2.4f}, {"真", "的", 2.5f}, {"天气", "真好", 2.6f}, {"今天", "天气", 2.6f},
    {"非常", "好", 2.4f}, {"特别", "好", 2.3f}, {"怎么", "样", 2.6f}, {"怎么", "办", 2.6f},
    {"为什么", "会", 2.2f}, {"为什么", "要", 2.2f}, {"为什么", "不", 2.2f},
    {"不", "是", 2.4f}, {"不", "会", 2.3f}, {"不", "能", 2.3f}, {"不", "要", 2.3f}, {"不", "知道", 2.5f},
    {"就", "是", 2.5f}, {"就", "在", 2.2f}, {"就", "会", 2.2f}, {"就", "能", 2.2f}, {"就", "这样", 2.4f},
    {"还", "是", 2.4f}, {"还", "有", 2.4f}, {"还", "没", 2.3f}, {"还", "可以", 2.4f},
    {"都", "是", 2.4f}, {"都", "有", 2.3f}, {"都", "可以", 2.4f}, {"也", "是", 2.4f}, {"也", "可以", 2.4f},

    // Structural particles & connectors
    {"的", "时候", 2.6f}, {"的", "问题", 2.5f}, {"的", "话", 2.3f}, {"的", "东西", 2.3f}, {"的", "意思", 2.4f},
    {"因为", "我", 2.0f}, {"因为", "这", 2.0f}, {"所以", "我们", 2.2f}, {"如果", "需要", 2.4f},
    {"如果", "有", 2.3f}, {"如果", "没有", 2.4f}, {"如果", "可以", 2.3f}, {"但是", "我", 2.1f},
    {"虽然", "但是", 2.1f}, {"已经", "完成", 2.4f}, {"应该", "可以", 2.4f}, {"应该", "是", 2.3f},
    {"可以", "用", 2.2f}, {"可以", "直接", 2.3f}, {"可以", "通过", 2.3f}, {"需要", "注意", 2.4f},

    // Everyday digital / dev / daily expressions
    {"输入法", "的", 2.1f}, {"写", "程序", 2.4f}, {"运行", "一下", 2.2f}, {"打开", "设置", 2.4f},
    {"安装", "包", 2.3f}, {"测试", "通过", 2.4f}, {"一下", "吧", 2.2f}, {"看", "一下", 2.5f},
    {"试", "一下", 2.5f}, {"等", "一下", 2.5f}, {"想", "一下", 2.4f}, {"问", "一下", 2.4f},
    {"怎么", "用", 2.3f}, {"什么", "时候", 2.6f}, {"什么", "意思", 2.5f}, {"什么", "东西", 2.4f},
    {"什么", "情况", 2.4f}, {"现在", "开始", 2.3f}, {"今天", "晚上", 2.4f}, {"明天", "早上", 2.4f},
    {"昨天", "晚上", 2.4f}, {"下", "一步", 2.5f}, {"上", "一步", 2.4f}, {"第", "一次", 2.5f},
    {"第", "一个", 2.5f}, {"最后", "一个", 2.4f}, {"每", "个人", 2.5f}, {"每", "天", 2.4f},
};

} // namespace

BigramModel::BigramModel() {
    loadBuiltins();
}

uint64_t BigramModel::makePairKey(std::string_view prevWord, std::string_view curWord) {
    uint64_t h1 = fnv1a64(prevWord, 0x243F6A8885A308D3ULL);
    uint64_t h2 = fnv1a64(curWord, 0x13198A2E03707344ULL);
    return splitMix64(h1) ^ ((h2 << 13) | (h2 >> 51));
}

void BigramModel::clear() {
    pairs_.clear();
}

void BigramModel::loadBuiltins() {
    pairs_.reserve(std::max<size_t>(pairs_.size(), 512));
    for (const auto& item : kBuiltinBigrams) {
        addPair(item.prev, item.cur, item.bonus);
    }
}

void BigramModel::addPair(std::string_view prevWord, std::string_view curWord, float bonus) {
    if (prevWord.empty() || curWord.empty() || bonus <= 0.0f) return;
    uint64_t key = makePairKey(prevWord, curWord);
    auto it = pairs_.find(key);
    if (it == pairs_.end() || bonus > it->second) {
        pairs_[key] = bonus;
    }
}

double BigramModel::scoreTransition(std::string_view prevWord, std::string_view curWord) const {
    if (prevWord.empty() || curWord.empty() || pairs_.empty()) return 0.0;
    auto it = pairs_.find(makePairKey(prevWord, curWord));
    return (it != pairs_.end()) ? static_cast<double>(it->second) : 0.0;
}

bool BigramModel::loadFile(const std::filesystem::path& path) {
    std::ifstream ifs(path);
    if (!ifs) return false;
    std::string line;
    size_t loaded = 0;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        size_t t2 = line.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;
        std::string_view prevW(line.data(), t1);
        std::string_view curW(line.data() + t1 + 1, t2 - t1 - 1);
        std::string_view valStr(line.data() + t2 + 1, line.size() - t2 - 1);
        if (prevW.empty() || curW.empty() || valStr.empty()) continue;
        float val = 0.0f;
        try {
            val = std::stof(std::string(valStr));
        } catch (...) {
            continue;
        }
        // If file stores integer frequencies >= 10, map to [1.0, 4.0] log bonus; else direct bonus
        float bonus = (val >= 10.0f)
            ? std::clamp(0.85f + std::log10(val) * 0.55f, 0.8f, 4.0f)
            : std::clamp(val, 0.1f, 4.5f);
        addPair(prevW, curW, bonus);
        ++loaded;
    }
    return loaded > 0;
}

} // namespace liteime::engine
