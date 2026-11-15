#pragma once

#include <cstdint>
#include <string>

namespace liteime {

enum class SkinTheme : uint8_t {
    CloudMist = 0,
    MatchaJade = 1,
    SakuraCream = 2,
    ObsidianInk = 3,
};

enum class CandidateLayout : uint8_t {
    Horizontal = 0,
    Vertical = 1,
};

enum class InlinePreeditMode : uint8_t {
    CandidatePreview = 0,    // 输入框显示已确认汉字+首选项预览
    CommittedPlusPinyin = 1, // 输入框只显示已确认汉字+未确认拼音
    None = 2,                // 未最终确认前输入框内不出现字
};

struct FuzzyConfig {
    bool zh_z = false;
    bool ch_c = false;
    bool sh_s = false;
    bool n_l = false;
    bool r_l = false;
    bool f_h = false;
    bool an_ang = false;
    bool en_eng = false;
    bool in_ing = false;
    bool iang_ian = false;
    bool uang_uan = false;
};

struct EngineConfig {
    int pageSize = 5;              // 5..8 candidates per page (clamped 3..9 in engine)
    bool chineseMode = true;       // true = CN, false = EN
    bool fullWidth = false;        // false = half-width alnum
    bool chinesePunct = true;      // true = CN punctuation in CN mode
    bool mixedEnglish = true;      // show English candidates in CN mode
    bool smartDecimal = true;      // 3.14 stays '.'
    bool autoUrlEnglish = true;    // http:// www. email stays ASCII
    bool shiftTapToggle = true;    // tap Shift to toggle CN/EN
    bool showStatusBar = true;
    int statusPosX = -10000;       // persisted floating pill X (-10000 = unset/default)
    int statusPosY = -10000;       // persisted floating pill Y (-10000 = unset/default)
    bool typoCorrection = true;    // auto-correct obvious pinyin typos (zhogn/zhog -> zhong)
    bool enableCsPack = false;     // optional domain pack: computer.dict (OFF by default)
    bool enableExtPack = false;    // optional domain pack: ext_cn.dict (OFF by default)
    bool enableProPack = false;    // optional domain pack: pro_fields.dict (OFF by default)
    bool enableEnPack = true;      // extensible common English pack: common_en.dict (ON by default)
    bool enableCustomDicts = true; // load user custom dicts in %APPDATA%\LiteIME\dicts
    SkinTheme skin = SkinTheme::CloudMist;
    CandidateLayout layout = CandidateLayout::Horizontal;
    bool squareFrame = true;       // true = crisp square frame, false = capsule rounded frame
    InlinePreeditMode inlinePreedit = InlinePreeditMode::CandidatePreview;
    FuzzyConfig fuzzy{};
};

} // namespace liteime
