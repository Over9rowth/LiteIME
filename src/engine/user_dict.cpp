#include "engine/user_dict.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace liteime::engine {

namespace {

bool atomicWriteTextFile(const std::filesystem::path& path, std::string_view content) {
    if (path.empty()) return false;
    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    std::filesystem::path tmpPath = path;
    tmpPath += L".tmp";
    {
        std::ofstream ofs(tmpPath, std::ios::binary | std::ios::trunc);
        if (!ofs) return false;
        if (!content.empty()) {
            ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        }
        ofs.flush();
        if (!ofs.good()) {
            ofs.close();
            std::filesystem::remove(tmpPath, ec);
            return false;
        }
    }

#ifdef _WIN32
    if (MoveFileExW(tmpPath.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
#endif
    std::filesystem::rename(tmpPath, path, ec);
    if (!ec) return true;
    std::filesystem::remove(tmpPath, ec);
    return false;
}

std::string escapeQuickField(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::string unescapeQuickField(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char nxt = s[i + 1];
            if (nxt == '\\') { out.push_back('\\'); ++i; continue; }
            if (nxt == 't') { out.push_back('\t'); ++i; continue; }
            if (nxt == 'n') { out.push_back('\n'); ++i; continue; }
            if (nxt == 'r') { out.push_back('\r'); ++i; continue; }
        }
        out.push_back(s[i]);
    }
    return out;
}

} // namespace

bool UserDict::load(const std::filesystem::path& path) {
    path_ = path;
    entries_.clear();
    clock_ = 1;
    dirty_ = false;

    std::ifstream ifs(path);
    bool loadedMain = false;
    if (ifs) {
        loadedMain = true;
        std::string line;
        while (std::getline(ifs, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;

            size_t t1 = line.find('\t');
            if (t1 == std::string::npos) continue;
            size_t t2 = line.find('\t', t1 + 1);
            if (t2 == std::string::npos) continue;
            size_t t3 = line.find('\t', t2 + 1);

            std::string key = line.substr(0, t1);
            std::string word = line.substr(t1 + 1, t2 - t1 - 1);
            uint32_t count = 1;
            uint64_t ts = 1;
            try {
                if (t3 == std::string::npos) {
                    count = static_cast<uint32_t>(std::stoul(line.substr(t2 + 1)));
                } else {
                    count = static_cast<uint32_t>(std::stoul(line.substr(t2 + 1, t3 - t2 - 1)));
                    ts = std::stoull(line.substr(t3 + 1));
                }
            } catch (...) {
                continue;
            }
            clock_ = std::max(clock_, ts + 1);
            entries_[std::move(key)].push_back(UserEntry{std::move(word), count, ts});
        }

        for (auto& [k, vec] : entries_) {
            std::sort(vec.begin(), vec.end(), [](const UserEntry& a, const UserEntry& b) {
                if (a.count != b.count) return a.count > b.count;
                return a.lastUsed > b.lastUsed;
            });
        }
    }

    if (!pinsPath().empty()) loadPins(pinsPath());
    if (!quickInputsPath().empty()) loadQuickInputs(quickInputsPath());
    return loadedMain;
}

bool UserDict::save() const {
    if (path_.empty()) return false;
    std::ostringstream oss;
    oss << "# LiteIME User Dictionary (pinyin\\tword\\tcount\\tlastUsed)\n";
    for (const auto& [key, vec] : entries_) {
        for (const auto& e : vec) {
            oss << key << '\t' << e.word << '\t' << e.count << '\t' << e.lastUsed << '\n';
        }
    }
    const bool ok = atomicWriteTextFile(path_, oss.str());
    if (ok) dirty_ = false;
    return ok;
}

void UserDict::recordSelection(std::string_view pinyinKey, std::string_view word) {
    if (pinyinKey.empty() || word.empty()) return;
    auto& vec = entries_[std::string(pinyinKey)];
    ++clock_;
    dirty_ = true;
    for (auto& e : vec) {
        if (e.word == word) {
            ++e.count;
            e.lastUsed = clock_;
            std::sort(vec.begin(), vec.end(), [](const UserEntry& a, const UserEntry& b) {
                if (a.count != b.count) return a.count > b.count;
                return a.lastUsed > b.lastUsed;
            });
            return;
        }
    }
    vec.push_back(UserEntry{std::string(word), 1, clock_});
    std::sort(vec.begin(), vec.end(), [](const UserEntry& a, const UserEntry& b) {
        if (a.count != b.count) return a.count > b.count;
        return a.lastUsed > b.lastUsed;
    });
    if (vec.size() > 32) vec.resize(32);
}

bool UserDict::removeEntry(std::string_view pinyinKey, std::string_view word) {
    auto it = entries_.find(std::string(pinyinKey));
    if (it == entries_.end()) return false;
    auto& vec = it->second;
    auto origSize = vec.size();
    vec.erase(std::remove_if(vec.begin(), vec.end(), [&](const UserEntry& e) {
        return e.word == word;
    }), vec.end());
    if (vec.empty()) entries_.erase(it);
    if (vec.size() != origSize) {
        dirty_ = true;
        return true;
    }
    return false;
}

std::vector<UserEntry> UserDict::lookup(std::string_view pinyinKey) const {
    auto it = entries_.find(std::string(pinyinKey));
    if (it == entries_.end()) return {};
    return it->second;
}

std::vector<std::pair<std::string, UserEntry>> UserDict::lookupSingleSyllablePrefix(std::string_view prefix) const {
    std::vector<std::pair<std::string, UserEntry>> out;
    if (prefix.empty()) return out;
    for (const auto& [key, vec] : entries_) {
        if (key.find(' ') != std::string::npos) continue;
        if (!std::string_view(key).starts_with(prefix)) continue;
        for (const auto& e : vec) {
            out.emplace_back(key, e);
        }
    }
    return out;
}

std::vector<std::pair<std::string, UserEntry>> UserDict::lookupByInitials(std::string_view initials) const {
    std::vector<std::pair<std::string, UserEntry>> out;
    if (initials.size() < 2) return out;
    for (const auto& [key, vec] : entries_) {
        if (key.find(' ') == std::string::npos) continue;
        std::string keyInit;
        bool atStart = true;
        for (char c : key) {
            if (c == ' ') {
                atStart = true;
            } else if (atStart) {
                keyInit.push_back(c);
                atStart = false;
            }
        }
        if (keyInit == initials) {
            for (const auto& e : vec) {
                out.emplace_back(key, e);
            }
        }
    }
    return out;
}

uint32_t UserDict::getBoost(std::string_view pinyinKey, std::string_view word) const {
    auto it = entries_.find(std::string(pinyinKey));
    if (it == entries_.end()) return 0;
    for (const auto& e : it->second) {
        if (e.word == word) {
            return e.count * 500000u;
        }
    }
    return 0;
}

uint32_t UserDict::tierForCount(uint32_t count) {
    uint32_t tier = 0;
    uint32_t threshold = 2;
    uint32_t step = 3;
    while (count >= threshold) {
        ++tier;
        threshold += step;
        if (step < 5) ++step;
    }
    return tier;
}

uint32_t UserDict::getTier(std::string_view pinyinKey, std::string_view word) const {
    auto it = entries_.find(std::string(pinyinKey));
    if (it == entries_.end()) return 0;
    for (const auto& e : it->second) {
        if (e.word == word) {
            return tierForCount(e.count);
        }
    }
    return 0;
}

std::filesystem::path UserDict::pinsPath() const {
    return path_.empty() ? std::filesystem::path{} : (path_.parent_path() / "pins.tsv");
}

std::filesystem::path UserDict::quickInputsPath() const {
    return path_.empty() ? std::filesystem::path{} : (path_.parent_path() / "quick_input.tsv");
}

std::filesystem::path UserDict::configPath() const {
    return path_.empty() ? std::filesystem::path{} : (path_.parent_path() / "config.ini");
}

std::string UserDict::normalizePinKey(std::string_view rawInput) {
    std::string out;
    out.reserve(rawInput.size());
    for (unsigned char ch : rawInput) {
        if (ch == '\'' || ch == ' ') continue;
        if (ch >= 'A' && ch <= 'Z') {
            out.push_back(static_cast<char>(ch - 'A' + 'a'));
        } else {
            out.push_back(static_cast<char>(ch));
        }
    }
    return out;
}

void UserDict::setPin(std::string_view rawKey, int slot, std::string_view word) {
    std::string key = normalizePinKey(rawKey);
    if (key.empty() || word.empty()) return;
    slot = std::clamp(slot, 0, 4);
    auto& vec = pins_[key];
    vec.erase(std::remove_if(vec.begin(), vec.end(), [&](const PinnedEntry& p) {
        return p.slot == slot || p.word == word;
    }), vec.end());
    vec.push_back(PinnedEntry{slot, std::string(word)});
    std::sort(vec.begin(), vec.end(), [](const PinnedEntry& a, const PinnedEntry& b) {
        return a.slot < b.slot;
    });
}

bool UserDict::unpin(std::string_view rawKey, std::string_view word) {
    std::string key = normalizePinKey(rawKey);
    auto it = pins_.find(key);
    if (it == pins_.end()) return false;
    auto& vec = it->second;
    const size_t before = vec.size();
    vec.erase(std::remove_if(vec.begin(), vec.end(), [&](const PinnedEntry& p) {
        return p.word == word;
    }), vec.end());
    if (vec.empty()) pins_.erase(it);
    return vec.size() != before || pins_.find(key) == pins_.end();
}

int UserDict::getPinnedSlot(std::string_view rawKey, std::string_view word) const {
    std::string key = normalizePinKey(rawKey);
    auto it = pins_.find(key);
    if (it == pins_.end()) return -1;
    for (const auto& p : it->second) {
        if (p.word == word) return p.slot;
    }
    return -1;
}

std::vector<PinnedEntry> UserDict::getPins(std::string_view rawKey) const {
    std::string key = normalizePinKey(rawKey);
    auto it = pins_.find(key);
    if (it == pins_.end()) return {};
    return it->second;
}

bool UserDict::loadPins(const std::filesystem::path& path) {
    pins_.clear();
    std::ifstream ifs(path);
    if (!ifs) return false;
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        size_t t2 = line.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;
        std::string key = normalizePinKey(line.substr(0, t1));
        int slot = 0;
        try {
            slot = std::clamp(std::stoi(line.substr(t1 + 1, t2 - t1 - 1)), 0, 4);
        } catch (...) {
            continue;
        }
        std::string word = line.substr(t2 + 1);
        if (!key.empty() && !word.empty()) {
            setPin(key, slot, word);
        }
    }
    return true;
}

bool UserDict::savePins() const {
    auto p = pinsPath();
    if (p.empty()) return false;
    std::ostringstream oss;
    oss << "# LiteIME Pinned Candidates (rawKey\\tslot0..4\\tword)\n";
    for (const auto& [key, vec] : pins_) {
        for (const auto& e : vec) {
            oss << key << '\t' << e.slot << '\t' << e.word << '\n';
        }
    }
    return atomicWriteTextFile(p, oss.str());
}

void UserDict::setQuickInputs(std::vector<QuickInputItem> items) {
    items.erase(std::remove_if(items.begin(), items.end(), [](const QuickInputItem& q) {
        return q.value.empty();
    }), items.end());
    quickInputs_ = std::move(items);
}

std::string UserDict::maskQuickValue(std::string_view value) {
    if (value.empty()) return {};

    // Count UTF-8 codepoints and collect byte end offsets of each codepoint
    std::vector<size_t> cpEnds;
    cpEnds.reserve(16);
    for (size_t i = 0; i < value.size();) {
        unsigned char b = static_cast<unsigned char>(value[i]);
        size_t adv = 1;
        if ((b & 0xE0) == 0xC0) adv = 2;
        else if ((b & 0xF0) == 0xE0) adv = 3;
        else if ((b & 0xF8) == 0xF0) adv = 4;
        i = std::min(value.size(), i + adv);
        cpEnds.push_back(i);
    }

    const size_t total = cpEnds.size();
    if (total <= 1) return "*";

    size_t keep = 1;
    if (total >= 7) keep = 3;
    else if (total >= 4) keep = 2;

    // If an obvious separator ('-', '@', ' ') occurs within chars 2..4, show only the first segment
    for (size_t idx = 1; idx < std::min<size_t>(total, 4); ++idx) {
        size_t start = cpEnds[idx - 1];
        size_t end = cpEnds[idx];
        if (end - start == 1) {
            char c = value[start];
            if (c == '-' || c == '@' || c == ' ' || c == ':') {
                keep = idx;
                break;
            }
        }
    }

    std::string out(value.substr(0, cpEnds[keep - 1]));
    out.append(total <= 3 ? "***" : "****");
    return out;
}

bool UserDict::loadQuickInputs(const std::filesystem::path& path) {
    quickInputs_.clear();
    std::ifstream ifs(path);
    if (!ifs) return false;
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        std::string label = unescapeQuickField(std::string_view(line.data(), t1));
        std::string value = unescapeQuickField(std::string_view(line.data() + t1 + 1, line.size() - t1 - 1));
        if (!value.empty()) {
            quickInputs_.push_back(QuickInputItem{std::move(label), std::move(value)});
        }
    }
    return true;
}

