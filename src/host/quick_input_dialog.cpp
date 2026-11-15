#include "host/quick_input_dialog.hpp"
#include "host/host_messages.hpp"

#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace liteime::host {

namespace {

constexpr int kIdList      = 1001;
constexpr int kIdEditLabel = 1002;
constexpr int kIdEditValue = 1003;
constexpr int kIdChkMask   = 1004;
constexpr int kIdBtnAdd    = 1005;
constexpr int kIdBtnDel    = 1006;
constexpr int kIdBtnSave   = 1007;
constexpr int kIdBtnCancel = 1008;

std::wstring utf8ToWide(std::string_view s) {
    if (s.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), needed);
    return out;
}

std::string wideToUtf8(std::wstring_view ws) {
    if (ws.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring getDlgItemTextW(HWND hwnd, int id) {
    HWND ctrl = GetDlgItem(hwnd, id);
    if (!ctrl) return {};
    int len = GetWindowTextLengthW(ctrl);
    if (len <= 0) return {};
    std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(ctrl, buf.data(), len + 1);
    buf.resize(static_cast<size_t>(len));
    return buf;
}

struct DialogState {
    std::vector<engine::QuickInputItem> items;
    HWND hwnd = nullptr;
    HFONT font = nullptr;
    bool accepted = false;
    bool closed = false;

    void refreshList(int selectIdx = -1) {
        HWND lb = GetDlgItem(hwnd, kIdList);
        if (!lb) return;
        SendMessageW(lb, LB_RESETCONTENT, 0, 0);
        for (const auto& q : items) {
            std::string masked = engine::UserDict::maskQuickValue(q.value);
            std::string line = q.label.empty() ? masked : (q.label + " : " + masked);
            std::wstring wline = utf8ToWide(line);
            SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wline.c_str()));
        }
        if (selectIdx >= 0 && selectIdx < static_cast<int>(items.size())) {
            SendMessageW(lb, LB_SETCURSEL, static_cast<WPARAM>(selectIdx), 0);
        }
    }
};

