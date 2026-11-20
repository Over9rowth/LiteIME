#include "engine/bilingual_engine.hpp"
#include "engine/binary_dict.hpp"
#include "engine/pinyin_table.hpp"
#include "common/ipc_protocol.hpp"
#include "common/utf.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "CHECK FAILED at line " << __LINE__ << ": " << #cond << "\n"; \
            std::exit(1); \
        } \
    } while (0)

using namespace liteime;
using namespace liteime::engine;

namespace {

std::filesystem::path buildFixtureDict(const std::filesystem::path& dir) {
    std::vector<RawPhraseRecord> phrases = {
        {"你", "ni", 10000, 1},
        {"拟", "ni", 1000, 1},
        {"好", "hao", 10000, 1},
        {"号", "hao", 5000, 1},
        {"你好", "ni hao", 50000, 2},
        {"拟好", "ni hao", 500, 2},
        {"我", "wo", 20000, 1},
        {"想", "xiang", 8000, 1},
        {"看", "kan", 7000, 1},
        {"我想", "wo xiang", 15000, 2},
        {"在", "zai", 18000, 1},
        {"写", "xie", 6000, 1},
        {"一个", "yi ge", 25000, 2},
        {"输入", "shu ru", 12000, 2},
        {"法", "fa", 9000, 1},
        {"输入法", "shu ru fa", 30000, 3},
        {"中国", "zhong guo", 40000, 2},
        {"西安", "xi an", 20000, 2},
        {"西安站", "xi an zhan", 12000, 3},
        {"先", "xian", 18000, 1},
        {"有", "you", 50000, 1},
        {"又", "you", 40000, 1},
        {"由", "you", 30000, 1},
        {"友", "you", 20000, 1},
        {"三", "san", 50000, 1},
        {"怎", "zen", 8000, 1},
        {"么", "me", 10000, 1},
        {"怎么", "zen me", 55000, 2},
        {"南", "nan", 9000, 1},
        {"蓝", "lan", 9500, 1},
        {"知道", "zhi dao", 25000, 2},
        {"字", "zi", 8000, 1},
        {"只", "zhi", 12000, 1},
        {"哇", "wa", 8000, 1},
        {"瓦", "wa", 5000, 1},
        {"挖掘", "wa jue", 12000, 2},
        {"娃哈哈", "wa ha ha", 20000, 3},
        {"五路", "wu lu", 5000, 2},
        {"无路", "wu lu", 3000, 2},
        {"无卤", "wu lu", 1000, 2},
        {"侮辱", "wu ru", 8000, 2},
        {"网络", "wang luo", 45000, 2},
        {"未来", "wei lai", 40000, 2},
        {"说", "shuo", 35000, 1},
        {"是", "shi", 55000, 1},
        {"四", "si", 25000, 1},
        {"什么", "shen me", 60000, 2},
        {"鬼", "gui", 15000, 1},
        {"啊", "a", 12000, 1},
        {"说明", "shuo ming", 30000, 2},
        {"市民", "shi min", 10000, 2},
        {"中", "zhong", 40000, 1},
        {"重", "zhong", 20000, 1},
        {"阿", "a", 12000, 1},
        {"拉", "la", 10000, 1},
        {"了", "le", 60000, 1},
        {"来", "lai", 50000, 1},
        {"里", "li", 45000, 1},
        {"林", "lin", 18000, 1},
        {"路", "lu", 22000, 1},
        {"泠", "ling", 3000, 1},
        {"请", "qing", 50000, 1},
        {"轻", "qing", 40000, 1},
        {"清", "qing", 35000, 1},
        {"年", "nian", 4000, 1},
        {"青年", "qing nian", 30000, 2},
        {"请你", "qing ni", 20000, 2},
        {"请您", "qing nin", 15000, 2},
        {"为", "wei", 50000, 1},
        {"必", "bi", 30000, 1},
        {"未必", "wei bi", 140000, 2},
        {"我不", "wo bu", 500000, 2},
        {"外部", "wai bu", 410000, 2},
        {"文本", "wen ben", 400000, 2},
        {"无病", "wu bing", 15000, 2},
        {"王二", "wang er", 13000, 2},
        {"为恶", "wei e", 7500, 2},
        {"婉儿", "wan er", 8000, 2},
        {"居", "ju", 52000, 1},
        {"局", "ju", 46000, 1},
        {"女", "nv", 9000, 1},
        {"怒", "nu", 8000, 1},
        {"女儿", "nv er", 50000, 2},
        {"去", "qu", 70000, 1},
        {"去年", "qu nian", 65000, 2},
        {"全", "quan", 55000, 1},
        {"需", "xu", 54000, 1},
        {"雨", "yu", 72000, 1},
        {"语", "yu", 60000, 1},
        {"语言", "yu yan", 68000, 2},
        {"决定", "jue ding", 62000, 2},
        {"觉", "jue", 48000, 1},
        {"虐", "nve", 3000, 1},
        {"略", "lve", 3000, 1},
    };
    std::vector<RawEnglishRecord> english = {
        {"python", "Python", 50000},
        {"pytest", "pytest", 10000},
        {"windows", "Windows", 40000},
        {"hello", "hello", 60000},
    };
    auto out = dir / "test_fixture.dict";
    CHECK(BinaryDict::compileToFile(out, std::move(phrases), std::move(english)));
    return out;
}

void typeWord(BilingualEngine& eng, const std::string& letters) {
    for (char c : letters) {
        if (c == '\'') {
            eng.handleKey(0xDE, 0, false);
        } else if (c >= 'a' && c <= 'z') {
            eng.handleKey(static_cast<uint32_t>(c - 'a' + 'A'), 0, false);
        } else if (c >= 'A' && c <= 'Z') {
            eng.handleKey(static_cast<uint32_t>(c), ipc::ModShift, false);
        }
    }
}

} // namespace

