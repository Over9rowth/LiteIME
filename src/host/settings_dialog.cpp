#include "host/settings_dialog.hpp"
#include "host/host_messages.hpp"

#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <string>

namespace liteime::host {

namespace {

enum ControlId : int {
    kIdComboScheme = 2001,
    kIdComboPageSize = 2002,
    kIdComboPreedit = 2003,
    kIdComboSkin = 2004,
    kIdComboLayout = 2005,

    kIdChkChineseMode = 2010,
    kIdChkChinesePunct = 2011,
    kIdChkFullWidth = 2012,
    kIdChkMixedEnglish = 2013,
    kIdChkShiftToggle = 2014,
    kIdChkSmartDecimal = 2015,
    kIdChkAutoUrl = 2016,
    kIdChkStatusBar = 2017,
    kIdChkSquareFrame = 2018,
    kIdChkTypo = 2019,
    kIdChkLooseInitial = 2020,

    kIdChkFuzzyZh = 2030,
    kIdChkFuzzyCh = 2031,
    kIdChkFuzzySh = 2032,
    kIdChkFuzzyNL = 2033,
    kIdChkFuzzyRL = 2034,
    kIdChkFuzzyFH = 2035,
    kIdChkFuzzyAnAng = 2036,
    kIdChkFuzzyEnEng = 2037,
    kIdChkFuzzyInIng = 2038,
    kIdChkFuzzyIangIan = 2039,
    kIdChkFuzzyUangUan = 2040,

    kIdChkPackEn = 2050,
    kIdChkPackCs = 2051,
    kIdChkPackExt = 2052,
    kIdChkPackPro = 2053,
    kIdChkPackCustom = 2054,

