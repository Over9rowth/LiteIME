#include "host/custom_kaomoji_dialog.hpp"
#include "host/host_messages.hpp"
#include "common/utf.hpp"

#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace liteime::host {

namespace {

constexpr int kIdList        = 2001;
constexpr int kIdEditText    = 2002;
constexpr int kIdEditLabel   = 2003;
constexpr int kIdEditKeys    = 2004;
constexpr int kIdBtnAdd      = 2005;
constexpr int kIdBtnDel      = 2006;
constexpr int kIdBtnUp       = 2007;
constexpr int kIdBtnOpenFile = 2008;
constexpr int kIdBtnSave     = 2009;
constexpr int kIdBtnCancel   = 2010;

std::wstring getCtrlText(HWND hwnd, int id) {
    HWND ctrl = GetDlgItem(hwnd, id);
    if (!ctrl) return {};
    int len = GetWindowTextLengthW(ctrl);
    if (len <= 0) return {};
    std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(ctrl, buf.data(), len + 1);
    buf.resize(static_cast<size_t>(len));
    return buf;
}

struct KaomojiDlgState {
    std::vector<engine::CustomKaomojiItem> items;
    std::filesystem::path filePath;
    HWND hwnd = nullptr;
    HFONT font = nullptr;
    bool accepted = false;
    bool closed = false;

    void refreshList(int selectIdx = -1) {
        HWND lb = GetDlgItem(hwnd, kIdList);
        if (!lb) return;
        SendMessageW(lb, LB_RESETCONTENT, 0, 0);
        for (const auto& k : items) {
            std::string line = (k.label.empty() ? "[自定义]" : "[" + k.label + "]") +
                               "  " + k.text;
            if (!k.keywords.empty()) {
                line += "   (" + k.keywords + ")";
            }
            std::wstring wline = utf8_to_utf16(line);
            SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wline.c_str()));
        }
        if (selectIdx >= 0 && selectIdx < static_cast<int>(items.size())) {
            SendMessageW(lb, LB_SETCURSEL, static_cast<WPARAM>(selectIdx), 0);
        }
    }
};