LRESULT CALLBACK quickInputDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            st = reinterpret_cast<DialogState*>(cs->lpCreateParams);
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
                   L"\u8bf4\u660e\uff1a\u4e2d\u6587\u6a21\u5f0f\u4e0b\u8f93\u5165 vvv \u53ef\u5feb\u901f\u586b\u5165\u4e0b\u5217\u5185\u5bb9\uff08\u5019\u9009\u6846\u4ec5\u663e\u793a\u524d\u6bb5\u8131\u654f\u9884\u89c8\uff0c\u4e0a\u5c4f\u540e\u8f93\u51fa\u5b8c\u6574\u503c\uff09\u3002",
                   0, 14, 12, 452, 36, 0);

            mkCtrl(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOTIFY | WS_TABSTOP,
                   14, 52, 452, 130, kIdList);

            mkCtrl(L"STATIC", L"\u540d\u79f0 / \u6807\u7b7e\uff08\u5982\uff1a\u624b\u673a\u53f7\u3001\u8eab\u4efd\u8bc1\u3001\u90ae\u7bb1\uff09\uff1a",
                   0, 14, 190, 320, 20, 0);
            mkCtrl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                   14, 212, 200, 26, kIdEditLabel);

            mkCtrl(L"BUTTON", L"\u9690\u85cf\u660e\u6587", BS_AUTOCHECKBOX | WS_TABSTOP,
                   228, 214, 100, 22, kIdChkMask);
            SendDlgItemMessageW(hwnd, kIdChkMask, BM_SETCHECK, BST_CHECKED, 0);

            mkCtrl(L"STATIC", L"\u5b8c\u6574\u5185\u5bb9\uff08\u4ec5\u5728\u672c\u673a\u672c\u5730\u4fdd\u5b58\uff09\uff1a",
                   0, 14, 246, 320, 20, 0);
            HWND editVal = mkCtrl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP,
                                  14, 268, 336, 26, kIdEditValue);
            SendMessageW(editVal, EM_SETPASSWORDCHAR, L'\u25cf', 0);

            mkCtrl(L"BUTTON", L"\u6dfb\u52a0 / \u66f4\u65b0", BS_PUSHBUTTON | WS_TABSTOP,
                   358, 267, 108, 28, kIdBtnAdd);
            mkCtrl(L"BUTTON", L"\u5220\u9664\u9009\u4e2d", BS_PUSHBUTTON | WS_TABSTOP,
                   358, 211, 108, 28, kIdBtnDel);

            mkCtrl(L"BUTTON", L"\u4fdd\u5b58\u5e76\u5173\u95ed", BS_DEFPUSHBUTTON | WS_TABSTOP,
                   246, 314, 106, 30, kIdBtnSave);
            mkCtrl(L"BUTTON", L"\u53d6\u6d88", BS_PUSHBUTTON | WS_TABSTOP,
                   360, 314, 106, 30, kIdBtnCancel);

            const int scaledW = S(496);
            const int scaledH = S(396);
            HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            int sx = (GetSystemMetrics(SM_CXSCREEN) - scaledW) / 2;
            int sy = (GetSystemMetrics(SM_CYSCREEN) - scaledH) / 2;
            if (GetMonitorInfoW(mon, &mi)) {
                sx = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - scaledW) / 2;
                sy = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - scaledH) / 2;
            }
            SetWindowPos(hwnd, HWND_TOPMOST, sx, sy, scaledW, scaledH, SWP_NOACTIVATE);

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
                    SetDlgItemTextW(hwnd, kIdEditLabel, utf8ToWide(st->items[sel].label).c_str());
                    SetDlgItemTextW(hwnd, kIdEditValue, utf8ToWide(st->items[sel].value).c_str());
                }
                return 0;
            }
            if (id == kIdChkMask) {
                bool hide = (SendDlgItemMessageW(hwnd, kIdChkMask, BM_GETCHECK, 0, 0) == BST_CHECKED);
                HWND editVal = GetDlgItem(hwnd, kIdEditValue);
                SendMessageW(editVal, EM_SETPASSWORDCHAR, hide ? L'\u25cf' : 0, 0);
                InvalidateRect(editVal, nullptr, TRUE);
                return 0;
            }
            if (id == kIdBtnAdd) {
                std::string label = wideToUtf8(getDlgItemTextW(hwnd, kIdEditLabel));
                std::string value = wideToUtf8(getDlgItemTextW(hwnd, kIdEditValue));
                if (value.empty()) {
                    MessageBoxW(hwnd, L"\u8bf7\u5148\u8f93\u5165\u8981\u5feb\u901f\u586b\u5165\u7684\u5185\u5bb9\u3002", L"LiteIME", MB_ICONINFORMATION);
                    return 0;
                }
                int sel = static_cast<int>(SendDlgItemMessageW(hwnd, kIdList, LB_GETCURSEL, 0, 0));
                bool updated = false;
                if (sel >= 0 && sel < static_cast<int>(st->items.size()) &&
                    !label.empty() && st->items[sel].label == label) {
                    st->items[sel].value = std::move(value);
                    updated = true;
                }
                if (!updated) {
                    st->items.push_back(engine::QuickInputItem{std::move(label), std::move(value)});
                    sel = static_cast<int>(st->items.size()) - 1;
                }
                st->refreshList(sel);
                SetDlgItemTextW(hwnd, kIdEditLabel, L"");
                SetDlgItemTextW(hwnd, kIdEditValue, L"");
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

bool editQuickInputItems(HWND owner, std::vector<engine::QuickInputItem>& items) {
    static bool s_busy = false;
    if (s_busy) return false;
    s_busy = true;

    struct Guard {
        bool& flag;
        ~Guard() { flag = false; }
    } guard{s_busy};

    // Show first-time privacy/safety notice only when user has no existing quick-input items
    if (items.empty()) {
        int choice = MessageBoxW(
            owner,
            L"\u26a0\ufe0f \u9690\u79c1\u4e0e\u5b89\u5168\u63d0\u9192\uff1a\n\n"
            L"\u300c\u5feb\u8f93\u5165 (vvv)\u300d\u4ec5\u5c06\u4fe1\u606f\u4fdd\u5b58\u5728\u672c\u673a\u672c\u5730\u6587\u4ef6\uff0c\n"
            L"\u5019\u9009\u6846\u4ec5\u663e\u793a\u524d\u6bb5\u8131\u654f\u9884\u89c8\uff0c\u4f46\u9009\u4e2d\u4e0a\u5c4f\u65f6\u4f1a\u8f93\u5165\u5b8c\u6574\u5185\u5bb9\u3002\n\n"
            L"\u8bf7\u786e\u4fdd\u6ca1\u6709\u5916\u4eba\u63a5\u89e6\u60a8\u7684\u8bbe\u5907\u540e\u518d\u8c28\u614e\u586b\u5199\u624b\u673a\u53f7\u3001\u8eab\u4efd\u8bc1\u53f7\u3001\u8d26\u53f7\u7b49\u654f\u611f\u4fe1\u606f\u3002\n"
            L"\u662f\u5426\u786e\u8ba4\u7ee7\u7eed\u7ba1\u7406\uff1f",
            L"LiteIME \u5feb\u8f93\u5165 \u00b7 \u5b89\u5168\u63d0\u9192",
            MB_ICONWARNING | MB_YESNO | MB_TOPMOST | MB_SETFOREGROUND);
        if (choice != IDYES) {
            return false;
        }
    }

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    static bool s_registered = false;
    constexpr const wchar_t* kClsName = L"LiteIME_QuickInputDialog";
    if (!s_registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = quickInputDlgProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = kClsName;
        RegisterClassExW(&wc);
        s_registered = true;
    }

    DialogState st;
    st.items = items;

    const int dlgW = 496;
    const int dlgH = 396;
    int sx = (GetSystemMetrics(SM_CXSCREEN) - dlgW) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - dlgH) / 2;

    HWND dlg = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_DLGMODALFRAME,
        kClsName,
        L"LiteIME \u5feb\u8f93\u5165 (vvv) \u7ba1\u7406",
        WS_CAPTION | WS_SYSMENU,
        sx, sy, dlgW, dlgH,
        owner, nullptr, hInst, &st);
    if (!dlg) return false;

    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);

    MSG msg{};
    while (!st.closed && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (st.accepted) {
        items = std::move(st.items);
        return true;
    }
    return false;
}

bool showQuickInputDialog(HWND owner, engine::UserDict& userDict) {
    auto items = userDict.quickInputs();
    if (!editQuickInputItems(owner, items)) {
        return false;
    }
    userDict.setQuickInputs(std::move(items));
    userDict.saveQuickInputs();
    return true;
}

} // namespace liteime::host