    kIdBtnQuickInput = 2060,
    kIdBtnCustomDict = 2061,
    kIdBtnOk = 2062,
    kIdBtnApply = 2063,
    kIdBtnCancel = 2064,
};

struct SettingsState {
    EngineConfig cfg;
    std::function<void(const EngineConfig&)> onApply;
    std::function<void(SettingsQuickCommand)> onCommand;
    HWND hwnd = nullptr;
    HFONT font = nullptr;
    bool appliedAny = false;
    bool closed = false;
};

void setCheck(HWND hwnd, int id, bool checked) {
    SendMessageW(GetDlgItem(hwnd, id), BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}

bool getCheck(HWND hwnd, int id) {
    return SendMessageW(GetDlgItem(hwnd, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void populateFromConfig(HWND hwnd, const EngineConfig& cfg) {
    SendMessageW(GetDlgItem(hwnd, kIdComboScheme), CB_SETCURSEL,
                 static_cast<WPARAM>(std::clamp(static_cast<int>(cfg.scheme), 0, 4)), 0);
    SendMessageW(GetDlgItem(hwnd, kIdComboPageSize), CB_SETCURSEL,
                 static_cast<WPARAM>(std::clamp(cfg.pageSize - 5, 0, 3)), 0);
    SendMessageW(GetDlgItem(hwnd, kIdComboPreedit), CB_SETCURSEL,
                 static_cast<WPARAM>(std::clamp(static_cast<int>(cfg.inlinePreedit), 0, 2)), 0);
    SendMessageW(GetDlgItem(hwnd, kIdComboSkin), CB_SETCURSEL,
                 static_cast<WPARAM>(std::clamp(static_cast<int>(cfg.skin), 0, 3)), 0);
    SendMessageW(GetDlgItem(hwnd, kIdComboLayout), CB_SETCURSEL,
                 static_cast<WPARAM>(cfg.layout == CandidateLayout::Vertical ? 1 : 0), 0);

    setCheck(hwnd, kIdChkChineseMode, cfg.chineseMode);
    setCheck(hwnd, kIdChkChinesePunct, cfg.chinesePunct);
    setCheck(hwnd, kIdChkFullWidth, cfg.fullWidth);
    setCheck(hwnd, kIdChkMixedEnglish, cfg.mixedEnglish);
    setCheck(hwnd, kIdChkShiftToggle, cfg.shiftTapToggle);
    setCheck(hwnd, kIdChkSmartDecimal, cfg.smartDecimal);
    setCheck(hwnd, kIdChkAutoUrl, cfg.autoUrlEnglish);
    setCheck(hwnd, kIdChkStatusBar, cfg.showStatusBar);
    setCheck(hwnd, kIdChkSquareFrame, cfg.squareFrame);
    setCheck(hwnd, kIdChkTypo, cfg.typoCorrection);
    setCheck(hwnd, kIdChkLooseInitial, cfg.looseInitialMatch);

    setCheck(hwnd, kIdChkFuzzyZh, cfg.fuzzy.zh_z);
    setCheck(hwnd, kIdChkFuzzyCh, cfg.fuzzy.ch_c);
    setCheck(hwnd, kIdChkFuzzySh, cfg.fuzzy.sh_s);
    setCheck(hwnd, kIdChkFuzzyNL, cfg.fuzzy.n_l);
    setCheck(hwnd, kIdChkFuzzyRL, cfg.fuzzy.r_l);
    setCheck(hwnd, kIdChkFuzzyFH, cfg.fuzzy.f_h);
    setCheck(hwnd, kIdChkFuzzyAnAng, cfg.fuzzy.an_ang);
    setCheck(hwnd, kIdChkFuzzyEnEng, cfg.fuzzy.en_eng);
    setCheck(hwnd, kIdChkFuzzyInIng, cfg.fuzzy.in_ing);
    setCheck(hwnd, kIdChkFuzzyIangIan, cfg.fuzzy.iang_ian);
    setCheck(hwnd, kIdChkFuzzyUangUan, cfg.fuzzy.uang_uan);

    setCheck(hwnd, kIdChkPackEn, cfg.enableEnPack);
    setCheck(hwnd, kIdChkPackCs, cfg.enableCsPack);
    setCheck(hwnd, kIdChkPackExt, cfg.enableExtPack);
    setCheck(hwnd, kIdChkPackPro, cfg.enableProPack);
    setCheck(hwnd, kIdChkPackCustom, cfg.enableCustomDicts);
}

void readIntoConfig(HWND hwnd, EngineConfig& cfg) {
    int schemeIdx = static_cast<int>(SendMessageW(GetDlgItem(hwnd, kIdComboScheme), CB_GETCURSEL, 0, 0));
    int pageIdx = static_cast<int>(SendMessageW(GetDlgItem(hwnd, kIdComboPageSize), CB_GETCURSEL, 0, 0));
    int preIdx = static_cast<int>(SendMessageW(GetDlgItem(hwnd, kIdComboPreedit), CB_GETCURSEL, 0, 0));
    int skinIdx = static_cast<int>(SendMessageW(GetDlgItem(hwnd, kIdComboSkin), CB_GETCURSEL, 0, 0));
    int layoutIdx = static_cast<int>(SendMessageW(GetDlgItem(hwnd, kIdComboLayout), CB_GETCURSEL, 0, 0));

    if (schemeIdx >= 0 && schemeIdx <= 4) cfg.scheme = static_cast<PinyinScheme>(schemeIdx);
    if (pageIdx >= 0 && pageIdx <= 3) cfg.pageSize = 5 + pageIdx;
    if (preIdx >= 0 && preIdx <= 2) cfg.inlinePreedit = static_cast<InlinePreeditMode>(preIdx);
    if (skinIdx >= 0 && skinIdx <= 3) cfg.skin = static_cast<SkinTheme>(skinIdx);
    if (layoutIdx >= 0 && layoutIdx <= 1) {
        cfg.layout = (layoutIdx == 1) ? CandidateLayout::Vertical : CandidateLayout::Horizontal;
    }

    cfg.chineseMode = getCheck(hwnd, kIdChkChineseMode);
    cfg.chinesePunct = getCheck(hwnd, kIdChkChinesePunct);
    cfg.fullWidth = getCheck(hwnd, kIdChkFullWidth);
    cfg.mixedEnglish = getCheck(hwnd, kIdChkMixedEnglish);
    cfg.shiftTapToggle = getCheck(hwnd, kIdChkShiftToggle);
    cfg.smartDecimal = getCheck(hwnd, kIdChkSmartDecimal);
    cfg.autoUrlEnglish = getCheck(hwnd, kIdChkAutoUrl);
    cfg.showStatusBar = getCheck(hwnd, kIdChkStatusBar);
    cfg.squareFrame = getCheck(hwnd, kIdChkSquareFrame);
    cfg.typoCorrection = getCheck(hwnd, kIdChkTypo);
    cfg.looseInitialMatch = getCheck(hwnd, kIdChkLooseInitial);

    cfg.fuzzy.zh_z = getCheck(hwnd, kIdChkFuzzyZh);
    cfg.fuzzy.ch_c = getCheck(hwnd, kIdChkFuzzyCh);
    cfg.fuzzy.sh_s = getCheck(hwnd, kIdChkFuzzySh);
    cfg.fuzzy.n_l = getCheck(hwnd, kIdChkFuzzyNL);
    cfg.fuzzy.r_l = getCheck(hwnd, kIdChkFuzzyRL);
    cfg.fuzzy.f_h = getCheck(hwnd, kIdChkFuzzyFH);
    cfg.fuzzy.an_ang = getCheck(hwnd, kIdChkFuzzyAnAng);
    cfg.fuzzy.en_eng = getCheck(hwnd, kIdChkFuzzyEnEng);
    cfg.fuzzy.in_ing = getCheck(hwnd, kIdChkFuzzyInIng);
    cfg.fuzzy.iang_ian = getCheck(hwnd, kIdChkFuzzyIangIan);
    cfg.fuzzy.uang_uan = getCheck(hwnd, kIdChkFuzzyUangUan);

    cfg.enableEnPack = getCheck(hwnd, kIdChkPackEn);
    cfg.enableCsPack = getCheck(hwnd, kIdChkPackCs);
    cfg.enableExtPack = getCheck(hwnd, kIdChkPackExt);
    cfg.enableProPack = getCheck(hwnd, kIdChkPackPro);
    cfg.enableCustomDicts = getCheck(hwnd, kIdChkPackCustom);
}

LRESULT CALLBACK settingsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<SettingsState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            st = reinterpret_cast<SettingsState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
            if (st) st->hwnd = hwnd;
            return TRUE;
        }
        case WM_CREATE: {
            if (!st) return -1;
            const UINT dpi = queryDpiForWindow(hwnd);
            auto sc = [dpi](int px) { return MulDiv(px, static_cast<int>(dpi), 96); };
            HINSTANCE hInst = GetModuleHandleW(nullptr);
            st->font = CreateFontW(-sc(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
            auto setFont = [st](HWND c) {
                if (c && st->font) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);
            };
            auto addGroup = [&](const wchar_t* title, int x, int y, int w, int h) {
                HWND c = CreateWindowExW(0, L"BUTTON", title, WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                         sc(x), sc(y), sc(w), sc(h), hwnd, nullptr, hInst, nullptr);
                setFont(c);
            };
            auto addLabel = [&](const wchar_t* txt, int x, int y, int w, int h) {
                HWND c = CreateWindowExW(0, L"STATIC", txt, WS_CHILD | WS_VISIBLE,
                                         sc(x), sc(y), sc(w), sc(h), hwnd, nullptr, hInst, nullptr);
                setFont(c);
            };
            auto addCombo = [&](int id, int x, int y, int w, int dropH) {
                HWND c = CreateWindowExW(0, L"COMBOBOX", nullptr,
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                         sc(x), sc(y), sc(w), sc(dropH), hwnd,
                                         reinterpret_cast<HMENU>(static_cast<intptr_t>(id)), hInst, nullptr);
                setFont(c);
                return c;
            };
            auto addCheck = [&](int id, const wchar_t* txt, int x, int y, int w) {
                HWND c = CreateWindowExW(0, L"BUTTON", txt,
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                         sc(x), sc(y), sc(w), sc(22), hwnd,
                                         reinterpret_cast<HMENU>(static_cast<intptr_t>(id)), hInst, nullptr);
                setFont(c);
                return c;
            };
            auto addBtn = [&](int id, const wchar_t* txt, int x, int y, int w, int h, bool defBtn = false) {
                DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | (defBtn ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON);
                HWND c = CreateWindowExW(0, L"BUTTON", txt, style,
                                         sc(x), sc(y), sc(w), sc(h), hwnd,
                                         reinterpret_cast<HMENU>(static_cast<intptr_t>(id)), hInst, nullptr);
                setFont(c);
                return c;
            };

            // Group 1: Input Scheme & Habit (top-left)
            addGroup(L"\u8f93\u5165\u65b9\u6848\u4e0e\u4e60\u60ef", 14, 12, 310, 250);
            addLabel(L"\u62fc\u97f3\u65b9\u6848:", 28, 40, 76, 20);
            HWND cbScheme = addCombo(kIdComboScheme, 110, 36, 198, 160);
            SendMessageW(cbScheme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u5168\u62fc (\u9ed8\u8ba4)"));
            SendMessageW(cbScheme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u5c0f\u9e64\u53cc\u62fc"));
            SendMessageW(cbScheme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u5fae\u8f6f\u53cc\u62fc"));
            SendMessageW(cbScheme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u81ea\u7136\u7801\u53cc\u62fc"));
            SendMessageW(cbScheme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u641c\u72d7\u53cc\u62fc"));

            addLabel(L"\u5019\u9009\u4e2a\u6570:", 28, 72, 76, 20);
            HWND cbPage = addCombo(kIdComboPageSize, 110, 68, 198, 130);
            SendMessageW(cbPage, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"5 \u4e2a\u5019\u9009\u8bcd (\u9ed8\u8ba4)"));
            SendMessageW(cbPage, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"6 \u4e2a\u5019\u9009\u8bcd"));
            SendMessageW(cbPage, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"7 \u4e2a\u5019\u9009\u8bcd"));
            SendMessageW(cbPage, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"8 \u4e2a\u5019\u9009\u8bcd"));

            addLabel(L"\u5d4c\u5165\u9884\u89c8:", 28, 104, 76, 20);
            HWND cbPre = addCombo(kIdComboPreedit, 110, 100, 198, 130);
            SendMessageW(cbPre, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u9996\u9009\u6c49\u5b57\u9884\u89c8"));
            SendMessageW(cbPre, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u5df2\u786e\u8ba4\u5b57+\u62fc\u97f3"));
            SendMessageW(cbPre, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u4ec5\u60ac\u6d6e\u5019\u9009\u6846"));

            addCheck(kIdChkChineseMode, L"\u4e2d\u6587\u6a21\u5f0f", 28, 136, 130);
            addCheck(kIdChkChinesePunct, L"\u4e2d\u6587\u6807\u70b9", 170, 136, 130);
            addCheck(kIdChkFullWidth, L"\u5168\u89d2\u5b57\u7b26", 28, 162, 130);
            addCheck(kIdChkMixedEnglish, L"\u4e2d\u82f1\u6df7\u8f93", 170, 162, 130);
            addCheck(kIdChkShiftToggle, L"Shift \u5355\u51fb\u5207\u6362", 28, 188, 135);
            addCheck(kIdChkSmartDecimal, L"\u6570\u5b57\u540e\u5c0f\u6570\u70b9 (3.14)", 170, 188, 148);
            addCheck(kIdChkAutoUrl, L"\u7f51\u5740/\u90ae\u7bb1\u81ea\u52a8\u82f1\u6587 (www. / @)", 28, 214, 280);

            // Group 2: Appearance (top-right)
            addGroup(L"\u5916\u89c2\u4e0e\u76ae\u80a4", 336, 12, 260, 152);
            addLabel(L"\u4e3b\u9898\u76ae\u80a4:", 350, 40, 72, 20);
            HWND cbSkin = addCombo(kIdComboSkin, 424, 36, 156, 150);
            SendMessageW(cbSkin, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u4e91\u96fe\u7409\u7483 (\u9ed8\u8ba4)"));
            SendMessageW(cbSkin, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u82cd\u7fe0\u7af9\u9752"));
            SendMessageW(cbSkin, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u6a31\u7c89\u67d4\u96fe"));
            SendMessageW(cbSkin, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u661f\u66dc\u7384\u591c"));

            addLabel(L"\u5019\u9009\u6392\u5217:", 350, 72, 72, 20);
            HWND cbLayout = addCombo(kIdComboLayout, 424, 68, 156, 100);
            SendMessageW(cbLayout, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u6a2a\u6392 (\u9ed8\u8ba4)"));
            SendMessageW(cbLayout, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u7ad6\u6392\u5217\u8868"));

            addCheck(kIdChkStatusBar, L"\u663e\u793a\u60ac\u6d6e\u72b6\u6001\u680f", 350, 104, 220);
            addCheck(kIdChkSquareFrame, L"\u65b9\u6846\u6837\u5f0f (\u5173\u95ed\u4e3a\u80f6\u56ca)", 350, 130, 225);

            // Group 3: Dict Packs (middle-right)
            addGroup(L"\u6269\u5c55\u8bcd\u5e93\u5305", 336, 172, 260, 154);
            addCheck(kIdChkPackEn, L"\u5e38\u89c1\u82f1\u6587\u6269\u5c55 (common_en)", 350, 196, 232);
            addCheck(kIdChkPackCs, L"\u8ba1\u7b97\u673a\u8bcd\u5e93 (computer)", 350, 220, 232);
            addCheck(kIdChkPackExt, L"\u6269\u5c55\u5927\u8bcd\u5e93 (ext_cn)", 350, 244, 232);
            addCheck(kIdChkPackPro, L"\u533b\u5b66/\u6cd5\u5f8b/\u8d22\u7ecf (pro_fields)", 350, 268, 232);
            addCheck(kIdChkPackCustom, L"\u52a0\u8f7d\u81ea\u5b9a\u4e49\u8bcd\u5e93\u76ee\u5f55", 350, 292, 232);

            // Group 4: Typo & Fuzzy Pinyin (bottom)
            addGroup(L"\u7ea0\u9519\u4e0e\u6a21\u7cca\u97f3", 14, 270, 310, 172);
            addCheck(kIdChkTypo, L"\u667a\u80fd\u62fc\u5199\u7ea0\u9519 (zhogn->zhong)", 28, 294, 270);
            addCheck(kIdChkLooseInitial, L"\u5bbd\u677e\u9996\u5b57\u6bcd\u5339\u914d", 28, 318, 200);
            addCheck(kIdChkFuzzyZh, L"zh \u2194 z", 28, 344, 88);
            addCheck(kIdChkFuzzyCh, L"ch \u2194 c", 124, 344, 88);
            addCheck(kIdChkFuzzySh, L"sh \u2194 s", 220, 344, 88);
            addCheck(kIdChkFuzzyNL, L"n \u2194 l", 28, 368, 88);
            addCheck(kIdChkFuzzyRL, L"r \u2194 l", 124, 368, 88);
            addCheck(kIdChkFuzzyFH, L"f \u2194 h", 220, 368, 88);
            addCheck(kIdChkFuzzyAnAng, L"an \u2194 ang", 28, 392, 92);
            addCheck(kIdChkFuzzyEnEng, L"en \u2194 eng", 124, 392, 92);
            addCheck(kIdChkFuzzyInIng, L"in \u2194 ing", 220, 392, 92);
            addCheck(kIdChkFuzzyIangIan, L"iang \u2194 ian", 28, 416, 108);
            addCheck(kIdChkFuzzyUangUan, L"uang \u2194 uan", 146, 416, 118);

            // Quick utility links (bottom-right)
            addGroup(L"\u5feb\u6377\u7ba1\u7406", 336, 334, 260, 108);
            addBtn(kIdBtnQuickInput, L"\u7ba1\u7406\u5feb\u8f93\u5165 (vvv)...", 352, 360, 228, 30);
            addBtn(kIdBtnCustomDict, L"\u6253\u5f00\u81ea\u5b9a\u4e49\u8bcd\u5e93\u76ee\u5f55", 352, 398, 228, 30);

            // Dialog confirmation buttons
            addBtn(kIdBtnOk, L"\u786e\u5b9a", 316, 456, 86, 30, true);
            addBtn(kIdBtnCancel, L"\u53d6\u6d88", 412, 456, 86, 30);
            addBtn(kIdBtnApply, L"\u5e94\u7528", 508, 456, 86, 30);

            populateFromConfig(hwnd, st->cfg);
            return 0;
        }
        case WM_COMMAND: {
            if (!st) break;
            const int id = LOWORD(wParam);
            switch (id) {
                case kIdBtnQuickInput:
                    if (st->onCommand) st->onCommand(SettingsQuickCommand::OpenQuickInput);
                    return 0;
                case kIdBtnCustomDict:
                    if (st->onCommand) st->onCommand(SettingsQuickCommand::OpenCustomDictFolder);
                    return 0;
                case kIdBtnApply:
                    readIntoConfig(hwnd, st->cfg);
                    st->appliedAny = true;
                    if (st->onApply) st->onApply(st->cfg);
                    return 0;
                case kIdBtnOk:
                    readIntoConfig(hwnd, st->cfg);
                    st->appliedAny = true;
                    if (st->onApply) st->onApply(st->cfg);
                    st->closed = true;
                    DestroyWindow(hwnd);
                    return 0;
                case kIdBtnCancel:
                    st->closed = true;
                    DestroyWindow(hwnd);
                    return 0;
                default:
                    break;
            }
            break;
        }
        case WM_CLOSE:
            if (st) st->closed = true;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (st && st->font) {
                DeleteObject(st->font);
                st->font = nullptr;
            }
            if (st) st->closed = true;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

bool showSettingsCenterDialog(
    HWND parent,
    EngineConfig& inOutCfg,
    const std::function<void(const EngineConfig&)>& onApply,
    const std::function<void(SettingsQuickCommand)>& onCommand,
    HWND* outActiveHwnd) {
    static bool sRegistered = false;
    static const wchar_t* kCls = L"LiteIME_SettingsCenter";
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    if (!sRegistered) {
        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
        InitCommonControlsEx(&icc);
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = settingsDlgProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = kCls;
        if (RegisterClassExW(&wc)) {
            sRegistered = true;
        }
    }

    SettingsState state;
    state.cfg = inOutCfg;
    state.onApply = onApply;
    state.onCommand = onCommand;

    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const UINT sysDpi = queryDpiForWindow(parent);
    constexpr DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    RECT clientRc{0, 0, MulDiv(610, static_cast<int>(sysDpi), 96), MulDiv(500, static_cast<int>(sysDpi), 96)};
    AdjustWindowRectEx(&clientRc, kStyle, FALSE, WS_EX_DLGMODALFRAME);
    const int winW = clientRc.right - clientRc.left;
    const int winH = clientRc.bottom - clientRc.top;
    const int x = wa.left + std::max(0, (static_cast<int>(wa.right - wa.left) - winW) / 2);
    const int y = wa.top + std::max(0, (static_cast<int>(wa.bottom - wa.top) - winH) / 2);

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kCls,
        L"LiteIME \u8bbe\u7f6e\u4e2d\u5fc3",
        kStyle,
        x, y, winW, winH,
        parent, nullptr, hInst, &state);
    if (!dlg) return false;
    if (outActiveHwnd) *outActiveHwnd = dlg;

    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);

    MSG msg{};
    while (!state.closed && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            PostMessageW(dlg, WM_CLOSE, 0, 0);
            continue;
        }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (outActiveHwnd && *outActiveHwnd == dlg) {
        *outActiveHwnd = nullptr;
    }

    if (state.appliedAny) {
        inOutCfg = state.cfg;
        return true;
    }
    return false;
}

} // namespace liteime::host