LRESULT CALLBACK customKaomojiDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<KaomojiDlgState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            st = reinterpret_cast<KaomojiDlgState*>(cs->lpCreateParams);
            if (st) st->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
            return TRUE;
        }
        case WM_CREATE: {
            if (!st) return -1;
            HINSTANCE hInst = GetModuleHandleW(nullptr);
            UINT dpi = queryDpiForWindow(hwnd);
            if (dpi == 0) dpi = 96;
            const float scale = static_cast<float>(dpi) / 96.0f;
            auto S = [scale](int v) -> int {
                return static_cast<int>(std::lround(static_cast<float>(v) * scale));
            };

            st->font = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");

            auto mkCtrl = [&](const wchar_t* cls, const wchar_t* text, DWORD style,
                              int x, int y, int w, int h, int id) -> HWND {
                HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                                         S(x), S(y), S(w), S(h), hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
                if (c && st->font) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);
                return c;
            };

            mkCtrl(L"STATIC",
                   L"\u63d0\u793a\uff1a\u6dfb\u52a0\u540e\u5728 vvvv \u5927\u65b9\u6846\u7f6e\u9876\u663e\u793a\uff0c\u4e14\u652f\u6301\u76f4\u63a5\u62fc\u97f3\u8054\u60f3\uff08\u5982 qinqin / tietie\uff09\u3002",
                   0, 14, 12, 512, 22, 0);

            mkCtrl(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOTIFY | WS_TABSTOP,
                   14, 40, 512, 162, kIdList);

            mkCtrl(L"BUTTON", L"\u4e0a\u79fb", BS_PUSHBUTTON | WS_TABSTOP,
                   14, 210, 82, 28, kIdBtnUp);
            mkCtrl(L"BUTTON", L"\u5220\u9664\u9009\u4e2d", BS_PUSHBUTTON | WS_TABSTOP,
                   104, 210, 96, 28, kIdBtnDel);
            mkCtrl(L"BUTTON", L"\u7528\u8bb0\u4e8b\u672c\u6253\u5f00\u8bcd\u5e93\u6587\u4ef6...", BS_PUSHBUTTON | WS_TABSTOP,
                   316, 210, 210, 28, kIdBtnOpenFile);

            mkCtrl(L"STATIC", L"\u989c\u6587\u5b57\u5185\u5bb9\uff08\u5982 (*\u02d8 \u00b3\u02d8)\u2665\uff09\uff1a",
                   0, 14, 250, 250, 20, 0);
            mkCtrl(L"STATIC", L"\u6807\u7b7e\uff08\u5982\uff1a\u4eb2\u4eb2 / \u8d34\u8d34\uff09\uff1a",
                   0, 276, 250, 240, 20, 0);

            mkCtrl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                   14, 272, 252, 26, kIdEditText);
            mkCtrl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                   276, 272, 250, 26, kIdEditLabel);

            mkCtrl(L"STATIC", L"\u89e6\u53d1\u62fc\u97f3\uff08\u591a\u4e2a\u7528 | \u5206\u9694\uff0c\u5982 qinqin|tietie|al\uff09\uff1a",
                   0, 14, 306, 390, 20, 0);
            mkCtrl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                   14, 328, 396, 26, kIdEditKeys);
            mkCtrl(L"BUTTON", L"\u6dfb\u52a0 / \u66f4\u65b0", BS_PUSHBUTTON | WS_TABSTOP,
                   418, 327, 108, 28, kIdBtnAdd);

            mkCtrl(L"BUTTON", L"\u4fdd\u5b58\u751f\u6548", BS_DEFPUSHBUTTON | WS_TABSTOP,
                   306, 374, 106, 30, kIdBtnSave);
            mkCtrl(L"BUTTON", L"\u53d6\u6d88", BS_PUSHBUTTON | WS_TABSTOP,
                   420, 374, 106, 30, kIdBtnCancel);

            const int scaledW = S(556);
            const int scaledH = S(452);
            RECT wa{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
            int x = (wa.left + wa.right - scaledW) / 2;
            int y = (wa.top + wa.bottom - scaledH) / 2;
            SetWindowPos(hwnd, HWND_TOPMOST, x, y, scaledW, scaledH, SWP_NOACTIVATE);

            st->refreshList(st->items.empty() ? -1 : 0);
            return 0;
        }
        case WM_COMMAND: {
            if (!st) break;
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);
            if (id == kIdList && code == LBN_SELCHANGE) {
                int sel = static_cast<int>(SendDlgItemMessageW(hwnd, kIdList, LB_GETCURSEL, 0, 0));
                if (sel >= 0 && sel < static_cast<int>(st->items.size())) {
                    SetDlgItemTextW(hwnd, kIdEditText, utf8_to_utf16(st->items[sel].text).c_str());
                    SetDlgItemTextW(hwnd, kIdEditLabel, utf8_to_utf16(st->items[sel].label).c_str());
                    SetDlgItemTextW(hwnd, kIdEditKeys, utf8_to_utf16(st->items[sel].keywords).c_str());
                }
                return 0;
            }
            if (id == kIdBtnAdd) {
                std::string text = utf16_to_utf8(getCtrlText(hwnd, kIdEditText));
                std::string label = utf16_to_utf8(getCtrlText(hwnd, kIdEditLabel));
                std::string keys = utf16_to_utf8(getCtrlText(hwnd, kIdEditKeys));
                auto trim = [](std::string& s) {
                    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
                    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
                };
                trim(text);
                trim(label);
                trim(keys);
                if (text.empty()) {
                    MessageBoxW(hwnd, L"\u8bf7\u5148\u586b\u5199\u989c\u6587\u5b57\u5185\u5bb9\u3002", L"LiteIME", MB_OK | MB_ICONINFORMATION);
                    return 0;
                }
                int updatedIdx = -1;
                for (size_t i = 0; i < st->items.size(); ++i) {
                    if (st->items[i].text == text) {
                        st->items[i].label = label;
                        st->items[i].keywords = keys;
                        updatedIdx = static_cast<int>(i);
                        break;
                    }
                }
                if (updatedIdx < 0) {
                    // Prepend newly added affectionate kaomoji right at the top for immediate visibility
                    st->items.insert(st->items.begin(), engine::CustomKaomojiItem{text, label, keys});
                    updatedIdx = 0;
                }
                SetDlgItemTextW(hwnd, kIdEditText, L"");
                SetDlgItemTextW(hwnd, kIdEditLabel, L"");
                SetDlgItemTextW(hwnd, kIdEditKeys, L"");
                st->refreshList(updatedIdx);
                return 0;
            }
            if (id == kIdBtnDel) {
                int sel = static_cast<int>(SendDlgItemMessageW(hwnd, kIdList, LB_GETCURSEL, 0, 0));
                if (sel >= 0 && sel < static_cast<int>(st->items.size())) {
                    st->items.erase(st->items.begin() + sel);
                    int nextSel = st->items.empty() ? -1 : std::min(sel, static_cast<int>(st->items.size()) - 1);
                    st->refreshList(nextSel);
                }
                return 0;
            }
            if (id == kIdBtnUp) {
                int sel = static_cast<int>(SendDlgItemMessageW(hwnd, kIdList, LB_GETCURSEL, 0, 0));
                if (sel > 0 && sel < static_cast<int>(st->items.size())) {
                    std::swap(st->items[sel], st->items[sel - 1]);
                    st->refreshList(sel - 1);
                }
                return 0;
            }
            if (id == kIdBtnOpenFile) {
                if (!st->filePath.empty()) {
                    ShellExecuteW(hwnd, L"open", st->filePath.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
                return 0;
            }
            if (id == kIdBtnSave) {
                st->accepted = true;
                st->closed = true;
                DestroyWindow(hwnd);
                return 0;
            }
            if (id == kIdBtnCancel) {
                st->accepted = false;
                st->closed = true;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            if (st) {
                st->accepted = false;
                st->closed = true;
            }
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

bool editCustomKaomojiItems(HWND owner,
                            std::vector<engine::CustomKaomojiItem>& items,
                            const std::filesystem::path& filePath) {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    static bool registered = false;
    constexpr const wchar_t* kCls = L"LiteIME_CustomKaomojiDialog";
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = customKaomojiDlgProc;
        wc.hInstance = hInst;
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kCls;
        RegisterClassExW(&wc);
        registered = true;
    }

    KaomojiDlgState st;
    st.items = items;
    st.filePath = filePath;

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        kCls,
        L"LiteIME \u81ea\u5b9a\u4e49\u989c\u6587\u5b57\u7ba1\u7406",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        100, 100, 560, 460,
        owner, nullptr, hInst, &st);
    if (!dlg) return false;

    if (owner) EnableWindow(owner, FALSE);
    MSG msg{};
    while (!st.closed && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (owner) EnableWindow(owner, TRUE);

    if (!st.accepted) return false;
    items = std::move(st.items);
    return true;
}

bool showCustomKaomojiDialog(HWND owner, engine::UserDict& userDict) {
    auto copy = userDict.customKaomojis();
    if (!editCustomKaomojiItems(owner, copy, userDict.customKaomojisPath())) {
        return false;
    }
    userDict.setCustomKaomojis(std::move(copy));
    userDict.saveCustomKaomojis();
    return true;
}

} // namespace liteime::host