int wmain() {
    auto tmpDir = std::filesystem::temp_directory_path() / "liteime_tests";
    std::filesystem::create_directories(tmpDir);
    auto dictPath = buildFixtureDict(tmpDir);

    BinaryDict dict;
    CHECK(dict.loadFile(dictPath));

    // 1. Pinyin segmentation & apostrophe boundary
    {
        auto p1 = PinyinTable::segment("xian");
        CHECK(!p1.empty());
        CHECK(p1[0].joinedKey() == "xian");

        auto p2 = PinyinTable::segment("xi'an");
        CHECK(!p2.empty());
        CHECK(p2[0].joinedKey() == "xi an");
    }

    // 2. Basic Chinese phrase, sentence Viterbi, and Jianpin
    {
        BilingualEngine eng;
        eng.setDictionary(&dict);
        auto downN = eng.handleKey('N', 0, false);
        CHECK(downN.consumed && downN.showCandidates);
        auto upN = eng.handleKey('N', 0, true);
        CHECK(!upN.consumed && upN.showCandidates);
        CHECK(upN.headerBuffer == "n");
        eng.handleKey(0x1B, 0, false); // Escape
        auto upAfterEsc = eng.handleKey('N', 0, true);
        CHECK(!upAfterEsc.consumed && !upAfterEsc.showCandidates);

        typeWord(eng, "nihao");
        auto page = eng.currentPageItems();
        CHECK(!page.empty());
        CHECK(page[0].text == "你好");

        auto committed = eng.handleKey(0x20, 0, false); // Space
        CHECK(committed.commitText == "你好");
        CHECK(!eng.isComposing());

        // Whole sentence: wo zai xie yi ge shu ru fa
        typeWord(eng, "wozaixieyigeshurufa");
        page = eng.currentPageItems();
        CHECK(!page.empty());
        CHECK(page[0].text == "我在写一个输入法");
        eng.handleKey(0x1B, 0, false); // Escape

        // Jianpin: srf -> 输入法
        typeWord(eng, "srf");
        page = eng.currentPageItems();
        CHECK(!page.empty());
        bool foundSrf = false;
        for (const auto& c : page) {
            if (c.text == "输入法") foundSrf = true;
        }
        CHECK(foundSrf);
        eng.handleKey(0x1B, 0, false);
    }

    // 3. Partial commit + self-made phrase learning
    {
        BilingualEngine eng;
        eng.setDictionary(&dict);
        auto userPath = tmpDir / "user_dict.tsv";
        std::filesystem::remove(userPath);
        eng.userDict().load(userPath);

        typeWord(eng, "woxiangkan");
        // Pick "我想" (partial prefix phrase)
        auto all = eng.allCandidates();
        int idxInPage = -1;
        for (size_t i = 0; i < eng.currentPageItems().size(); ++i) {
            if (eng.currentPageItems()[i].text == "我想") {
                idxInPage = static_cast<int>(i);
                break;
            }
        }
        CHECK(idxInPage >= 0);
        auto step1 = eng.selectCandidateOnPage(idxInPage);
        CHECK(step1.commitText.empty());
        CHECK(eng.isComposing());
        CHECK(eng.rawBuffer() == "kan");

        auto step2 = eng.handleKey(0x20, 0, false); // Space picks "看"
        CHECK(step2.commitText == "我想看");

        // Now typing "woxiangkan" again should offer "我想看" as #1!
        typeWord(eng, "woxiangkan");
        CHECK(eng.currentPageItems()[0].text == "我想看");
        eng.handleKey(0x1B, 0, false); // Escape

        // Backspace undoing remaining partial pinyin restores prefix step instead of going blank
        typeWord(eng, "woxiangk");
        idxInPage = -1;
        for (size_t i = 0; i < eng.currentPageItems().size(); ++i) {
            if (eng.currentPageItems()[i].text == "我想") {
                idxInPage = static_cast<int>(i);
                break;
            }
        }
        CHECK(idxInPage >= 0);
        auto partialRes = eng.selectCandidateOnPage(idxInPage);
        CHECK(partialRes.showCandidates);
        CHECK(partialRes.headerBuffer.find("我想") != std::string::npos);
        auto bsUndo = eng.handleKey(0x08, 0, false); // deletes 'k' -> restores "woxiang"
        CHECK(bsUndo.showCandidates);
        CHECK(eng.rawBuffer() == "woxiang");
        eng.handleKey(0x1B, 0, false); // Escape

        // ']' (0xDD) and '[' (0xDB) page through candidates while composing, and output 【 】 when idle
        eng.config().pageSize = 3;
        typeWord(eng, "nihao");
        CHECK(eng.totalPages() >= 2);
        CHECK(eng.currentPage() == 0);
        auto pageNext = eng.handleKey(0xDD, 0, false); // ']'
        CHECK(pageNext.consumed && pageNext.commitText.empty() && pageNext.showCandidates);
        CHECK(eng.currentPage() == 1);
        CHECK(eng.isComposing());
        auto pagePrev = eng.handleKey(0xDB, 0, false); // '['
        CHECK(pagePrev.consumed && pagePrev.commitText.empty());
        CHECK(eng.currentPage() == 0);
        eng.handleKey(0x1B, 0, false); // Escape
        CHECK(eng.handleKey(0xDB, 0, false).commitText == "【");
        CHECK(eng.handleKey(0xDD, 0, false).commitText == "】");
    }

    // 4. Bilingual mixed English + Enter raw commit + Shift tap toggle + Smart decimal
    {
        BilingualEngine eng;
        eng.setDictionary(&dict);

        // Mixed English candidate
        typeWord(eng, "python");
        bool hasPython = false;
        for (const auto& c : eng.allCandidates()) {
            if (c.text == "Python") hasPython = true;
        }
        CHECK(hasPython);

        // Enter commits raw lowercase "python"
        auto enterRes = eng.handleKey(0x0D, 0, false);
        CHECK(enterRes.commitText == "python");

        // Smart decimal: 3.14 stays '.'
        eng.handleKey('3', 0, false);
        auto dotRes = eng.handleKey(0xBE, 0, false);
        CHECK(dotRes.commitText == ".");

        // Regular period after Chinese text -> "。"
        eng.handleKey('A', 0, false);
        eng.handleKey(0x20, 0, false);
        auto cnDot = eng.handleKey(0xBE, 0, false);
        CHECK(cnDot.commitText == "。");

        // Shift tap toggles CN -> EN, Shift+A does NOT toggle
        eng.handleKey(0x10, ipc::ModShift, false);
        eng.handleKey('A', ipc::ModShift, false);
        eng.handleKey(0x10, 0, true);
        CHECK(eng.config().chineseMode == true); // Shift+A must not toggle!

        eng.handleKey(0x10, ipc::ModShift, false);
        eng.handleKey(0x10, 0, true);
        CHECK(eng.config().chineseMode == false); // Tap Shift toggles to EN!
    }

    // 5. Fuzzy Pinyin
    {
        BilingualEngine eng;
        eng.setDictionary(&dict);
        eng.config().fuzzy.n_l = true;
        typeWord(eng, "nan");
        bool hasLan = false;
        for (const auto& c : eng.allCandidates()) {
            if (c.text == "蓝") hasLan = true;
        }
        CHECK(hasLan);
    }

    // 6. Single-initial word length cap ('w' must ONLY show 1-char words)
    {
        BilingualEngine eng;
        eng.setDictionary(&dict);
        typeWord(eng, "w");
        const auto& all = eng.allCandidates();
        CHECK(!all.empty());
        for (const auto& c : all) {
            if (c.kind == CandidateKind::RawInput || c.kind == CandidateKind::EnglishWord) continue;
            // Count UTF-8 characters (all Chinese chars in fixture are 3 bytes each)
            size_t chars = 0;
            for (unsigned char b : c.text) {
                if ((b & 0xC0) != 0x80) ++chars;
            }
            CHECK(chars <= 1);
            CHECK(c.text != "挖掘");
            CHECK(c.text != "娃哈哈");
        }
    }

    // 7. Page size 5..8 configurability
    {
        BilingualEngine eng;
        eng.setDictionary(&dict);
        eng.config().pageSize = 7;
        typeWord(eng, "nihao");
        CHECK(eng.currentPageItems().size() <= 7);
        CHECK(eng.config().pageSize == 7);
        eng.handleKey(0x1B, 0, false);
        eng.config().pageSize = 8;
        typeWord(eng, "w");
        CHECK(eng.currentPageItems().size() <= 8);
    }

    // 8. Domain pack extra dictionary (OFF by default, ON via addExtraDictionary)
    {
        std::vector<RawPhraseRecord> csPhrases = {
            {"二叉树", "er cha shu", 20000, 3},
            {"哈希表", "ha xi biao", 18000, 3},
        };
        auto csPath = tmpDir / "cs_pack.dict";
        CHECK(BinaryDict::compileToFile(csPath, std::move(csPhrases), {}));
        BinaryDict csDict;
        CHECK(csDict.loadFile(csPath));

        BilingualEngine eng;
        eng.setDictionary(&dict);
        typeWord(eng, "erchashu");
        auto before = eng.allCandidates();
        CHECK(std::none_of(before.begin(), before.end(), [](const CandidateItem& c) { return c.text == "二叉树"; }));
        eng.handleKey(0x1B, 0, false);

        eng.addExtraDictionary(&csDict);
        typeWord(eng, "erchashu");
        auto after = eng.allCandidates();
        CHECK(!after.empty() && after[0].text == "二叉树");
    }

    // 9. 'vvv' QuickInput: masked preview, full commit, no learning, setup dialog trigger
    {
        BilingualEngine eng;
        eng.setDictionary(&dict);
        auto qPath = tmpDir / "quick_user.tsv";
        std::filesystem::remove(qPath);
        std::filesystem::remove(tmpDir / "quick_input.tsv");
        eng.userDict().load(qPath);
        eng.userDict().setQuickInputs({
            {"手机", "13812345678"},
            {"身份证", "110101199001011234"},
        });
        CHECK(UserDict::maskQuickValue("13812345678") == "138****");
        CHECK(UserDict::maskQuickValue("010-88889999") == "010****");

        eng.handleKey('V', 0, false);
        eng.handleKey('V', 0, false);
        auto v3Res = eng.handleKey('V', 0, false);
        auto page = eng.currentPageItems();
        CHECK(page.size() >= 3);
        CHECK(page[0].text == "手机: 138****");
        CHECK(page[0].kind == CandidateKind::QuickInput);
        CHECK(v3Res.preeditText == "vvv"); // never leaks secret in preedit
        CHECK(page.back().kind == CandidateKind::QuickInputSetup);

        auto commitPhone = eng.handleKey(0x20, 0, false); // Space
        CHECK(commitPhone.commitText == "13812345678");
        CHECK(eng.userDict().lookup("vvv").empty()); // never learned

        typeWord(eng, "vvv");
        auto openDlg = eng.selectCandidateOnPage(2); // Setup entry
        CHECK(openDlg.openQuickInputDialog);
        CHECK(!eng.isComposing());
    }

    // 10. Fuzzy r<->l ('wulu' -> 侮辱 (wu'ru)) strictly after exact ('五路/无路/无卤');
    //     LooseInitial ('未来 (weilai)') is OFF by default and capped at <= 2 ultra-high-freq items when enabled.
    {
        BilingualEngine eng;
        eng.setDictionary(&dict);
        CHECK(eng.config().looseInitialMatch == false);
        eng.config().fuzzy.r_l = true;
        typeWord(eng, "wulu");
        auto hasText = [&](const std::string& w) {
            for (const auto& c : eng.allCandidates()) {
                if (c.text == w) return true;
            }
            return false;
        };
        CHECK(!hasText("未来")); // Default OFF
        eng.handleKey(0x1B, 0, false);

        eng.config().looseInitialMatch = true;
        typeWord(eng, "wulu");
        const auto& all = eng.allCandidates();
        auto idxOf = [&](const std::string& w) -> int {
            for (size_t i = 0; i < all.size(); ++i) {
                if (all[i].text == w) return static_cast<int>(i);
            }
            return -1;
        };
        int iWuLu = idxOf("五路");
        int iWuLu3 = idxOf("无卤");
        int iWuRu = idxOf("侮辱");
        int iWeiLai = idxOf("未来");
        CHECK(iWuLu >= 0 && iWuLu3 >= 0 && iWuRu >= 0 && iWeiLai >= 0);
        CHECK(iWuLu < iWuLu3);
        CHECK(iWuLu3 < iWuRu);
        CHECK(iWuRu < iWeiLai);
        CHECK(all[iWuRu].comment == "(wu'ru)");
        CHECK(all[iWeiLai].comment == "(weilai)");
    }

    // 11. Typo auto-correction: zhogn / zhog / zzhong / zhognguo
    {
        BilingualEngine eng;
        eng.setDictionary(&dict);
        CHECK(eng.config().typoCorrection == true);

        typeWord(eng, "zhogn");
        CHECK(!eng.currentPageItems().empty());
        CHECK(eng.currentPageItems()[0].text == "中");
        CHECK(eng.currentPageItems()[0].comment == "(zhong)");
        eng.handleKey(0x1B, 0, false);

        typeWord(eng, "zhog");
        CHECK(!eng.currentPageItems().empty());
        CHECK(eng.currentPageItems()[0].text == "中");
        CHECK(eng.currentPageItems()[0].comment == "(zhong)");
        eng.handleKey(0x1B, 0, false);

        typeWord(eng, "zhognguo");
        CHECK(!eng.currentPageItems().empty());
        CHECK(eng.currentPageItems()[0].text == "中国");
        CHECK(eng.currentPageItems()[0].comment == "(zhong'guo)");
        eng.handleKey(0x1B, 0, false);

        eng.config().typoCorrection = false;
        typeWord(eng, "zhogn");
        CHECK(eng.currentPageItems()[0].kind != CandidateKind::TypoCorrection);
        CHECK(eng.currentPageItems()[0].comment != "(zhong)");
        eng.handleKey(0x1B, 0, false);

        eng.config().typoCorrection = true;
        typeWord(eng, "zzhong");
        CHECK(eng.currentPageItems()[0].text == "中");
        CHECK(eng.currentPageItems()[0].comment == "(zhong)");
        eng.handleKey(0x1B, 0, false);

        eng.config().typoCorrection = false;
        typeWord(eng, "zzhong");
        CHECK(eng.currentPageItems()[0].kind != CandidateKind::TypoCorrection);
        CHECK(eng.currentPageItems()[0].comment != "(zhong)");
    }

    // 12. Tiered frequency promotion (0->1:2, 1->2:+3=5, 2->3:+4=9), strict intra-tier default order, NO fuzzy promotion
    {
        CHECK(UserDict::tierForCount(0) == 0);
        CHECK(UserDict::tierForCount(1) == 0);
        CHECK(UserDict::tierForCount(2) == 1);
        CHECK(UserDict::tierForCount(4) == 1);
        CHECK(UserDict::tierForCount(5) == 2);
        CHECK(UserDict::tierForCount(8) == 2);
        CHECK(UserDict::tierForCount(9) == 3);
        CHECK(UserDict::tierForCount(13) == 3);
        CHECK(UserDict::tierForCount(14) == 4);
        CHECK(UserDict::tierForCount(18) == 4);
        CHECK(UserDict::tierForCount(19) == 5);
        CHECK(UserDict::tierForCount(23) == 5);
        CHECK(UserDict::tierForCount(24) == 6);

        BilingualEngine eng;
        eng.setDictionary(&dict);
        eng.config().fuzzy.r_l = true;
        auto tierUserPath = tmpDir / "tier_user.tsv";
        std::filesystem::remove(tierUserPath);
        eng.userDict().load(tierUserPath);

        // Select "无卤" ONCE -> still Tier 0 -> stays behind "五路" and "无路"
        typeWord(eng, "wulu");
        CHECK(eng.currentPageItems()[0].text == "五路");
        CHECK(eng.currentPageItems()[1].text == "无路");
        CHECK(eng.currentPageItems()[2].text == "无卤");
        CHECK(eng.selectCandidateOnPage(2).commitText == "无卤");

        typeWord(eng, "wulu");
        CHECK(eng.currentPageItems()[0].text == "五路");
        CHECK(eng.currentPageItems()[2].text == "无卤");

        // Select "无卤" a 2nd time -> Tier 1 -> jumps to #1!
        CHECK(eng.selectCandidateOnPage(2).commitText == "无卤");
        typeWord(eng, "wulu");
        CHECK(eng.currentPageItems()[0].text == "无卤");
        eng.handleKey(0x1B, 0, false);

        // Also select "侮辱" (Fuzzy) 3 times -> MUST NEVER learn or jump ahead of "五路"!
        for (int k = 0; k < 3; ++k) {
            typeWord(eng, "wulu");
            const auto& all = eng.allCandidates();
            int fIdx = -1;
            for (size_t i = 0; i < all.size(); ++i) {
                if (all[i].text == "侮辱") { fIdx = static_cast<int>(i); break; }
            }
            CHECK(fIdx >= 0);
            CHECK(eng.selectCandidateOnPage(fIdx).commitText == "侮辱");
        }
        CHECK(eng.userDict().lookup("wu ru").empty());
        CHECK(eng.userDict().lookup("wu lu").size() == 1);

        // Promote "无路" twice -> now BOTH "无路" (dict freq 3000) and "无卤" (dict freq 1000) are in Tier 1;
        // Within Tier 1, default dict order MUST put "无路" ahead of "无卤"!
        for (int k = 0; k < 2; ++k) {
            typeWord(eng, "wulu");
            const auto& page = eng.currentPageItems();
            int pos = -1;
            for (size_t i = 0; i < page.size(); ++i) {
                if (page[i].text == "无路") { pos = static_cast<int>(i); break; }
            }
            CHECK(pos >= 0);
            eng.selectCandidateOnPage(pos);
        }
        typeWord(eng, "wulu");
        CHECK(eng.currentPageItems()[0].text == "无路");
        CHECK(eng.currentPageItems()[1].text == "无卤");
        eng.handleKey(0x1B, 0, false);

        // 3 MORE hits on "无卤" (total 5 -> Tier 2!) beats Tier 1 "无路"!
        for (int k = 0; k < 3; ++k) {
            typeWord(eng, "wulu");
            CHECK(eng.selectCandidateOnPage(1).commitText == "无卤");
        }
        typeWord(eng, "wulu");
        CHECK(eng.currentPageItems()[0].text == "无卤");
        CHECK(eng.currentPageItems()[1].text == "无路");
    }

    // 13. Right-click pinning to slots 1..5 overrides tiers + persists
    {
        BilingualEngine eng;
        eng.setDictionary(&dict);
        auto pinUserPath = tmpDir / "pin_user.tsv";
        std::filesystem::remove(pinUserPath);
        std::filesystem::remove(tmpDir / "pins.tsv");
        eng.userDict().load(pinUserPath);

        typeWord(eng, "sm");
        // Find "市民" and pin it to slot 0 (#1), overriding higher-freq "什么"
        int smIdx = -1;
        for (size_t i = 0; i < eng.currentPageItems().size(); ++i) {
            if (eng.currentPageItems()[i].text == "市民") { smIdx = static_cast<int>(i); break; }
        }
        CHECK(smIdx >= 0);
        CHECK(eng.pinCandidateOnPage(smIdx, 0));
        CHECK(eng.currentPageItems()[0].text == "市民");
        CHECK(eng.currentPageItems()[0].pinnedSlot == 0);
        CHECK(eng.currentPageItems()[1].text == "什么");

        // Reload in fresh engine and verify persistence
        BilingualEngine eng2;
        eng2.setDictionary(&dict);
        eng2.userDict().load(pinUserPath);
        typeWord(eng2, "sm");
        CHECK(eng2.currentPageItems()[0].text == "市民");
        CHECK(eng2.unpinCandidateOnPage(0));
        CHECK(eng2.currentPageItems()[0].text == "什么");
    }

    // 14. Caps Lock priority in Chinese mode
    {
        BilingualEngine eng;
        eng.setDictionary(&dict);
        CHECK(eng.config().chineseMode == true);

        // Idle with ModCaps: letters & punctuation pass straight through (consumed == false)
        auto capsA = eng.handleKey('A', ipc::ModCaps, false);
        CHECK(!capsA.consumed && !capsA.showCandidates);
        auto capsComma = eng.handleKey(0xBC, ipc::ModCaps, false); // ',' stays English
        CHECK(!capsComma.consumed && capsComma.commitText.empty());

        // Mid-composition when ModCaps turns on: typing 'A' flushes buffered pinyin + 'A'
        typeWord(eng, "ni");
        CHECK(eng.isComposing());
        auto flushCaps = eng.handleKey('A', ipc::ModCaps, false);
        CHECK(flushCaps.consumed && flushCaps.commitText == "niA");
        CHECK(!eng.isComposing());
    }

    // 15. Long raw-buffer safety cap (prevents CPU spike on stuck keys)
    {
        BilingualEngine eng;
        eng.setDictionary(&dict);
        for (int i = 0; i < 90; ++i) {
            auto r = eng.handleKey('W', 0, false);
            CHECK(r.consumed);
        }
        CHECK(eng.rawBuffer().size() == 64);
        eng.handleKey(0x1B, 0, false); // Escape
        CHECK(!eng.isComposing());
    }

    // 16. Partial initial 'l' after partial selection of "阿" in "al" + user-learned Jianpin "al -> 阿泠"
    {
        auto tmpDir = std::filesystem::temp_directory_path() / "liteime_test_al";
        std::filesystem::create_directories(tmpDir);
        auto alUserPath = tmpDir / "user_dict.tsv";
        std::filesystem::remove(alUserPath);
        std::filesystem::remove(tmpDir / "pins.tsv");

        BilingualEngine eng;
        eng.setDictionary(&dict);
        eng.userDict().load(alUserPath);

        typeWord(eng, "al");
        int aPageIdx = -1;
        for (int p = 0; p < eng.totalPages() && aPageIdx < 0; ++p) {
            const auto items = eng.currentPageItems();
            for (size_t i = 0; i < items.size(); ++i) {
                if (items[i].text == "阿") { aPageIdx = static_cast<int>(i); break; }
            }
            if (aPageIdx < 0) eng.handleKey(0xBB, 0, false); // '=' next page
        }
        CHECK(aPageIdx >= 0);
        auto partialRes = eng.handleKey(static_cast<uint32_t>('1' + aPageIdx), 0, false);
        CHECK(partialRes.consumed && partialRes.commitText.empty());
        CHECK(eng.rawBuffer() == "l");
        // In fixture dict all 7 l* entries across la..lu (not truncated inside 'la*') should appear!
        CHECK(eng.allCandidates().size() >= 7);
        auto hasWord = [&](const std::string& w) {
            for (const auto& c : eng.allCandidates()) if (c.text == w) return true;
            return false;
        };
        CHECK(hasWord("了") && hasWord("来") && hasWord("里") && hasWord("泠"));
        eng.handleKey(0x1B, 0, false); // cancel

        // Simulate user composing "阿泠" (a ling) once -> subsequent "al" surfaces "阿泠" for pinning to #1!
        eng.userDict().recordSelection("a ling", "阿泠");
        typeWord(eng, "al");
        int alPageIdx = -1;
        for (int p = 0; p < eng.totalPages() && alPageIdx < 0; ++p) {
            const auto items = eng.currentPageItems();
            for (size_t i = 0; i < items.size(); ++i) {
                if (items[i].text == "阿泠") { alPageIdx = static_cast<int>(i); break; }
            }
            if (alPageIdx < 0) eng.handleKey(0xBB, 0, false);
        }
        CHECK(alPageIdx >= 0);
        CHECK(eng.pinCandidateOnPage(alPageIdx, 0));
        eng.handleKey(0x1B, 0, false);
        typeWord(eng, "al");
        CHECK(!eng.currentPageItems().empty() && eng.currentPageItems()[0].text == "阿泠");
    }

    // 17. Mixed Jianpin+full-syllable prefix ("smguia" -> "什么" -> "guia") + prefix pin & squareFrame config
    {
        auto tmpDir = std::filesystem::temp_directory_path() / "liteime_test_smguia";
        std::filesystem::create_directories(tmpDir);
        auto smUserPath = tmpDir / "user_dict.tsv";
        std::filesystem::remove(smUserPath);
        std::filesystem::remove(tmpDir / "pins.tsv");
        std::filesystem::remove(tmpDir / "config.ini");

        BilingualEngine eng;
        eng.setDictionary(&dict);
        eng.userDict().load(smUserPath);

        // Unpinned "smguia" surfaces "什么" above single 's' chars, and picking it leaves "guia"
        typeWord(eng, "smguia");
        CHECK(!eng.currentPageItems().empty());
        CHECK(eng.currentPageItems()[0].text == "什么");
        auto pickSm = eng.handleKey('1', 0, false);
        CHECK(pickSm.consumed && pickSm.commitText.empty());
        CHECK(eng.rawBuffer() == "guia");
        eng.handleKey(0x1B, 0, false);

        // Pin "说明" to slot 0 on "sm", then type "smguia" -> "说明" jumps to #1 with [固1]
        typeWord(eng, "sm");
        int smIdx = -1;
        for (size_t i = 0; i < eng.currentPageItems().size(); ++i) {
            if (eng.currentPageItems()[i].text == "说明") { smIdx = static_cast<int>(i); break; }
        }
        CHECK(smIdx >= 0);
        CHECK(eng.pinCandidateOnPage(smIdx, 0));
        eng.handleKey(0x1B, 0, false);

        typeWord(eng, "smguia");
        CHECK(!eng.currentPageItems().empty() && eng.currentPageItems()[0].text == "说明");
        CHECK(eng.currentPageItems()[0].pinnedSlot == 0);
        auto pickPinnedSm = eng.handleKey('1', 0, false);
        CHECK(pickPinnedSm.consumed && pickPinnedSm.commitText.empty());
        CHECK(eng.rawBuffer() == "guia");
        eng.handleKey(0x1B, 0, false);

        // Config persistence for squareFrame (default true -> toggle false -> reload)
        CHECK(eng.config().squareFrame == true);
        eng.config().squareFrame = false;
        CHECK(UserDict::saveConfig(eng.userDict().configPath(), eng.config()));
        EngineConfig reloaded;
        CHECK(UserDict::loadConfig(eng.userDict().configPath(), reloaded));
        CHECK(reloaded.squareFrame == false);
    }

    // 18. InlinePreeditMode: CandidatePreview / CommittedPlusPinyin / None + config persistence
    {
        BilingualEngine eng;
        eng.setDictionary(&dict);

        // Mode A: CandidatePreview (default) shows predicted Chinese chars inline on first & multi keys
        CHECK(eng.config().inlinePreedit == InlinePreeditMode::CandidatePreview);
        auto rN = eng.handleKey('N', 0, false);
        CHECK(rN.consumed && rN.showCandidates && rN.preeditText == "你");
        eng.handleKey(0x1B, 0, false);

        // Mode B: CommittedPlusPinyin shows pinyin before commit, and committedWords + remaining pinyin mid-phrase
        eng.config().inlinePreedit = InlinePreeditMode::CommittedPlusPinyin;
        auto rS = eng.handleKey('S', 0, false);
        CHECK(rS.consumed && rS.showCandidates && rS.preeditText == "s");
        typeWord(eng, "mgui");
        auto rA = eng.handleKey('A', 0, false);
        CHECK(rA.consumed && rA.showCandidates && rA.preeditText == "s'm'gui'a");
        auto pickSm = eng.handleKey('1', 0, false); // "什么"
        CHECK(pickSm.consumed && pickSm.commitText.empty() && pickSm.preeditText == "什么gui'a");
        eng.handleKey(0x1B, 0, false);

        // Mode C: None keeps target input box completely empty until final commit
        eng.config().inlinePreedit = InlinePreeditMode::None;
        auto rW = eng.handleKey('W', 0, false);
        CHECK(rW.consumed && rW.showCandidates && rW.preeditText.empty());
        typeWord(eng, "o");
        auto rLast = eng.handleKey('O', 0, false); // "woo"? reset and type "ni" cleanly
        (void)rLast;
        eng.handleKey(0x1B, 0, false);
        typeWord(eng, "smgui");
        auto rA2 = eng.handleKey('A', 0, false);
        CHECK(rA2.consumed && rA2.showCandidates && rA2.preeditText.empty());
        auto pickSm2 = eng.handleKey('1', 0, false); // "什么"
        CHECK(pickSm2.consumed && pickSm2.commitText.empty() && pickSm2.preeditText.empty() && pickSm2.showCandidates);
        auto pickGuiA = eng.handleKey('1', 0, false); // Viterbi sentence "鬼啊" commits remaining buffer
        CHECK(pickGuiA.consumed && pickGuiA.commitText == "什么鬼啊");

        // Config roundtrip for inlinePreedit
        auto cfgPath = std::filesystem::temp_directory_path() / "liteime_test_preedit_config.ini";
        std::filesystem::remove(cfgPath);
        eng.config().inlinePreedit = InlinePreeditMode::CommittedPlusPinyin;
        CHECK(UserDict::saveConfig(cfgPath, eng.config()));
        EngineConfig reloaded;
        CHECK(UserDict::loadConfig(cfgPath, reloaded));
        CHECK(reloaded.inlinePreedit == InlinePreeditMode::CommittedPlusPinyin);
    }

    // 19. Argon review regressions: tail-partial whole sentence, m->n typos (sam/zem), exact prefix > jianpin prefix (78 > 72)
    {
        BilingualEngine eng;
        eng.setDictionary(&dict);

        // 19a. Tail partial syllable in long sentence: "wozaixieyigeshuruf" -> "我在写一个输入法"
        typeWord(eng, "wozaixieyigeshuruf");
        CHECK(!eng.currentPageItems().empty());
        bool foundFullSent = false;
        for (const auto& it : eng.currentPageItems()) {
            if (it.text == "我在写一个输入法") {
                foundFullSent = true;
                break;
            }
        }
        CHECK(foundFullSent);
        eng.handleKey(0x1B, 0, false);

        // 19b. Trailing 'm' -> 'n' adjacent-key typo: sam -> 三, zem -> 怎, zemme -> 怎么
        auto hasCand = [&](const std::string& w) {
            for (const auto& c : eng.allCandidates()) {
                if (c.text == w) return true;
            }
            return false;
        };
        typeWord(eng, "sam");
        CHECK(hasCand("三"));
        eng.handleKey(0x1B, 0, false);

        typeWord(eng, "zem");
        CHECK(hasCand("怎"));
        eng.handleKey(0x1B, 0, false);

        typeWord(eng, "zemme");
        CHECK(hasCand("怎么"));
        eng.handleKey(0x1B, 0, false);

        // 19c. Partial-prefix exact full-syllable phrase (78) beats partial-prefix Jianpin (72):
        // "xianzguia" has [xi, an] ("西安", 78) and [xi, an, z*] ("西安站", 72).
        typeWord(eng, "xianzguia");
        CHECK(!eng.currentPageItems().empty());
        CHECK(eng.currentPageItems()[0].text == "西安");
    }

    // 20. Explicit trailing apostrophe separator stays visible immediately (e.g. "dan'" before 'g')
    {
        BilingualEngine eng;
        eng.setDictionary(&dict);
        eng.config().inlinePreedit = InlinePreeditMode::CommittedPlusPinyin;
        typeWord(eng, "dan");
        auto rApos = eng.handleKey(0xDE, 0, false); // VK_OEM_7 (')
        CHECK(rApos.consumed && rApos.showCandidates);
        CHECK(rApos.headerBuffer == "dan'");
        CHECK(rApos.preeditText == "dan'");
        auto rG = eng.handleKey('G', 0, false);
        CHECK(rG.consumed && rG.headerBuffer == "dan'g" && rG.preeditText == "dan'g");
    }

    // 21. Extensible Common English display in Chinese mode (exact collision + prefix + toggle + custom .txt EN dict)
    {
        auto customTxt = std::filesystem::temp_directory_path() / "liteime_test_custom_en.txt";
        {
            std::ofstream ofs(customTxt);
            ofs << "# Custom English words\n"
                << "Antigravity\n"
                << "you\t230000\n"
                << "hello\t240000\n";
        }
        std::vector<RawPhraseRecord> customPhrases;
        std::vector<RawEnglishRecord> customEnglish;
        CHECK(BinaryDict::parseDictYaml(customTxt, customPhrases, customEnglish, false));
        CHECK(customEnglish.size() == 3);

        auto customDictFile = std::filesystem::temp_directory_path() / "liteime_test_custom_en.dict";
        CHECK(BinaryDict::compileToFile(customDictFile, std::move(customPhrases), std::move(customEnglish)));
        BinaryDict extraEn;
        CHECK(extraEn.loadFile(customDictFile));

        BilingualEngine eng;
        eng.setDictionary(&dict);
        eng.addExtraDictionary(&extraEn);
        eng.config().pageSize = 6;

        // "you" collides with Chinese 'you', yet common EN "you" surfaces visibly on Page 1 while Chinese stays #1
        typeWord(eng, "you");
        auto pageYou = eng.currentPageItems();
        CHECK(pageYou.size() >= 3);
        CHECK(pageYou[0].kind != CandidateKind::EnglishWord);
        bool hasYouOnPage1 = false;
        for (const auto& item : pageYou) {
            if (item.kind == CandidateKind::EnglishWord && item.text == "you") hasYouOnPage1 = true;
        }
        CHECK(hasYouOnPage1);
        eng.handleKey(0x1B, 0, false);

        // Prefix "anti" resolves custom English word "Antigravity" visibly on Page 1
        typeWord(eng, "anti");
        bool hasAntigravity = false;
        for (const auto& item : eng.currentPageItems()) {
            if (item.kind == CandidateKind::EnglishWord && item.text == "Antigravity") hasAntigravity = true;
        }
        CHECK(hasAntigravity);
        eng.handleKey(0x1B, 0, false);

        // Prefix "hel" surfaces "hello" visibly on Page 1
        typeWord(eng, "hel");
        bool hasHelloOnPage1 = false;
        for (const auto& item : eng.currentPageItems()) {
            if (item.kind == CandidateKind::EnglishWord && item.text == "hello") hasHelloOnPage1 = true;
        }
        CHECK(hasHelloOnPage1);
        eng.handleKey(0x1B, 0, false);

        // Turning off mixedEnglish suppresses [EN] display in Chinese mode
        eng.config().mixedEnglish = false;
        typeWord(eng, "hello");
        for (const auto& item : eng.allCandidates()) {
            CHECK(item.kind != CandidateKind::EnglishWord);
        }
        eng.clearExtraDictionaries();
        extraEn.unload();
        std::error_code ec;
        std::filesystem::remove(customTxt, ec);
        std::filesystem::remove(customDictFile, ec);
    }

    // 22. Argon Round 2 Batch A: Ctrl+Delete removal, Tab paging/selection, Quote backspace/focus, Dirty saveIfDirty, statusPos config
    {
        auto bDir = tmpDir / "liteime_test_r2a";
        std::filesystem::create_directories(bDir);
        auto bUserPath = bDir / "user_dict.tsv";
        auto bPinsPath = bDir / "pins.tsv";
        auto bCfgPath = bDir / "config.ini";
        std::filesystem::remove(bUserPath);
        std::filesystem::remove(bPinsPath);
        std::filesystem::remove(bCfgPath);

        BilingualEngine eng;
        eng.setDictionary(&dict);
        eng.userDict().load(bUserPath);
        CHECK(!eng.userDict().dirty());

        // Learn "ni hao" -> "拟好" multiple times so it becomes top ranked; verify zero synchronous disk write yet!
        eng.userDict().recordSelection("ni hao", "拟好");
        eng.userDict().recordSelection("ni hao", "拟好");
        eng.userDict().recordSelection("ni hao", "拟好");
        typeWord(eng, "nihao");
        CHECK(!eng.currentPageItems().empty());
        CHECK(eng.currentPageItems()[0].text == "拟好");
        auto commitRes = eng.handleKey(0x20, 0, false); // Space commits without synchronous disk I/O
        CHECK(commitRes.commitText == "拟好");
        CHECK(eng.userDict().dirty());
        CHECK(!std::filesystem::exists(bUserPath)); // No blocking disk write on hot commit path!

        // NotifyFocusChanged(false) flushes dirty userDict_ to disk cleanly
        eng.notifyFocusChanged(false);
        CHECK(!eng.userDict().dirty());
        CHECK(std::filesystem::exists(bUserPath));

        // Now type "nihao", press Ctrl+Delete (0x2E) -> removes mislearned "拟好", drops back to default "你好"
        typeWord(eng, "nihao");
        CHECK(eng.currentPageItems()[0].text == "拟好");
        CHECK(eng.handleKey(0x2E, ipc::ModCtrl, false, true).consumed); // OnTestKeyDown consumes Ctrl+Delete
        auto delRes = eng.handleKey(0x2E, ipc::ModCtrl, false, false);
        CHECK(delRes.consumed && delRes.showCandidates);
        CHECK(!eng.currentPageItems().empty() && eng.currentPageItems()[0].text == "你好");
        CHECK(eng.userDict().dirty());
        CHECK(eng.userDict().saveIfDirty());
        CHECK(!eng.userDict().dirty());
        eng.handleKey(0x1B, 0, false);

        // Tab / Shift+Tab while composing: multi-page flips page, single-page moves selection
        eng.config().pageSize = 3;
        typeWord(eng, "nihao");
        CHECK(eng.totalPages() >= 2);
        CHECK(eng.currentPage() == 0);
        CHECK(eng.handleKey(0x09, 0, false, true).consumed); // Tab intercepted on TestKeyDown
        CHECK(eng.handleKey(0x09, ipc::ModShift, false, true).consumed); // Shift+Tab intercepted too
        eng.handleKey(0x09, 0, false); // Tab -> page 1
        CHECK(eng.currentPage() == 1);
        eng.handleKey(0x09, ipc::ModShift, false); // Shift+Tab -> page 0
        CHECK(eng.currentPage() == 0);
        eng.handleKey(0x1B, 0, false);

        // Single page Tab selection cycling (w + extra or 'xi' etc. -> check "srf" or "er" or "nihao" with pageSize=8)
        eng.config().pageSize = 8;
        typeWord(eng, "wulu"); // only "五路","无路","无卤","wulu" -> <= 1 page
        if (eng.totalPages() == 1 && eng.currentPageItems().size() >= 2) {
            CHECK(eng.selectedIndexInPage() == 0);
            eng.handleKey(0x09, 0, false);
            CHECK(eng.selectedIndexInPage() == 1);
            eng.handleKey(0x09, ipc::ModShift, false);
            CHECK(eng.selectedIndexInPage() == 0);
        }
        eng.handleKey(0x1B, 0, false);

        // Quote toggle + immediate Backspace restores parity, and notifyFocusChanged(!gained) resets to open quote
        auto q1 = eng.handleKey(0xDE, ipc::ModShift, false); // “
        CHECK(q1.commitText == "“");
        auto bs = eng.handleKey(0x08, 0, false); // Backspace passes through & restores open quote
        CHECK(!bs.consumed);
        auto q1Again = eng.handleKey(0xDE, ipc::ModShift, false); // Still “!
        CHECK(q1Again.commitText == "“");
        eng.notifyFocusChanged(false); // Lost focus -> next quote resets to “ even without backspace
        auto qAfterFocus = eng.handleKey(0xDE, ipc::ModShift, false);
        CHECK(qAfterFocus.commitText == "“");

        // Config roundtrip for statusPosX / statusPosY
        EngineConfig cfgOut;
        cfgOut.statusPosX = 1234;
        cfgOut.statusPosY = 789;
        CHECK(UserDict::saveConfig(bCfgPath, cfgOut));
        EngineConfig cfgIn;
        CHECK(UserDict::loadConfig(bCfgPath, cfgIn));
        CHECK(cfgIn.statusPosX == 1234 && cfgIn.statusPosY == 789);
    }

    // 23. Header display always preserves every actual typed character (regression for "qingn" -> "qing")
    {
        BilingualEngine eng;
        eng.setDictionary(&dict);
        eng.config().inlinePreedit = InlinePreeditMode::CommittedPlusPinyin;

        typeWord(eng, "qingn");
        CHECK(eng.formattedHeader() == "qing'n");
        auto qingnUp = eng.handleKey('N', 0, true);
        CHECK(qingnUp.preeditText == "qing'n");
        // All matching multi-syllable "qing n*" words MUST rank at top, never hijacked by TypoCorrection "(qing)"
        auto qPage = eng.currentPageItems();
        CHECK(qPage.size() >= 4);
        CHECK(qPage[0].text == "青年");
        CHECK(qPage[1].text == "请你");
        CHECK(qPage[2].text == "请您");
        CHECK(qPage[3].text == "请");
        for (const auto& c : eng.allCandidates()) {
            CHECK(c.kind != CandidateKind::TypoCorrection);
            CHECK(c.comment != "(qing)");
        }
        eng.handleKey(0x1B, 0, false);

        typeWord(eng, "zhogn");
        CHECK(!eng.currentPageItems().empty() && eng.currentPageItems()[0].text == "中");
        std::string stripped;
        for (char c : eng.formattedHeader()) {
            if (c != '\'') stripped.push_back(c);
        }
        CHECK(stripped == "zhogn");
        eng.handleKey(0x1B, 0, false);

        // Mixed-case typed input keeps exact casing in header without stray apostrophes
        typeWord(eng, "pYthon");
        CHECK(eng.formattedHeader() == "pYthon");
        eng.handleKey(0x1B, 0, false);
    }

    // 24. wei'bi / weibi never leaks bogus secondary "we" Jianpin ("王二"/"为恶"/"婉儿"), and LooseInitial respects OFF default & <= 2 high-freq cap
    {
        BilingualEngine eng;
        eng.setDictionary(&dict);
        CHECK(eng.config().looseInitialMatch == false);

        typeWord(eng, "wei'bi");
        auto checkNoGarbage = [&]() {
            for (const auto& c : eng.allCandidates()) {
                CHECK(c.text != "王二");
                CHECK(c.text != "为恶");
                CHECK(c.text != "婉儿");
                CHECK(c.text != "无病");
            }
        };
        checkNoGarbage();
        for (const auto& c : eng.allCandidates()) {
            CHECK(c.kind != CandidateKind::LooseInitial);
        }
        eng.handleKey(0x1B, 0, false);

        eng.config().looseInitialMatch = true;
        typeWord(eng, "weibi");
        checkNoGarbage();
        size_t looseCount = 0;
        for (const auto& c : eng.allCandidates()) {
            if (c.kind == CandidateKind::LooseInitial) {
                ++looseCount;
                CHECK(c.text == "我不" || c.text == "外部" || c.text == "文本");
            }
        }
        CHECK(looseCount == 2);
        eng.handleKey(0x1B, 0, false);

        EngineConfig cfgOut;
        CHECK(cfgOut.looseInitialMatch == false);
        cfgOut.looseInitialMatch = true;
        auto tmpCfg = std::filesystem::temp_directory_path() / "liteime_loose_cfg.ini";
        CHECK(UserDict::saveConfig(tmpCfg, cfgOut));
        EngineConfig cfgIn;
        CHECK(UserDict::loadConfig(tmpCfg, cfgIn));
        CHECK(cfgIn.looseInitialMatch == true);
        std::filesystem::remove(tmpCfg);
    }

    // 25. Mandarin u/v alias normalization: jv(居), nv(女), qv(去), xv(需), yv(雨), jve/qvan/nue/lue & multi-syllable words
    {
        CHECK(PinyinTable::canonicalizeSyllable("jv") == "ju");
        CHECK(PinyinTable::canonicalizeSyllable("qv") == "qu");
        CHECK(PinyinTable::canonicalizeSyllable("xv") == "xu");
        CHECK(PinyinTable::canonicalizeSyllable("yv") == "yu");
        CHECK(PinyinTable::canonicalizeSyllable("nv") == "nv");
        CHECK(PinyinTable::canonicalizeSyllable("nu") == "nu");
        CHECK(PinyinTable::canonicalizeSyllable("nue") == "nve");
        CHECK(PinyinTable::canonicalizeSyllable("lue") == "lve");

        BilingualEngine eng;
        eng.setDictionary(&dict);
        eng.config().inlinePreedit = InlinePreeditMode::CommittedPlusPinyin;

        auto expectTop = [&](const std::string& keys, const std::string& expectedHdr, const std::string& expectedTop) {
            typeWord(eng, keys);
            CHECK(eng.formattedHeader() == expectedHdr);
            auto items = eng.currentPageItems();
            CHECK(!items.empty() && items[0].text == expectedTop);
            CHECK(items[0].kind != CandidateKind::TypoCorrection);
            eng.handleKey(0x1B, 0, false);
        };

        expectTop("jv", "jv", "居");
        expectTop("nv", "nv", "女");
        expectTop("nu", "nu", "怒");
        expectTop("qv", "qv", "去");
        expectTop("xv", "xv", "需");
        expectTop("yv", "yv", "雨");
        expectTop("jve", "jve", "觉");
        expectTop("qvan", "qvan", "全");
        expectTop("nue", "nue", "虐");
        expectTop("lue", "lue", "略");
        expectTop("nver", "nv'er", "女儿");
        expectTop("qvnian", "qv'nian", "去年");
        expectTop("yvyan", "yv'yan", "语言");
        expectTop("jveding", "jve'ding", "决定");

        // Typo inside jv/qv/xv/yv tokens still resolves (e.g. qvxan -> quxan -> quan)
        typeWord(eng, "qvxan");
        CHECK(!eng.currentPageItems().empty() && eng.currentPageItems()[0].text == "全");
        eng.handleKey(0x1B, 0, false);

        // User frequency learning (2 selections -> tier 1) shares canonical key across "jv" and "ju"
        for (int rep = 0; rep < 2; ++rep) {
            typeWord(eng, "jv");
            const auto items = eng.currentPageItems();
            int juIdx = -1;
            for (size_t i = 0; i < items.size(); ++i) {
                if (items[i].text == "局") { juIdx = static_cast<int>(i); break; }
            }
            CHECK(juIdx >= 0);
            auto sel = eng.handleKey(static_cast<uint32_t>('1' + juIdx), 0, false);
            CHECK(sel.commitText == "局");
        }
        typeWord(eng, "jv");
        CHECK(!eng.currentPageItems().empty() && eng.currentPageItems()[0].text == "局");
        eng.handleKey(0x1B, 0, false);
        typeWord(eng, "ju");
        CHECK(!eng.currentPageItems().empty() && eng.currentPageItems()[0].text == "局");
        eng.handleKey(0x1B, 0, false);
    }

    // Regression tests for Moduan 9-bug audit & Tier-1 optimizations
    {
        // Bug 1: All 5 missing EngineConfig fields persist & reload across config.ini
        EngineConfig cfgOut;
        cfgOut.showStatusBar = false;
        cfgOut.fullWidth = true;
        cfgOut.smartDecimal = false;
        cfgOut.autoUrlEnglish = false;
        cfgOut.shiftTapToggle = false;
        auto cfgFile = tmpDir / "cfg_roundtrip.ini";
        CHECK(UserDict::saveConfig(cfgFile, cfgOut));
        EngineConfig cfgIn;
        CHECK(UserDict::loadConfig(cfgFile, cfgIn));
        CHECK(cfgIn.showStatusBar == false);
        CHECK(cfgIn.fullWidth == true);
        CHECK(cfgIn.smartDecimal == false);
        CHECK(cfgIn.autoUrlEnglish == false);
        CHECK(cfgIn.shiftTapToggle == false);

        // Bug 5: copyUtf8Bounded never splits multi-byte UTF-8 characters
        const std::string utf8Text = "你好世界"; // 4 x 3 bytes = 12 bytes
        char smallBuf[8]{};
        size_t copied = copyUtf8Bounded(utf8Text, smallBuf, sizeof(smallBuf));
        CHECK(copied == 6); // 2 complete 3-byte chars fit in 7 usable bytes
        CHECK(std::string(smallBuf) == "你好");

        // Bug 6: BinaryDict::loadFromMemory validates offsets and rejects truncated/corrupted tables
        std::vector<uint8_t> badBuf(sizeof(DictHeader), 0);
        DictHeader hdr{};
        hdr.magic = 0x4C494D31;
        hdr.version = 1;
        hdr.phraseCount = 999999;
        hdr.phraseOffset = sizeof(DictHeader);
        std::memcpy(badBuf.data(), &hdr, sizeof(hdr));
        BinaryDict corruptDict;
        CHECK(!corruptDict.loadFromMemory(badBuf));

        // Bug 6b: BinaryDict::lookupJianpin skips corrupted out-of-bounds phraseIdx safely
        auto validFile = tmpDir / "oob_jp_test.dict";
        CHECK(BinaryDict::compileToFile(validFile, {{"你好", "ni hao", 1000, 2}}, {}));
        std::ifstream ifs(validFile, std::ios::binary | std::ios::ate);
        auto fsz = ifs.tellg();
        ifs.seekg(0);
        std::vector<uint8_t> rawDictBytes(static_cast<size_t>(fsz));
        CHECK(static_cast<bool>(ifs.read(reinterpret_cast<char*>(rawDictBytes.data()), fsz)));
        auto* rawHdr = reinterpret_cast<DictHeader*>(rawDictBytes.data());
        if (rawHdr->jianpinCount > 0) {
            auto* jpEntries = reinterpret_cast<JianpinEntry*>(rawDictBytes.data() + rawHdr->jianpinOffset);
            jpEntries[0].phraseIdx = 99999;
        }
        BinaryDict oobDict;
        CHECK(oobDict.loadFromMemory(rawDictBytes));
        CHECK(oobDict.lookupJianpin("nh", 8).empty());

        // Bug 7: lookupPinyinPrefix scans beyond 256 lexicon-prefix entries (e.g. >260 "wo sha..." before "wo shi")
        std::vector<RawPhraseRecord> bigPrefixPhrases;
        for (int i = 0; i < 300; ++i) {
            bigPrefixPhrases.push_back({"占位", "wo sha", 100, 2});
        }
        bigPrefixPhrases.push_back({"我是", "wo shi", 900000, 2});
        auto bigPrefixFile = tmpDir / "big_prefix_test.dict";
        CHECK(BinaryDict::compileToFile(bigPrefixFile, bigPrefixPhrases, {}));
        BinaryDict bigPrefixDict;
        CHECK(bigPrefixDict.loadFile(bigPrefixFile));
        auto pHits = bigPrefixDict.lookupPinyinPrefix("wo sh", 10);
        CHECK(!pHits.empty() && pHits[0].word == "我是");

        // Bug 8: quick_input.tsv escapes \, \t, \r, \n cleanly & atomic write leaves no .tmp file
        UserDict qiUd;
        auto qiDictPath = tmpDir / "qi_dir" / "user_dict.tsv";
        qiUd.load(qiDictPath);
        qiUd.setQuickInputs({{"multi\\line", "line1\nline2\tcol2\r\nend"}});
        CHECK(qiUd.saveQuickInputs());
        CHECK(!std::filesystem::exists(qiUd.quickInputsPath().wstring() + L".tmp"));
        UserDict qiReloaded;
        qiReloaded.load(qiDictPath);
        CHECK(qiReloaded.quickInputs().size() == 1);
        CHECK(qiReloaded.quickInputs()[0].label == "multi\\line");
        CHECK(qiReloaded.quickInputs()[0].value == "line1\nline2\tcol2\r\nend");

        // Bug 9: Numpad digits + '.' smartDecimal, plus fullWidth ASCII conversion
        BilingualEngine numEng;
        numEng.setDictionary(&bigPrefixDict);
        auto num3 = numEng.handleKey(0x63, 0, false); // VK_NUMPAD3
        CHECK(!num3.consumed);
        auto dotRes = numEng.handleKey(0xBE, 0, false); // VK_OEM_PERIOD -> half-width '.'
        CHECK(dotRes.consumed && dotRes.commitText == ".");

        BilingualEngine fwEng;
        fwEng.setDictionary(&bigPrefixDict);
        fwEng.config().chineseMode = false;
        fwEng.config().fullWidth = true;
        auto fwA = fwEng.handleKey('A', 0, false);
        CHECK(fwA.consumed && fwA.commitText == "\xEF\xBD\x81"); // 'ａ' (U+FF41)

        // Tier-1 Dir 2 & Dir 4: 2-char [EN] "we" demotion & "fangan" dual-path frequency fairness
        std::vector<RawPhraseRecord> tier1Phrases = {
            {"为", "wei", 50000, 1},
            {"反感", "fan gan", 3000, 2},
            {"方案", "fang an", 50000, 2},
        };
        std::vector<RawEnglishRecord> tier1En = {
            {"we", "we", 50000},
            {"ok", "ok", 50000},
        };
        auto t1File = tmpDir / "tier1_test.dict";
        CHECK(BinaryDict::compileToFile(t1File, tier1Phrases, tier1En));
        BinaryDict t1Dict;
        CHECK(t1Dict.loadFile(t1File));
        BilingualEngine t1Eng;
        t1Eng.setDictionary(&t1Dict);

        typeWord(t1Eng, "we");
        auto weItems = t1Eng.currentPageItems();
        CHECK(!weItems.empty() && weItems[0].text == "为" && weItems[0].kind != CandidateKind::EnglishWord);
        t1Eng.handleKey(0x1B, 0, false);

        typeWord(t1Eng, "fangan");
        auto fgItems = t1Eng.currentPageItems();
        CHECK(!fgItems.empty() && fgItems[0].text == "方案");
        CHECK(t1Eng.formattedHeader() == "fang'an");
        t1Eng.handleKey(0x1B, 0, false);

        // Codex Finding 2: UserDict::removeEntry & unpin last-item and non-last-item safety + saveIfDirty before reload
        {
            UserDict udFix;
            auto udFile = tmpDir / "codex_ud" / "user_dict.tsv";
            std::filesystem::remove_all(udFile.parent_path());
            udFix.load(udFile);
            udFix.recordSelection("ceshi", "测试");
            udFix.recordSelection("ceshi", "测视");
            CHECK(udFix.dirty());
            // Save before reload preserves unsaved entries
            CHECK(udFix.saveIfDirty());
            udFix.load(udFile);
            CHECK(udFix.lookup("ceshi").size() == 2);
            // Delete non-last then last item under same key (must not UAF on vec after erase)
            CHECK(udFix.removeEntry("ceshi", "测视"));
            CHECK(udFix.dirty());
            CHECK(!udFix.removeEntry("ceshi", "不存在"));
            CHECK(udFix.removeEntry("ceshi", "测试"));
            CHECK(udFix.lookup("ceshi").empty());
            CHECK(udFix.dirty());

            udFix.setPin("ni", 0, "你");
            udFix.setPin("ni", 1, "拟");
            CHECK(!udFix.unpin("ni", "不存在"));
            CHECK(udFix.unpin("ni", "拟"));
            CHECK(udFix.unpin("ni", "你"));
            CHECK(udFix.getPins("ni").empty());
            CHECK(!udFix.unpin("ni", "你"));
        }

        // Codex Finding 3: Auto URL / Email detection (www.example.com, alice@example.com, https://example.com)
        {
            BilingualEngine urlEng;
            urlEng.setDictionary(&t1Dict);
            urlEng.config().autoUrlEnglish = true;

            auto sendSeq = [&](std::string_view seq) {
                for (char c : seq) {
                    if (c >= 'a' && c <= 'z') {
                        urlEng.handleKey(static_cast<uint32_t>(c - 'a' + 'A'), 0, false);
                    } else if (c >= '0' && c <= '9') {
                        urlEng.handleKey(static_cast<uint32_t>(c), 0, false);
                    } else if (c == '.') {
                        urlEng.handleKey(0xBE, 0, false); // VK_OEM_PERIOD
                    } else if (c == '/') {
                        urlEng.handleKey(0xBF, 0, false); // VK_OEM_2
                    } else if (c == '-') {
                        urlEng.handleKey(0xBD, 0, false); // VK_OEM_MINUS
                    } else if (c == '@') {
                        urlEng.handleKey('2', 1u /* ModShift */, false);
                    } else if (c == ':') {
                        urlEng.handleKey(0xBA, 1u /* ModShift */, false); // VK_OEM_1
                    }
                }
            };

            // 1) www.example.com
            sendSeq("www.example.com");
            CHECK(urlEng.isComposing());
            auto wItems = urlEng.currentPageItems();
            CHECK(!wItems.empty() && wItems[0].text == "www.example.com");
            auto wCommit = urlEng.handleKey(0x0D, 0, false); // Enter
            CHECK(wCommit.consumed && wCommit.commitText == "www.example.com");

            // 2) alice@example.com
            sendSeq("alice@example.com");
            CHECK(urlEng.isComposing());
            auto eItems = urlEng.currentPageItems();
            CHECK(!eItems.empty() && eItems[0].text == "alice@example.com");
            // Verify Backspace all the way out of '@' exits URL mode cleanly
            auto eCommit = urlEng.handleKey(0x20, 0, false); // Space commits candidate 0
            CHECK(eCommit.consumed && eCommit.commitText == "alice@example.com");

            // 3) https://example.com + Backspace past trigger exits urlMode_
            sendSeq("https://example.com");
            auto hItems = urlEng.currentPageItems();
            CHECK(!hItems.empty() && hItems[0].text == "https://example.com");
            auto hCommit = urlEng.handleKey(0x0D, 0, false);
            CHECK(hCommit.consumed && hCommit.commitText == "https://example.com");

            sendSeq("alice@b");
            urlEng.handleKey(0x08, 0, false); // drop 'b' -> "alice@" (still URL mode)
            CHECK(!urlEng.currentPageItems().empty() && urlEng.currentPageItems()[0].text == "alice@");
            urlEng.handleKey(0x08, 0, false); // drop '@' -> "alice" (exits URL mode -> back to Pinyin/EN)
            CHECK(urlEng.currentPageItems().empty() || urlEng.currentPageItems()[0].comment != "[URL]");
            urlEng.handleKey(0x1B, 0, false);
        }

        // Enhancement 1: Bigram Beam Viterbi DP + User Bigram co-occurrence learning
        {
            std::vector<RawPhraseRecord> biPhrases = {
                {"是", "shi", 9000, 1},
                {"事", "shi", 4000, 1},
                {"时", "shi", 3500, 1},
                {"没", "mei", 8000, 1},
                {"美", "mei", 5000, 1},
                {"好", "hao", 9500, 1},
                {"豪", "hao", 2000, 1},
                {"的", "de", 20000, 1},
                {"我", "wo", 15000, 1},
                {"在", "zai", 12000, 1},
                {"写", "xie", 6000, 1},
                {"谢", "xie", 6200, 1}, // slightly higher unigram freq than "写"
                {"测试", "ce shi", 8000, 2},
                {"策划", "ce hua", 8000, 2},
            };
            std::filesystem::path biDictPath = tmpDir / "bigram_test.dict";
            CHECK(BinaryDict::compileToFile(biDictPath, biPhrases, {}));
            BinaryDict biDict;
            CHECK(biDict.loadFile(biDictPath));

            BilingualEngine biEng;
            biEng.setDictionary(&biDict);
            // "写" has lower raw unigram freq than "谢", but builtin/custom bigram "在 -> 写" wins over "在 -> 谢"
            for (char ch : std::string("zaixie")) {
                biEng.handleKey(static_cast<uint32_t>(ch - 'a' + 'A'), 0, false);
            }
            auto bItems = biEng.currentPageItems();
            CHECK(!bItems.empty() && bItems[0].text == "在写");
            biEng.handleKey(0x1B, 0, false);

            // "mei shi": without context "是" > "事", with bigram "没 -> 事" ("mei shi" -> "没事") wins!
            for (char ch : std::string("meishi")) {
                biEng.handleKey(static_cast<uint32_t>(ch - 'a' + 'A'), 0, false);
            }
            auto mItems = biEng.currentPageItems();
            CHECK(!mItems.empty() && mItems[0].text == "没事");
            biEng.handleKey(0x1B, 0, false);

            // Dynamic user bigram learning & persistence
            std::filesystem::path biUserPath = tmpDir / "bigram_user" / "user_dict.tsv";
            biEng.userDict().load(biUserPath);
            biEng.userDict().recordBigram("测试", "策划");
            biEng.userDict().recordBigram("测试", "策划");
            CHECK(biEng.userDict().dirty());
            CHECK(biEng.userDict().saveIfDirty());
            CHECK(!biEng.userDict().dirty());
            CHECK(std::filesystem::exists(biEng.userDict().bigramsPath()));

            UserDict biReload;
            biReload.load(biUserPath);
            CHECK(biReload.getBigramBonus("测试", "策划") > 1.0);
        }

        // Enhancement 2: Double Pinyin (Xiaohe, Microsoft, Ziranma, Sogou) + Scheme Config persistence
        {
            auto xh1 = PinyinTable::decodeShuangpinPair('n', 'i', PinyinScheme::Xiaohe);
            CHECK(!xh1.empty() && xh1[0] == "ni");
            auto xh2 = PinyinTable::decodeShuangpinPair('h', 'c', PinyinScheme::Xiaohe);
            CHECK(!xh2.empty() && xh2[0] == "hao");
            auto xh3 = PinyinTable::decodeShuangpinPair('v', 'l', PinyinScheme::Xiaohe);
            CHECK(!xh3.empty() && xh3[0] == "zhuang");
            auto xh4 = PinyinTable::decodeShuangpinPair('a', 'n', PinyinScheme::Xiaohe);
            CHECK(!xh4.empty() && xh4[0] == "an");

            auto msIng = PinyinTable::decodeShuangpinPair('j', ';', PinyinScheme::Microsoft);
            CHECK(!msIng.empty() && msIng[0] == "jing");
            auto msZero = PinyinTable::decodeShuangpinPair('o', 'k', PinyinScheme::Sogou);
            CHECK(!msZero.empty() && msZero[0] == "ao");
            auto zr1 = PinyinTable::decodeShuangpinPair('u', 'd', PinyinScheme::Ziranma);
            CHECK(!zr1.empty() && zr1[0] == "shuang");

            auto spSeg = PinyinTable::segmentScheme("nihc", PinyinScheme::Xiaohe, 3);
            CHECK(!spSeg.empty() && spSeg[0].joinedKey() == "ni hao");

            std::vector<RawPhraseRecord> spPhrases = {
                {"你好", "ni hao", 10000, 2},
                {"北京", "bei jing", 9000, 2},
                {"小鹤", "xiao he", 8000, 2},
            };
            std::filesystem::path spDictPath = tmpDir / "sp_test.dict";
            CHECK(BinaryDict::compileToFile(spDictPath, spPhrases, {}));
            BinaryDict spDict;
            CHECK(spDict.loadFile(spDictPath));

            BilingualEngine spEng;
            spEng.setDictionary(&spDict);
            spEng.config().scheme = PinyinScheme::Xiaohe;
            for (char ch : std::string("nihc")) {
                spEng.handleKey(static_cast<uint32_t>(ch - 'a' + 'A'), 0, false);
            }
            auto spItems = spEng.currentPageItems();
            CHECK(!spItems.empty() && spItems[0].text == "你好");
            auto spCommit = spEng.handleKey(0x20, 0, false);
            CHECK(spCommit.consumed && spCommit.commitText == "你好");

            // Microsoft Shuangpin with ';' key for 'ing': "bei jing" -> "bzj;"
            spEng.config().scheme = PinyinScheme::Microsoft;
            spEng.handleKey('B', 0, false);
            spEng.handleKey('Z', 0, false); // bei
            spEng.handleKey('J', 0, false);
            spEng.handleKey(0xBA, 0, false); // j; -> jing
            auto msItems = spEng.currentPageItems();
            CHECK(!msItems.empty() && msItems[0].text == "北京");
            spEng.handleKey(0x1B, 0, false);

            // Config round-trip for PinyinScheme
            EngineConfig savedCfg{};
            savedCfg.scheme = PinyinScheme::Xiaohe;
            std::filesystem::path cfgPath = tmpDir / "scheme_config.ini";
            CHECK(UserDict::saveConfig(cfgPath, savedCfg));
            EngineConfig loadedCfg{};
            CHECK(UserDict::loadConfig(cfgPath, loadedCfg));
            CHECK(loadedCfg.scheme == PinyinScheme::Xiaohe);
        }

        // Codex Round 2 Fixes: QuickInput 4095-byte limit (no silent truncation) & UserDict::load dirty flush
        {
            UserDict udR2;
            const std::filesystem::path r2Dir = tmpDir / "codex_r2";
            std::filesystem::remove_all(r2Dir);
            const std::filesystem::path r2UserPath = r2Dir / "user_dict.tsv";
            udR2.load(r2UserPath);

            // Exact boundary: 4095 bytes allowed, 4096 bytes rejected without overwriting existing valid items
            CHECK(udR2.setQuickInputs({{"exact", std::string(UserDict::kMaxQuickInputBytes, 'a')}}));
            CHECK(udR2.quickInputs().size() == 1);
            CHECK(!udR2.setQuickInputs({{"too_long", std::string(UserDict::kMaxQuickInputBytes + 1, 'b')}}));
            CHECK(udR2.quickInputs().size() == 1);
            CHECK(udR2.quickInputs()[0].label == "exact");

            // loadQuickInputs skips oversized entries written directly to quick_input.tsv
            CHECK(udR2.saveQuickInputs());
            {
                std::ofstream ofs(udR2.quickInputsPath(), std::ios::app | std::ios::binary);
                ofs << "oversize\t" << std::string(UserDict::kMaxQuickInputBytes + 10, 'z') << "\n";
                ofs << "valid2\tok2\n";
            }
            CHECK(udR2.loadQuickInputs(udR2.quickInputsPath()));
            CHECK(udR2.quickInputs().size() == 2);
            CHECK(udR2.quickInputs()[0].label == "exact");
            CHECK(udR2.quickInputs()[1].label == "valid2");

            // UserDict::load auto-flushes dirty user_dict & user_bigram before clearing/reloading
            udR2.recordSelection("baoliu", "保留");
            udR2.recordBigram("保留", "记录");
            CHECK(udR2.dirty());
            udR2.load(r2UserPath);
            CHECK(!udR2.lookup("baoliu").empty());
            CHECK(udR2.getBigramBonus("保留", "记录") > 1.0);
        }

        // One-shot assembled phrase memory & mixed/jianpin/full recall ("shuoxiesm" -> "说些什么" -> "sxsm" / "shuoxiesm")
        {
            std::vector<RawPhraseRecord> memPhrases = {
                {"说些", "shuo xie", 20000, 2},
                {"什么", "shen me", 50000, 2},
                {"说", "shuo", 15000, 1},
                {"些", "xie", 12000, 1},
                {"四", "si", 10000, 1},
                {"吗", "ma", 10000, 1},
                {"呢", "ne", 15000, 1},
                {"实现什么", "shi xian shen me", 40000, 4},
                {"思绪散漫", "si xu san man", 35000, 4},
            };
            const std::filesystem::path memDictPath = tmpDir / "mem_phrase.dict";
            CHECK(BinaryDict::compileToFile(memDictPath, memPhrases, {}));
            BinaryDict memDict;
            CHECK(memDict.loadFile(memDictPath));

            const std::filesystem::path memUserPath = tmpDir / "mem_user" / "user_dict.tsv";
            std::filesystem::remove_all(memUserPath.parent_path());

            BilingualEngine memEng;
            memEng.setDictionary(&memDict);
            memEng.userDict().load(memUserPath);

            // Step 1: First-time composition on "shuoxiesm": select "说些", leaving "sm", then select "什么"
            typeWord(memEng, "shuoxiesm");
            int idxShuoXie = -1;
            for (size_t i = 0; i < memEng.currentPageItems().size(); ++i) {
                if (memEng.currentPageItems()[i].text == "说些") {
                    idxShuoXie = static_cast<int>(i);
                    break;
                }
            }
            CHECK(idxShuoXie >= 0);
            auto s1 = memEng.selectCandidateOnPage(idxShuoXie);
            CHECK(s1.consumed && s1.commitText.empty());
            CHECK(memEng.isComposing() && memEng.rawBuffer() == "sm");

            int idxShenMe = -1;
            for (size_t i = 0; i < memEng.currentPageItems().size(); ++i) {
                if (memEng.currentPageItems()[i].text == "什么") {
                    idxShenMe = static_cast<int>(i);
                    break;
                }
            }
            CHECK(idxShenMe >= 0);
            auto s2 = memEng.selectCandidateOnPage(idxShenMe);
            CHECK(s2.consumed && s2.commitText == "说些什么");

            // Step 2: Immediately recall in 1 shot via mixed "shuoxiesm", pure jianpin "sxsm", "shxsm", full "shuoxieshenme"
            typeWord(memEng, "shuoxiesm");
            CHECK(!memEng.currentPageItems().empty() && memEng.currentPageItems()[0].text == "说些什么");
            memEng.handleKey(0x1B, 0, false);

            typeWord(memEng, "sxsm");
            CHECK(!memEng.currentPageItems().empty() && memEng.currentPageItems()[0].text == "说些什么");
            memEng.handleKey(0x1B, 0, false);

            typeWord(memEng, "shxsm");
            CHECK(!memEng.currentPageItems().empty() && memEng.currentPageItems()[0].text == "说些什么");
            memEng.handleKey(0x1B, 0, false);

            typeWord(memEng, "shuoxieshenme");
            CHECK(!memEng.currentPageItems().empty() && memEng.currentPageItems()[0].text == "说些什么");
            memEng.handleKey(0x1B, 0, false);

            // Prefix recall in longer query ("shuoxiesmne" surfaces "说些什么")
            typeWord(memEng, "shuoxiesmne");
            bool foundMemPrefix = false;
            for (const auto& c : memEng.allCandidates()) {
                if (c.text == "说些什么") {
                    foundMemPrefix = true;
                    break;
                }
            }
            CHECK(foundMemPrefix);
            memEng.handleKey(0x1B, 0, false);

            // Step 3: Persistence across save & reload (isPhrase flag preserved on disk)
            CHECK(memEng.userDict().saveIfDirty());
            BilingualEngine memReloaded;
            memReloaded.setDictionary(&memDict);
            CHECK(memReloaded.userDict().load(memUserPath));
            typeWord(memReloaded, "sxsm");
            CHECK(!memReloaded.currentPageItems().empty() && memReloaded.currentPageItems()[0].text == "说些什么");
            typeWord(memReloaded, "shuoxiesm");
            CHECK(!memReloaded.currentPageItems().empty() && memReloaded.currentPageItems()[0].text == "说些什么");

            // Step 4: Ctrl+Delete cleanly removes the learned phrase
            CHECK(memReloaded.handleKey(0x2E, 0x02 /*ModCtrl*/, false).consumed);
            bool stillPresent = false;
            for (const auto& c : memReloaded.allCandidates()) {
                if (c.text == "说些什么") stillPresent = true;
            }
            CHECK(!stillPresent);
            memReloaded.handleKey(0x1B, 0, false);
        }

        // First-time Quanpin mixed typing, Option A tiering, Shuangpin bugfix ("zhendema"), & Shuangpin+Quanpin mixed typing
        {
            std::vector<RawPhraseRecord> mixPhrases = {
                {"说些", "shuo xie", 15000, 2},
                {"什么", "shen me", 60000, 2},
                {"是", "shi", 80000, 1},
                {"上", "shang", 50000, 1},
                {"么", "me", 12000, 1},
                {"今天", "jin tian", 70000, 2},
                {"天气", "tian qi", 45000, 2},
                {"早安", "zao an", 25000, 2},
                {"杂", "za", 4000, 1},
                {"你好", "ni hao", 75000, 2},
                {"怎么样", "zen me yang", 42000, 3},
                {"我们", "wo men", 85000, 2},
                {"的", "de", 120000, 1},
                {"大", "da", 50000, 1},
                {"国家", "guo jia", 65000, 2},
                {"中国", "zhong guo", 90000, 2},
                {"人民", "ren min", 70000, 2},
                {"人民币", "ren min bi", 48000, 3},
                {"不好意思", "bu hao yi si", 35000, 4},
                {"真", "zhen", 30000, 1},
                {"真的", "zhen de", 68000, 2},
                {"真的吗", "zhen de ma", 55000, 3},
                {"吗", "ma", 45000, 1},
                {"脏", "zang", 3000, 1},
                {"嗯", "en", 1200, 1},
                {"喜", "xi", 9000, 1},
                {"喜欢", "xi huan", 60000, 2},
                {"真喜欢", "zhen xi huan", 28000, 3},
                {"大家", "da jia", 62000, 2},
                {"好", "hao", 70000, 1},
                {"谢谢", "xie xie", 70000, 2},
                {"我想", "wo xiang", 55000, 2},
                {"西安", "xi an", 30000, 2},
                {"先", "xian", 25000, 1},
                {"桑", "sang", 8000, 1},
                {"输入法", "shu ru fa", 40000, 3},
                {"助兴", "zhu xing", 18000, 2},
                {"实事求是", "shi shi qiu shi", 32000, 4},
                {"死", "si", 6000, 1},
                {"商", "shang", 5000, 1},
                {"秋", "qiu", 4000, 1},
                {"时", "shi", 20000, 1},
            };
            std::filesystem::path mixDictPath = tmpDir / "mixed_sp.dict";
            CHECK(BinaryDict::compileToFile(mixDictPath, mixPhrases, {}));
            BinaryDict mixDict;
            CHECK(mixDict.loadFile(mixDictPath));

            const std::filesystem::path mixUserPath = tmpDir / "mixed_user.tsv";
            std::filesystem::remove(mixUserPath);

            BilingualEngine mixEng;
            mixEng.setDictionary(&mixDict);
            mixEng.userDict().load(mixUserPath);

            // A. First-time Quanpin mixed decoding (zero prior UserDict history)
            auto checkFirstTop = [&](std::string_view input, std::string_view expectedText) {
                mixEng.handleKey(0x1B, 0, false);
                typeWord(mixEng, std::string(input));
                auto items = mixEng.currentPageItems();
                bool ok = !items.empty() && items[0].text == expectedText;
                if (!ok) {
                    std::cerr << "First-time mixed fail for [" << input << "]: got "
                              << (items.empty() ? "<empty>" : items[0].text) << " expected " << expectedText << "\n";
                    for (size_t idx = 0; idx < std::min<size_t>(5, items.size()); ++idx) {
                        std::cerr << "  [" << idx << "] " << items[idx].text
                                  << " key=" << items[idx].matchedKey
                                  << " sc=" << items[idx].score << "\n";
                    }
                }
                CHECK(ok);
                mixEng.handleKey(0x1B, 0, false);
            };
            checkFirstTop("shuoxiesm", "说些什么");
            checkFirstTop("jintiantq", "今天天气");
            checkFirstTop("nihaozmy", "你好怎么样");
            checkFirstTop("womendgj", "我们的国家");
            checkFirstTop("zhongguorm", "中国人民");
            checkFirstTop("jintianza", "今天早安");
            checkFirstTop("renmb", "人民币");
            checkFirstTop("zenmy", "怎么样");
            checkFirstTop("buhaoys", "不好意思");
            // Pure Jianpin idioms must NOT be displaced by random BuildWholeSentence guesses
            checkFirstTop("ssqs", "实事求是");

            // B. Option A: direct sentence commit records full canonical pinyin (count=1 -> tier 0, count=2 -> tier 1)
            typeWord(mixEng, "shuoxiesm");
            CHECK(!mixEng.currentPageItems().empty() && mixEng.currentPageItems()[0].text == "说些什么");
            auto pick1 = mixEng.handleKey(0x20, 0, false); // 1st direct pick
            CHECK(pick1.commitText == "说些什么");
            // Should be stored under canonical "shuo xie shen me" with count == 1, tier == 0
            auto learned1 = mixEng.userDict().lookup("shuo xie shen me");
            CHECK(!learned1.empty() && learned1[0].word == "说些什么" && learned1[0].count == 1 && !learned1[0].isPhrase);

            typeWord(mixEng, "shuoxiesm");
            auto pick2 = mixEng.handleKey(0x20, 0, false); // 2nd direct pick -> tier 1!
            CHECK(pick2.commitText == "说些什么");
            typeWord(mixEng, "sxsm");
            CHECK(!mixEng.currentPageItems().empty() && mixEng.currentPageItems()[0].text == "说些什么");
            mixEng.handleKey(0x1B, 0, false);

            // C. Shuangpin bugfixes & Shuangpin+Quanpin mixed typing
            mixEng.config().scheme = PinyinScheme::Xiaohe;

            // Screenshot regression: "zhendema" in Xiaohe must decode "真的吗" (NOT "脏嗯的吗") & clean header
            typeWord(mixEng, "zhendema");
            CHECK(!mixEng.currentPageItems().empty() && mixEng.currentPageItems()[0].text == "真的吗");
            CHECK(mixEng.segmentedHeader() == "zhen'de'ma");
            mixEng.handleKey(0x1B, 0, false);

            // Pure Quanpin words typed in Shuangpin mode (odd lengths & non-SP pairs) must work
            checkFirstTop("nihao", "你好");
            checkFirstTop("zhongguo", "中国");
            checkFirstTop("xiexie", "谢谢");
            checkFirstTop("woxiang", "我想");
            checkFirstTop("dajiahao", "大家好");

            // Genuine Shuangpin must stay prioritized on collisions
            checkFirstTop("xian", "西安"); // SP xi'an > QP xian ("先")
            checkFirstTop("nihc", "你好"); // SP ni hao
            checkFirstTop("sh", "桑");     // SP sang

            // Mixed Shuangpin + Quanpin ("vf" Xiaohe "zhen" + "xihuan" Quanpin -> "真喜欢")
            typeWord(mixEng, "vfxihuan");
            CHECK(!mixEng.currentPageItems().empty() && mixEng.currentPageItems()[0].text == "真喜欢");
            CHECK(mixEng.segmentedHeader() == "vf'xi'huan [zhen'xi'huan]");
            mixEng.handleKey(0x1B, 0, false);

            // Shuangpin Jianpin with apostrophes ("u'r'f" -> Xiaohe sh+r+f -> "输入法")
            typeWord(mixEng, "u'r'f");
            CHECK(!mixEng.currentPageItems().empty() && mixEng.currentPageItems()[0].text == "输入法");
            mixEng.handleKey(0x1B, 0, false);

            // Microsoft Shuangpin ';' on odd position AFTER a 3-letter Quanpin syllable ("zhux;" -> "zhu xing")
            mixEng.config().scheme = PinyinScheme::Microsoft;
            typeWord(mixEng, "zhux");
            mixEng.handleKey(0xBA, 0, false); // ';' completes xing
            CHECK(!mixEng.currentPageItems().empty() && mixEng.currentPageItems()[0].text == "助兴");
            mixEng.handleKey(0x1B, 0, false);
        }
    }

    std::cout << "All LiteIME engine tests passed!\n";
    return 0;
}
