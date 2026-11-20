#include "engine/symbol_catalog.hpp"
#include "engine/engine_internal.hpp"

#include <algorithm>
#include <unordered_set>

namespace liteime::engine {

namespace {

int scorePipeList(const char* list, std::string_view queryLower) {
    if (!list || queryLower.empty()) return 0;
    std::string_view kws(list);
    size_t pos = 0;
    int best = 0;
    while (pos < kws.size()) {
        size_t next = kws.find('|', pos);
        if (next == std::string_view::npos) next = kws.size();
        std::string_view tok = kws.substr(pos, next - pos);
        if (!tok.empty()) {
            if (tok == queryLower) {
                best = std::max(best, 100);
            } else if (tok.starts_with(queryLower)) {
                best = std::max(best, 70);
            } else if (tok.find(queryLower) != std::string_view::npos) {
                best = std::max(best, 40);
            }
        }
        pos = next + 1;
    }
    return best;
}

int scoreKeywordMatch(const SymbolEntry& entry, std::string_view queryLower) {
    if (queryLower.empty()) return 1;
    return std::max(scorePipeList(entry.inlineTriggers, queryLower),
                    scorePipeList(entry.searchKeywords, queryLower));
}

bool exactInlineKeywordMatch(const SymbolEntry& entry, std::string_view concatLower) {
    if (entry.inlineSlot < 0 || !entry.inlineTriggers || concatLower.empty()) return false;
    std::string_view kws(entry.inlineTriggers);
    size_t pos = 0;
    while (pos < kws.size()) {
        size_t next = kws.find('|', pos);
        if (next == std::string_view::npos) next = kws.size();
        if (kws.substr(pos, next - pos) == concatLower) {
            return true;
        }
        pos = next + 1;
    }
    return false;
}

} // namespace

const std::vector<SymbolEntry>& SymbolCatalog::allEntries() {
    static const std::vector<SymbolEntry> kEntries = {
        // ==================== 1. 颜文字库 (Kaomoji) ====================
        {"(*^▽^*)",      "[开心]",   "kaixin|gaoxing",         "kx|gx|huanhu|smile|happy",  SymbolLibrary::Kaomoji, 3},
        {"(≧▽≦)",        "[大笑]",   "daxiao",                 "kaixin|gaoxing|haha|hh|happy", SymbolLibrary::Kaomoji, 3},
        {"ヽ(✿ﾟ▽ﾟ)ノ",   "[欢呼]",   "huanhu",                 "kaixin|chong|happy",        SymbolLibrary::Kaomoji, 3},
        {"(o゜▽゜)o☆",   "[惊喜]",   "jingxi",                 "kaixin|jx|happy",           SymbolLibrary::Kaomoji, 3},
        {"(^_^)",        "[微笑]",   "weixiao",                "wx|xiao|smile",             SymbolLibrary::Kaomoji, 3},
        {"(◍•ᴗ•◍)",      "[可爱]",   "keai|maimeng",           "ka|mm|meng|cute",           SymbolLibrary::Kaomoji, 3},
        {"(｡･ω･｡)",      "[卖萌]",   "",                       "keai|maimeng|guai|cute",    SymbolLibrary::Kaomoji, -1},
        {"(●'◡'●)",      "[甜甜]",   "tiantian",               "weixiao|keai|tiantie",      SymbolLibrary::Kaomoji, 3},
        {"(*/ω＼*)",     "[害羞]",   "haixiu|lianhong",        "hx|lh|wulian|shy",          SymbolLibrary::Kaomoji, 3},
        {"(//∇//)",      "[脸红]",   "",                       "haixiu|lianhong|shy",       SymbolLibrary::Kaomoji, -1},
        {"(ಥ_ಥ)",        "[难过]",   "nanguo|shangxin",        "ng|sx|lei|sad",             SymbolLibrary::Kaomoji, 3},
        {"(╥╯^╰╥)",     "[委屈]",   "weiqu",                  "wq|nanguo|shangxin|sad",    SymbolLibrary::Kaomoji, 3},
        {"(T_T)",        "[哭泣]",   "kuqi|daku",              "kq|dk|ku|wuku|cry",         SymbolLibrary::Kaomoji, 3},
        {"(っ˘̩╭╮˘̩)っ",   "[抱抱哭]", "",                       "kuqi|baobao|bb|weiqu|cry",  SymbolLibrary::Kaomoji, -1},
        {"(╬◣д◢)",      "[生气]",   "shengqi|fennu",          "sq|fn|qi|angry",            SymbolLibrary::Kaomoji, 3},
        {"(╯°□°）╯︵ ┻━┻", "[掀桌]", "xianzhuo",               "shengqi|fennu|angry",       SymbolLibrary::Kaomoji, 3},
        {"(￣ー￣)",      "[无语]",   "wuyu",                   "wy|han|liuhan|speechless",  SymbolLibrary::Kaomoji, 3},
        {"(-_-;)",       "[流汗]",   "liuhan|ganga",           "wuyu|han|gg|sweat",         SymbolLibrary::Kaomoji, 3},
        {"(ΩДΩ)",        "[惊讶]",   "jingya|zhenjing",        "jy|zj|wow|shock",           SymbolLibrary::Kaomoji, 3},
        {"Σ(°△°|||)︴",   "[震惊]",   "",                       "zhenjing|jingya|shock",     SymbolLibrary::Kaomoji, -1},
        {"(・_・?)",     "[疑惑]",   "yihuo",                  "yh|bujie|wenhao|doubt",     SymbolLibrary::Kaomoji, 3},
        {"(?_?)",        "[不懂]",   "",                       "yihuo|budong|doubt",        SymbolLibrary::Kaomoji, -1},
        {"(ง •̀_•́)ง",    "[加油]",   "jiayou",                 "jy|chongya|nuli|fight",     SymbolLibrary::Kaomoji, 3},
        {"٩(˃̶͈̀௰˂̶͈́)و",   "[冲鸭]",   "chongya",                "jiayou|cy|happy|fight",     SymbolLibrary::Kaomoji, 3},
        {"(๑•̀ㅂ•́)و✧",   "[点赞]",   "dianzan|bangbang",       "zan|bang|nb|good",          SymbolLibrary::Kaomoji, 3},
        {"(づ｡◕‿‿◕｡)づ", "[抱抱]",   "baobao",                 "bb|aixin|hug",              SymbolLibrary::Kaomoji, 3},
        {"(｡･ω･｡)ﾉ♡",    "[比心]",   "bixin",                  "bx|xiexie|ganxie|love",     SymbolLibrary::Kaomoji, 3},
        {"( ^_^)/~~",    "[再见]",   "zaijian|baibai",         "zj|bb|bye",                 SymbolLibrary::Kaomoji, 3},
        {"(－_－) zzZ",   "[困了]",   "kunle|keshui",           "kl|shuijiao|sj|sleep",      SymbolLibrary::Kaomoji, 3},
        {"┐(´д｀)┌",     "[无奈]",   "wunai|tanqi",            "wn|tq|shiwang|shrug",       SymbolLibrary::Kaomoji, 3},
        {"_(┐「ε:)_",    "[躺平]",   "tangping|moyu",          "tp|my|lan|lazy",            SymbolLibrary::Kaomoji, 3},

        // ==================== 2. 常用表情库 (Emoji) ====================
        {"😊", "[开心]",   "kaixin|gaoxing|weixiao", "kx|gx|wx|smile|happy",   SymbolLibrary::Emoji, 1},
        {"😄", "[大笑]",   "daxiao",                 "kaixin|haha|smile",      SymbolLibrary::Emoji, 1},
        {"😂", "[笑哭]",   "xiaoku",                 "xk|haha|hh|ganga|lol",   SymbolLibrary::Emoji, 1},
        {"🤣", "[笑死]",   "xiaosi",                 "xiaoku|rofl|lol",        SymbolLibrary::Emoji, 1},
        {"😍", "[喜欢]",   "xihuan|huachi",          "xh|aixin|love",          SymbolLibrary::Emoji, 1},
        {"🥰", "[比心]",   "bixin",                  "bx|tiantian|love",       SymbolLibrary::Emoji, 1},
        {"😘", "[亲亲]",   "qinqin",                 "qq|kiss",                SymbolLibrary::Emoji, 1},
        {"🥳", "[庆祝]",   "qingzhu|shengri",        "qz|sr|kuaile|party",     SymbolLibrary::Emoji, 1},
        {"😭", "[大哭]",   "daku|kuqi|nanguo|shangxin", "dk|kq|ng|sx|cry|sad", SymbolLibrary::Emoji, 1},
        {"🥺", "[委屈]",   "weiqu|kelian",           "wq|kl|baobao|plead",     SymbolLibrary::Emoji, 1},
        {"😡", "[生气]",   "shengqi|fennu",          "sq|fn|angry",            SymbolLibrary::Emoji, 1},
        {"😅", "[尴尬]",   "ganga|wuyu|liuhan",      "gg|wy|han|sweat",        SymbolLibrary::Emoji, 1},
        {"🤔", "[思考]",   "sikao|yihuo",            "sk|yh|think",            SymbolLibrary::Emoji, 1},
        {"😱", "[震惊]",   "zhenjing|jingya",        "zj|jy|xiadao|shock",     SymbolLibrary::Emoji, 1},
        {"😴", "[困了]",   "kunle|shuijiao|keshui",  "kl|sj|sleep",            SymbolLibrary::Emoji, 1},
        {"👍", "[点赞]",   "dianzan|bangbang",       "zan|dz|bang|nb|good|ok", SymbolLibrary::Emoji, 1},
        {"👏", "[鼓掌]",   "guzhang|paishou",        "gz|bang|clap",           SymbolLibrary::Emoji, 1},
        {"🙏", "[感谢]",   "ganxie|xiexie|baituo",   "gx|xx|bt|please|thanks", SymbolLibrary::Emoji, 1},
        {"🤝", "[合作]",   "hezuo|woshou",           "hz|ws|handshake",        SymbolLibrary::Emoji, 1},
        {"💪", "[加油]",   "jiayou",                 "jy|nuli|nl|strong",      SymbolLibrary::Emoji, 1},
        {"🎉", "[撒花]",   "sahua|gongxi",           "sh|gx|qingzhu|tada",     SymbolLibrary::Emoji, 1},
        {"❤️", "[爱心]",   "aixin|hongxin",          "ax|ai|heart|love",       SymbolLibrary::Emoji, 1},
        {"🔥", "[火热]",   "huore|baokuan",          "huo|re|remen|rm|fire",   SymbolLibrary::Emoji, 1},
        {"✨", "[闪亮]",   "shanliang",              "sl|xingxing|sparkle",    SymbolLibrary::Emoji, 1},
        {"☀️", "[太阳]",   "taiyang|qingtian",       "ty|qing|sun",            SymbolLibrary::Emoji, 1},
        {"🌙", "[月亮]",   "yueliang|wanan",         "yl|wa|moon",             SymbolLibrary::Emoji, 1},
        {"⭐", "[星星]",   "xingxing",               "xx|shoucang|sc|star",    SymbolLibrary::Emoji, 1},
        {"✅", "[完成]",   "wancheng|duigou",        "wc|dg|check|yes",        SymbolLibrary::Emoji, 1},
        {"❌", "[错误]",   "cuowu",                  "cw|chacha|cc|no|cross",  SymbolLibrary::Emoji, 1},
        {"⚠️", "[警告]",   "jinggao",                "jg|zhuyi|zy|warn",       SymbolLibrary::Emoji, 1},
        {"💡", "[灵感]",   "linggan",                "lg|dianzi|idea",         SymbolLibrary::Emoji, 1},
        {"🚀", "[起飞]",   "qifei|huojian",          "qf|hj|rocket",           SymbolLibrary::Emoji, 1},
        {"☕", "[咖啡]",   "kafei",                  "kf|moyu|my|coffee",      SymbolLibrary::Emoji, 1},
        {"🍵", "[喝茶]",   "hecha",                  "hc|cha|moyu|tea",        SymbolLibrary::Emoji, 1},
        {"🍉", "[吃瓜]",   "chigua|xigua",           "cg|xg|melon",            SymbolLibrary::Emoji, 1},
        {"🐶", "[狗头]",   "goutou",                 "gt|gou|doge|dog",        SymbolLibrary::Emoji, 1},
        {"🐱", "[猫猫]",   "maomao",                 "mm|mao|miao|cat",        SymbolLibrary::Emoji, 1},
        {"🌹", "[玫瑰]",   "meigui",                 "mg|hua|huaduo|rose",     SymbolLibrary::Emoji, 1},

        // ==================== 3. 希腊字母库 (Greek) ====================
        {"α", "[alpha / 阿尔法]",     "aerfa",                 "aef|alpha",          SymbolLibrary::Greek, 4},
        {"β", "[beta / 贝塔]",        "beita",                 "bt|beta",            SymbolLibrary::Greek, 4},
        {"γ", "[gamma / 伽马]",       "gama|jiama",            "gm|gamma",           SymbolLibrary::Greek, 4},
        {"δ", "[delta / 德尔塔]",     "deerta|daerta",         "det|delta",          SymbolLibrary::Greek, 4},
        {"ε", "[epsilon / 伊普西隆]", "yipuxilong|aipuxilong", "ypxl|epsilon",       SymbolLibrary::Greek, 4},
        {"ζ", "[zeta / 泽塔]",        "zeita|jita",            "zt|zeta",            SymbolLibrary::Greek, 4},
        {"η", "[eta / 伊塔]",         "yita|aita",             "yt|eta",             SymbolLibrary::Greek, 4},
        {"θ", "[theta / 西塔]",       "xita|sita",             "xt|theta",           SymbolLibrary::Greek, 4},
        {"ι", "[iota / 约塔]",        "yaota|yueta",           "iota",               SymbolLibrary::Greek, 4},
        {"κ", "[kappa / 卡帕]",       "kapa",                  "kp|kappa",           SymbolLibrary::Greek, 4},
        {"λ", "[lambda / 拉姆达]",    "lamuda|namuda",         "lmd|lambda",         SymbolLibrary::Greek, 4},
        {"μ", "[mu / 缪]",            "miu|miou",              "mu",                 SymbolLibrary::Greek, 4},
        {"ν", "[nu / 纽]",            "nyu",                   "niu|nu",             SymbolLibrary::Greek, 4},
        {"ξ", "[xi / 克西]",          "kesai|kexi",            "ks|xi",              SymbolLibrary::Greek, 4},
        {"ο", "[omicron / 奥密克戎]", "aomikerong",            "amkr|omicron",       SymbolLibrary::Greek, 4},
        {"π", "[pi / 派]",            "pai|yuanzhoulv",        "pi",                 SymbolLibrary::Greek, 4},
        {"ρ", "[rho / 柔]",           "rou|ruo",               "rho",                SymbolLibrary::Greek, 4},
        {"σ", "[sigma / 西格玛]",     "xigema|sigema",         "xgm|sigma",          SymbolLibrary::Greek, 4},
        {"τ", "[tau / 陶]",           "tao",                   "tau",                SymbolLibrary::Greek, 4},
        {"υ", "[upsilon / 宇普西隆]", "yupuxilong",            "ypxl|upsilon",       SymbolLibrary::Greek, 4},
        {"φ", "[phi / 斐]",           "fai|fei|fuai",          "phi",                SymbolLibrary::Greek, 4},
        {"χ", "[chi / 凯]",           "",                      "kai|xi|chi",         SymbolLibrary::Greek, -1},
        {"ψ", "[psi / 普赛]",         "pusai",                 "ps|psi",             SymbolLibrary::Greek, 4},
        {"ω", "[omega / 欧米伽]",     "oumiga|oumeiga|omiga",  "omg|omega",          SymbolLibrary::Greek, 4},
        // 大写希腊字母（词库模式检索）
        {"Α", "[大写 Alpha]",         "", "alpha|aerfa|daxie",          SymbolLibrary::Greek, -1},
        {"Β", "[大写 Beta]",          "", "beta|beita|daxie",           SymbolLibrary::Greek, -1},
        {"Γ", "[大写 Gamma]",         "", "gamma|gama|daxie",           SymbolLibrary::Greek, -1},
        {"Δ", "[大写 Delta]",         "", "delta|deerta|zengliang|daxie", SymbolLibrary::Greek, -1},
        {"Ε", "[大写 Epsilon]",       "", "epsilon|yipuxilong|daxie",   SymbolLibrary::Greek, -1},
        {"Ζ", "[大写 Zeta]",          "", "zeta|zeita|daxie",           SymbolLibrary::Greek, -1},
        {"Η", "[大写 Eta]",           "", "eta|yita|daxie",             SymbolLibrary::Greek, -1},
        {"Θ", "[大写 Theta]",         "", "theta|xita|daxie",           SymbolLibrary::Greek, -1},
        {"Ι", "[大写 Iota]",          "", "iota|yaota|daxie",           SymbolLibrary::Greek, -1},
        {"Κ", "[大写 Kappa]",         "", "kappa|kapa|daxie",           SymbolLibrary::Greek, -1},
        {"Λ", "[大写 Lambda]",        "", "lambda|lamuda|daxie",        SymbolLibrary::Greek, -1},
        {"Μ", "[大写 Mu]",            "", "mu|miu|daxie",               SymbolLibrary::Greek, -1},
        {"Ν", "[大写 Nu]",            "", "nu|niu|daxie",               SymbolLibrary::Greek, -1},
        {"Ξ", "[大写 Xi]",            "", "xi|kesai|daxie",             SymbolLibrary::Greek, -1},
        {"Ο", "[大写 Omicron]",       "", "omicron|aomikerong|daxie",   SymbolLibrary::Greek, -1},
        {"Π", "[大写 Pi]",            "", "pi|pai|liancheng|daxie",     SymbolLibrary::Greek, -1},
        {"Ρ", "[大写 Rho]",           "", "rho|rou|daxie",              SymbolLibrary::Greek, -1},
        {"Σ", "[大写 Sigma]",         "", "sigma|xigema|qiuhe|daxie",   SymbolLibrary::Greek, -1},
        {"Τ", "[大写 Tau]",           "", "tau|tao|daxie",              SymbolLibrary::Greek, -1},
        {"Υ", "[大写 Upsilon]",       "", "upsilon|yupuxilong|daxie",   SymbolLibrary::Greek, -1},
        {"Φ", "[大写 Phi]",           "", "phi|fai|daxie",              SymbolLibrary::Greek, -1},
        {"Χ", "[大写 Chi]",           "", "chi|kai|daxie",              SymbolLibrary::Greek, -1},
        {"Ψ", "[大写 Psi]",           "", "psi|pusai|daxie",            SymbolLibrary::Greek, -1},
        {"Ω", "[大写 Omega]",         "", "omega|oumiga|oumu|daxie",    SymbolLibrary::Greek, -1},

        // ==================== 4. 符号大全库 (Symbol) ====================
        {"⓪", "[序号0]",  "ling",        "xuhao|shuzi",   SymbolLibrary::Symbol, 4},
        {"①", "[序号1]",  "yi|diyi",     "xuhao|shuzi",   SymbolLibrary::Symbol, 4},
        {"②", "[序号2]",  "er|dier",     "xuhao|shuzi",   SymbolLibrary::Symbol, 4},
        {"③", "[序号3]",  "san|disan",   "xuhao|shuzi",   SymbolLibrary::Symbol, 4},
        {"④", "[序号4]",  "si|disi",     "xuhao|shuzi",   SymbolLibrary::Symbol, 4},
        {"⑤", "[序号5]",  "wu|diwu",     "xuhao|shuzi",   SymbolLibrary::Symbol, 4},
        {"⑥", "[序号6]",  "liu|diliu",   "xuhao|shuzi",   SymbolLibrary::Symbol, 4},
        {"⑦", "[序号7]",  "qi|diqi",     "xuhao|shuzi",   SymbolLibrary::Symbol, 4},
        {"⑧", "[序号8]",  "ba|diba",     "xuhao|shuzi",   SymbolLibrary::Symbol, 4},
        {"⑨", "[序号9]",  "jiu|dijiu",   "xuhao|shuzi",   SymbolLibrary::Symbol, 4},
        {"⑩", "[序号10]", "shi|dishi",   "xuhao|shuzi",   SymbolLibrary::Symbol, 4},
        {"⑪", "[序号11]", "shiyi",       "xuhao",         SymbolLibrary::Symbol, 4},
        {"⑫", "[序号12]", "shier",       "xuhao",         SymbolLibrary::Symbol, 4},
        {"⑬", "[序号13]", "shisan",      "xuhao",         SymbolLibrary::Symbol, 4},
        {"⑭", "[序号14]", "shisi",       "xuhao",         SymbolLibrary::Symbol, 4},
        {"⑮", "[序号15]", "shiwu",       "xuhao",         SymbolLibrary::Symbol, 4},
        {"⑯", "[序号16]", "shiliu",      "xuhao",         SymbolLibrary::Symbol, 4},
        {"⑰", "[序号17]", "shiqi",       "xuhao",         SymbolLibrary::Symbol, 4},
        {"⑱", "[序号18]", "shiba",       "xuhao",         SymbolLibrary::Symbol, 4},
        {"⑲", "[序号19]", "shijiu",      "xuhao",         SymbolLibrary::Symbol, 4},
        {"⑳", "[序号20]", "ershi",       "xuhao",         SymbolLibrary::Symbol, 4},
        {"⑴", "[括号1]",  "", "yi|xuhao|kuohao",  SymbolLibrary::Symbol, -1},
        {"⑵", "[括号2]",  "", "er|xuhao|kuohao",  SymbolLibrary::Symbol, -1},
        {"⑶", "[括号3]",  "", "san|xuhao|kuohao", SymbolLibrary::Symbol, -1},
        {"⑷", "[括号4]",  "", "si|xuhao|kuohao",  SymbolLibrary::Symbol, -1},
        {"⑸", "[括号5]",  "", "wu|xuhao|kuohao",  SymbolLibrary::Symbol, -1},
        {"Ⅰ", "[罗马I]",    "", "luoma|yi",   SymbolLibrary::Symbol, -1},
        {"Ⅱ", "[罗马II]",   "", "luoma|er",   SymbolLibrary::Symbol, -1},
        {"Ⅲ", "[罗马III]",  "", "luoma|san",  SymbolLibrary::Symbol, -1},
        {"Ⅳ", "[罗马IV]",   "", "luoma|si",   SymbolLibrary::Symbol, -1},
        {"Ⅴ", "[罗马V]",    "", "luoma|wu",   SymbolLibrary::Symbol, -1},
        {"Ⅵ", "[罗马VI]",   "", "luoma|liu",  SymbolLibrary::Symbol, -1},
        {"Ⅶ", "[罗马VII]",  "", "luoma|qi",   SymbolLibrary::Symbol, -1},
        {"Ⅷ", "[罗马VIII]", "", "luoma|ba",   SymbolLibrary::Symbol, -1},
        {"Ⅸ", "[罗马IX]",   "", "luoma|jiu",  SymbolLibrary::Symbol, -1},
        {"Ⅹ", "[罗马X]",    "", "luoma|shi",  SymbolLibrary::Symbol, -1},

        // 标点名称联想 (第 5 位)
        {"、",   "[顿号]",   "dunhao",               "dh|biaodian",      SymbolLibrary::Symbol, 4},
        {"。",   "[句号]",   "juhao",                "jh|biaodian",      SymbolLibrary::Symbol, 4},
        {"，",   "[逗号]",   "douhao",               "biaodian",         SymbolLibrary::Symbol, 4},
        {"？",   "[问号]",   "wenhao",               "wh|biaodian",      SymbolLibrary::Symbol, 4},
        {"！",   "[叹号]",   "tanhao|gantanhao",     "gth|biaodian",     SymbolLibrary::Symbol, 4},
        {"：",   "[冒号]",   "maohao",               "mh|biaodian",      SymbolLibrary::Symbol, 4},
        {"；",   "[分号]",   "fenhao",               "fh|biaodian",      SymbolLibrary::Symbol, 4},
        {"《》", "[书名号]", "shuminghao",           "smh|biaodian",     SymbolLibrary::Symbol, 4},
        {"“”",   "[双引号]", "yinhao|shuangyinhao",  "yh|biaodian",      SymbolLibrary::Symbol, 4},
        {"‘’",   "[单引号]", "danyinhao",            "dyh|yinhao",       SymbolLibrary::Symbol, 4},
        {"（）", "[括号]",   "kuohao|xiaokuohao",    "kh|biaodian",      SymbolLibrary::Symbol, 4},
        {"【】", "[方括号]", "zhongkuohao|fangkuohao", "zkh|biaodian",   SymbolLibrary::Symbol, 4},
        {"——",   "[破折号]", "pozhehao",             "pzh|biaodian",     SymbolLibrary::Symbol, 4},
        {"……",   "[省略号]", "shenglvehao",          "slh|biaodian",     SymbolLibrary::Symbol, 4},
        {"·",    "[间隔号]", "jiangehao",            "jgh|zhongdian",    SymbolLibrary::Symbol, 4},
        {"/",    "[斜杠]",   "xiegang",              "xg|biaodian",      SymbolLibrary::Symbol, 4},
        {"\\",   "[反斜杠]", "fanxiegang",           "fxg|biaodian",     SymbolLibrary::Symbol, 4},
        {"|",    "[竖线]",   "shuxian",              "sx|biaodian",      SymbolLibrary::Symbol, 4},
        {"#",    "[井号]",   "jinghao",              "biaodian",         SymbolLibrary::Symbol, 4},
        {"%",    "[百分号]", "baifenhao",            "bfh",              SymbolLibrary::Symbol, 4},
        {"‰",    "[千分号]", "qianfenhao",           "qfh",              SymbolLibrary::Symbol, 4},
        {"¥",    "[人民币]", "renminbi",             "rmb|huobi",        SymbolLibrary::Symbol, 4},
        {"$",    "[美元]",   "meiyuan",              "huobi|dollar",     SymbolLibrary::Symbol, 4},
        {"€",    "[欧元]",   "ouyuan",               "huobi|euro",       SymbolLibrary::Symbol, 4},

        // 数学、单位与箭头符号
        {"→", "[右箭头]",   "youjiantou|jiantou",     "yjt|jt|arrow",   SymbolLibrary::Symbol, 4},
        {"←", "[左箭头]",   "zuojiantou",             "zjt|jt|arrow",   SymbolLibrary::Symbol, 4},
        {"↑", "[上箭头]",   "shangjiantou",           "sjt|jt|arrow",   SymbolLibrary::Symbol, 4},
        {"↓", "[下箭头]",   "xiajiantou",             "xjt|jt|arrow",   SymbolLibrary::Symbol, 4},
        {"↔", "[双向箭头]", "shuangxiangjiantou",     "jiantou|arrow",  SymbolLibrary::Symbol, 4},
        {"⇒", "[推出]",     "",                       "tuichu|jiantou", SymbolLibrary::Symbol, -1},
        {"×", "[乘号]",     "chenghao",               "ch|shuxue",      SymbolLibrary::Symbol, 4},
        {"÷", "[除号]",     "chuhao",                 "shuxue",         SymbolLibrary::Symbol, 4},
        {"±", "[正负号]",   "zhengfuhao",             "zhengfu|zf|shuxue", SymbolLibrary::Symbol, 4},
        {"≠", "[不等于]",   "budengyu",               "bdy|shuxue",     SymbolLibrary::Symbol, 4},
        {"≈", "[约等于]",   "yuedengyu",              "ydy|shuxue",     SymbolLibrary::Symbol, 4},
        {"≤", "[小于等于]", "xiaoyudengyu",           "xydy|shuxue",    SymbolLibrary::Symbol, 4},
        {"≥", "[大于等于]", "dayudengyu",             "dydy|shuxue",    SymbolLibrary::Symbol, 4},
        {"∞", "[无穷大]",   "wuqiongda|wuqiong",      "wq|shuxue",      SymbolLibrary::Symbol, 4},
        {"∵", "[因为]",     "",                       "yinwei|yw|shuxue", SymbolLibrary::Symbol, -1},
        {"∴", "[所以]",     "",                       "suoyi|sy|shuxue", SymbolLibrary::Symbol, -1},
        {"√", "[根号]",     "genhao",                 "gh|kaifang|shuxue", SymbolLibrary::Symbol, 4},
        {"∑", "[求和]",     "qiuhe",                  "qh|shuxue",      SymbolLibrary::Symbol, 4},
        {"∫", "[积分]",     "jifen",                  "jf|shuxue",      SymbolLibrary::Symbol, 4},
        {"°", "[度]",       "jiaodu",                 "du|danwei",      SymbolLibrary::Symbol, 4},
        {"℃", "[摄氏度]",   "sheshidu",               "ssd|wendu",      SymbolLibrary::Symbol, 4},
        {"㎡","[平方米]",   "pingfangmi",             "pfm|danwei",     SymbolLibrary::Symbol, 4},
        {"²", "[平方]",     "pingfang",               "pf|shuxue",      SymbolLibrary::Symbol, 4},
        {"³", "[立方]",     "lifang",                 "lf|shuxue",      SymbolLibrary::Symbol, 4},
        {"✓", "[对号]",     "duihao|duigou|dagou",    "dh|dg",          SymbolLibrary::Symbol, 4},
        {"✕", "[错号]",     "cuohao|chacha",          "ch",             SymbolLibrary::Symbol, 4},
        {"★", "[五角星]",   "wujiaoxing|xinghao",     "wjx|star",       SymbolLibrary::Symbol, 4},
        {"☆", "[空心星]",   "kongxing",               "wujiaoxing|star", SymbolLibrary::Symbol, 4},
        {"♡", "[空心心]",   "",                       "aixin|xinxing|heart", SymbolLibrary::Symbol, -1},
        {"©", "[版权]",     "banquan",                "bq|copyright",   SymbolLibrary::Symbol, 4},
        {"®", "[注册]",     "zhuce",                  "zc",             SymbolLibrary::Symbol, 4},
        {"™", "[商标]",     "shangbiao",              "sb|tm",          SymbolLibrary::Symbol, 4},
        {"♪", "[音符]",     "yinfu|yinle",            "yf|music",       SymbolLibrary::Symbol, 4},
    };
    return kEntries;
}

std::vector<InlineSymbolHit> SymbolCatalog::matchInline(std::string_view concatPinyin) {
    if (concatPinyin.empty()) return {};
    const std::string lower = detail::toLowerAscii(concatPinyin);

    std::vector<InlineSymbolHit> out;
    std::unordered_set<std::string_view> seen;
    for (const auto& entry : allEntries()) {
        if (exactInlineKeywordMatch(entry, lower)) {
            if (seen.insert(entry.text).second) {
                out.push_back(InlineSymbolHit{&entry, entry.inlineSlot});
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const InlineSymbolHit& a, const InlineSymbolHit& b) {
        if (a.preferredSlot != b.preferredSlot) return a.preferredSlot < b.preferredSlot;
        return static_cast<uint8_t>(a.entry->library) < static_cast<uint8_t>(b.entry->library);
    });
    return out;
}

std::vector<const SymbolEntry*> SymbolCatalog::queryLibrary(SymbolLibrary lib,
                                                            std::string_view query,
                                                            bool filterByLibrary) {
    const std::string lower = detail::toLowerAscii(query);
    struct RankedHit {
        const SymbolEntry* entry = nullptr;
        int score = 0;
        size_t order = 0;
    };
    std::vector<RankedHit> ranked;
    const auto& all = allEntries();
    for (size_t i = 0; i < all.size(); ++i) {
        const auto& e = all[i];
        if (filterByLibrary && e.library != lib) continue;
        int sc = scoreKeywordMatch(e, lower);
        if (sc > 0) {
            ranked.push_back(RankedHit{&e, sc, i});
        }
    }
    std::sort(ranked.begin(), ranked.end(), [](const RankedHit& a, const RankedHit& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.order < b.order;
    });
    std::vector<const SymbolEntry*> out;
    out.reserve(ranked.size());
    for (const auto& r : ranked) {
        out.push_back(r.entry);
    }
    return out;
}

std::vector<std::string> SymbolCatalog::matchCustomInline(
    std::string_view concatPinyin,
    const std::vector<CustomKaomojiItem>& customKaomojis) {
    if (concatPinyin.empty() || customKaomojis.empty()) return {};
    const std::string lower = detail::toLowerAscii(concatPinyin);
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    for (const auto& item : customKaomojis) {
        if (item.text.empty() || item.keywords.empty()) continue;
        std::string_view kws(item.keywords);
        size_t pos = 0;
        bool matched = false;
        while (pos < kws.size()) {
            size_t next = kws.find('|', pos);
            if (next == std::string_view::npos) next = kws.size();
            const std::string tok = detail::toLowerAscii(kws.substr(pos, next - pos));
            // Require >= 3 chars for custom inline trigger unless full keyword matches cleanly (avoid short partial hijack)
            if (tok == lower && tok.size() >= 3) {
                matched = true;
                break;
            }
            pos = next + 1;
        }
        if (matched && seen.insert(item.text).second) {
            out.push_back(item.text);
        }
    }
    return out;
}

std::vector<CatalogDisplayEntry> SymbolCatalog::queryCatalog(
    SymbolLibrary lib,
    std::string_view query,
    const std::vector<CustomKaomojiItem>& customKaomojis,
    bool filterByLibrary) {
    const std::string lower = detail::toLowerAscii(query);
    std::vector<CatalogDisplayEntry> out;
    std::unordered_set<std::string> seenText;

    if ((!filterByLibrary || lib == SymbolLibrary::Kaomoji) && !customKaomojis.empty()) {
        struct RankedCustom {
            const CustomKaomojiItem* item = nullptr;
            int score = 0;
            size_t order = 0;
        };
        std::vector<RankedCustom> rankedCustom;
        for (size_t i = 0; i < customKaomojis.size(); ++i) {
            const auto& ck = customKaomojis[i];
            if (ck.text.empty()) continue;
            int sc = 1;
            if (!lower.empty()) {
                sc = std::max(scorePipeList(ck.keywords.c_str(), lower),
                              scorePipeList(ck.label.c_str(), lower));
                if (sc == 0 && !ck.label.empty() &&
                    detail::toLowerAscii(ck.label).find(lower) != std::string::npos) {
                    sc = 50;
                }
            }
            if (sc > 0) {
                rankedCustom.push_back(RankedCustom{&ck, sc, i});
            }
        }
        std::sort(rankedCustom.begin(), rankedCustom.end(), [](const RankedCustom& a, const RankedCustom& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.order < b.order;
        });
        for (const auto& rc : rankedCustom) {
            if (seenText.insert(rc.item->text).second) {
                std::string lbl = rc.item->label.empty()
                    ? "[自定义]"
                    : ("[" + rc.item->label + "]");
                out.push_back(CatalogDisplayEntry{rc.item->text, std::move(lbl), true});
            }
        }
    }

    for (const auto* e : queryLibrary(lib, query, filterByLibrary)) {
        if (seenText.insert(e->text).second) {
            out.push_back(CatalogDisplayEntry{e->text, e->label ? e->label : "", false});
        }
    }
    return out;
}

} // namespace liteime::engine