bool UserDict::saveQuickInputs() const {
    auto p = quickInputsPath();
    if (p.empty()) return false;
    std::ostringstream oss;
    oss << "# LiteIME Quick Input Snippets (label\\tvalue)\n";
    for (const auto& q : quickInputs_) {
        oss << escapeQuickField(q.label) << '\t' << escapeQuickField(q.value) << '\n';
    }
    return atomicWriteTextFile(p, oss.str());
}

bool UserDict::loadConfig(const std::filesystem::path& path, EngineConfig& cfg) {
    std::ifstream ifs(path);
    if (!ifs) return false;
    std::string line;
    auto asBool = [](std::string_view v) {
        return v == "1" || v == "true" || v == "TRUE";
    };
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string_view k(line.data(), eq);
        std::string_view v(line.data() + eq + 1, line.size() - eq - 1);
        if (k == "pageSize") {
            try { cfg.pageSize = std::clamp(std::stoi(std::string(v)), 5, 8); } catch (...) {}
        } else if (k == "chineseMode") cfg.chineseMode = asBool(v);
        else if (k == "fullWidth") cfg.fullWidth = asBool(v);
        else if (k == "chinesePunct") cfg.chinesePunct = asBool(v);
        else if (k == "mixedEnglish") cfg.mixedEnglish = asBool(v);
        else if (k == "smartDecimal") cfg.smartDecimal = asBool(v);
        else if (k == "autoUrlEnglish") cfg.autoUrlEnglish = asBool(v);
        else if (k == "shiftTapToggle") cfg.shiftTapToggle = asBool(v);
        else if (k == "showStatusBar") cfg.showStatusBar = asBool(v);
        else if (k == "typoCorrection") cfg.typoCorrection = asBool(v);
        else if (k == "looseInitialMatch") cfg.looseInitialMatch = asBool(v);
        else if (k == "enableCsPack") cfg.enableCsPack = asBool(v);
        else if (k == "enableExtPack") cfg.enableExtPack = asBool(v);
        else if (k == "enableProPack") cfg.enableProPack = asBool(v);
        else if (k == "enableEnPack") cfg.enableEnPack = asBool(v);
        else if (k == "enableCustomDicts") cfg.enableCustomDicts = asBool(v);
        else if (k == "statusPosX") {
            try { cfg.statusPosX = std::stoi(std::string(v)); } catch (...) {}
        } else if (k == "statusPosY") {
            try { cfg.statusPosY = std::stoi(std::string(v)); } catch (...) {}
        } else if (k == "skin") {
            try { cfg.skin = static_cast<SkinTheme>(std::clamp(std::stoi(std::string(v)), 0, 3)); } catch (...) {}
        } else if (k == "layout") {
            try { cfg.layout = static_cast<CandidateLayout>(std::clamp(std::stoi(std::string(v)), 0, 1)); } catch (...) {}
        } else if (k == "squareFrame") cfg.squareFrame = asBool(v);
        else if (k == "inlinePreedit") {
            try { cfg.inlinePreedit = static_cast<InlinePreeditMode>(std::clamp(std::stoi(std::string(v)), 0, 2)); } catch (...) {}
        } else if (k == "fuzzy.zh_z") cfg.fuzzy.zh_z = asBool(v);
        else if (k == "fuzzy.ch_c") cfg.fuzzy.ch_c = asBool(v);
        else if (k == "fuzzy.sh_s") cfg.fuzzy.sh_s = asBool(v);
        else if (k == "fuzzy.n_l") cfg.fuzzy.n_l = asBool(v);
        else if (k == "fuzzy.r_l") cfg.fuzzy.r_l = asBool(v);
        else if (k == "fuzzy.f_h") cfg.fuzzy.f_h = asBool(v);
        else if (k == "fuzzy.an_ang") cfg.fuzzy.an_ang = asBool(v);
        else if (k == "fuzzy.en_eng") cfg.fuzzy.en_eng = asBool(v);
        else if (k == "fuzzy.in_ing") cfg.fuzzy.in_ing = asBool(v);
        else if (k == "fuzzy.iang_ian") cfg.fuzzy.iang_ian = asBool(v);
        else if (k == "fuzzy.uang_uan") cfg.fuzzy.uang_uan = asBool(v);
    }
    return true;
}

bool UserDict::saveConfig(const std::filesystem::path& path, const EngineConfig& cfg) {
    if (path.empty()) return false;
    auto b = [](bool v) { return v ? 1 : 0; };
    std::ostringstream oss;
    oss << "# LiteIME Config\n"
        << "pageSize=" << cfg.pageSize << '\n'
        << "chineseMode=" << b(cfg.chineseMode) << '\n'
        << "fullWidth=" << b(cfg.fullWidth) << '\n'
        << "chinesePunct=" << b(cfg.chinesePunct) << '\n'
        << "mixedEnglish=" << b(cfg.mixedEnglish) << '\n'
        << "smartDecimal=" << b(cfg.smartDecimal) << '\n'
        << "autoUrlEnglish=" << b(cfg.autoUrlEnglish) << '\n'
        << "shiftTapToggle=" << b(cfg.shiftTapToggle) << '\n'
        << "showStatusBar=" << b(cfg.showStatusBar) << '\n'
        << "typoCorrection=" << b(cfg.typoCorrection) << '\n'
        << "looseInitialMatch=" << b(cfg.looseInitialMatch) << '\n'
        << "enableCsPack=" << b(cfg.enableCsPack) << '\n'
        << "enableExtPack=" << b(cfg.enableExtPack) << '\n'
        << "enableProPack=" << b(cfg.enableProPack) << '\n'
        << "enableEnPack=" << b(cfg.enableEnPack) << '\n'
        << "enableCustomDicts=" << b(cfg.enableCustomDicts) << '\n'
        << "statusPosX=" << cfg.statusPosX << '\n'
        << "statusPosY=" << cfg.statusPosY << '\n'
        << "skin=" << static_cast<int>(cfg.skin) << '\n'
        << "layout=" << static_cast<int>(cfg.layout) << '\n'
        << "squareFrame=" << b(cfg.squareFrame) << '\n'
        << "inlinePreedit=" << static_cast<int>(cfg.inlinePreedit) << '\n'
        << "fuzzy.zh_z=" << b(cfg.fuzzy.zh_z) << '\n'
        << "fuzzy.ch_c=" << b(cfg.fuzzy.ch_c) << '\n'
        << "fuzzy.sh_s=" << b(cfg.fuzzy.sh_s) << '\n'
        << "fuzzy.n_l=" << b(cfg.fuzzy.n_l) << '\n'
        << "fuzzy.r_l=" << b(cfg.fuzzy.r_l) << '\n'
        << "fuzzy.f_h=" << b(cfg.fuzzy.f_h) << '\n'
        << "fuzzy.an_ang=" << b(cfg.fuzzy.an_ang) << '\n'
        << "fuzzy.en_eng=" << b(cfg.fuzzy.en_eng) << '\n'
        << "fuzzy.in_ing=" << b(cfg.fuzzy.in_ing) << '\n'
        << "fuzzy.iang_ian=" << b(cfg.fuzzy.iang_ian) << '\n'
        << "fuzzy.uang_uan=" << b(cfg.fuzzy.uang_uan) << '\n';
    return atomicWriteTextFile(path, oss.str());
}

void UserDict::clear() {
    entries_.clear();
    pins_.clear();
    quickInputs_.clear();
    dirty_ = false;
}

} // namespace liteime::engine
