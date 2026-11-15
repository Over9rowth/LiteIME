#include "host/candidate_window.hpp"
#include "host/status_window.hpp"
#include "engine/bilingual_engine.hpp"
#include "engine/binary_dict.hpp"

#include <windows.h>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "UI SMOKE CHECK FAILED at line " << __LINE__ << ": " << #cond << "\n"; \
            std::exit(1); \
        } \
    } while (0)

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    liteime::host::CandidateWindow candWin;
    CHECK(candWin.create(hInst));
    CHECK(candWin.hwnd() != nullptr);

    liteime::host::StatusWindow statusWin;
    CHECK(statusWin.create(hInst, nullptr));
    CHECK(statusWin.hwnd() != nullptr);

    liteime::EngineConfig cfg;
    statusWin.update(cfg);
    CHECK(IsWindowVisible(statusWin.hwnd()) != FALSE);
    RECT statusRc{};
    CHECK(GetWindowRect(statusWin.hwnd(), &statusRc) != FALSE);
    CHECK((statusRc.right - statusRc.left) > 100);
    CHECK((statusRc.bottom - statusRc.top) > 20);

    // Load real dictionary if available, otherwise fixture
    liteime::engine::BinaryDict dict;
    std::filesystem::path realDict = L"D:/LiteIME/dist/data/liteime.dict";
    if (std::filesystem::exists(realDict)) {
        CHECK(dict.loadFile(realDict));
    }

    liteime::engine::BilingualEngine eng;
    eng.setDictionary(&dict);
    for (char c : std::string("MEIYOUZHEGEKUANG")) {
        eng.handleKey(static_cast<uint32_t>(c), 0, false);
    }

    liteime::host::CandidateViewState vs;
    vs.visible = true;
    vs.headerBuffer = eng.segmentedHeader();
    vs.items = eng.currentPageItems();
    vs.selectedIndex = 0;
    vs.page = eng.currentPage();
    vs.totalPages = eng.totalPages();
    vs.chineseMode = true;
    vs.skin = liteime::SkinTheme::CloudMist;
    vs.layout = liteime::CandidateLayout::Horizontal;
    vs.caretX = 400;
    vs.caretY = 300;
    vs.caretHeight = 22;

    candWin.update(vs);
    CHECK(IsWindowVisible(candWin.hwnd()) != FALSE);
    RECT candRc{};
    CHECK(GetWindowRect(candWin.hwnd(), &candRc) != FALSE);
    CHECK((candRc.right - candRc.left) > 150);
    CHECK((candRc.bottom - candRc.top) > 50);

    // Exercise all 4 redesigned skins, both frame styles, pin badge, and horizontal/vertical pager
    if (!vs.items.empty()) {
        vs.items[0].pinnedSlot = 0;
        if (vs.items[0].comment.empty()) vs.items[0].comment = "(test)";
    }
    if (vs.totalPages < 2) vs.totalPages = 3;

    for (auto lay : {liteime::CandidateLayout::Horizontal, liteime::CandidateLayout::Vertical}) {
        for (bool sq : {true, false}) {
            for (auto sk : {liteime::SkinTheme::CloudMist, liteime::SkinTheme::MatchaJade,
                            liteime::SkinTheme::SakuraCream, liteime::SkinTheme::ObsidianInk}) {
                cfg.layout = lay;
                cfg.squareFrame = sq;
                cfg.skin = sk;
                statusWin.update(cfg);
                vs.layout = lay;
                vs.squareFrame = sq;
                vs.skin = sk;
                candWin.update(vs);
                CHECK(IsWindowVisible(statusWin.hwnd()) != FALSE);
                CHECK(IsWindowVisible(candWin.hwnd()) != FALSE);
            }
        }
    }

    // Exercise CandidateWindow hover tracking messages (WM_MOUSEMOVE + WM_MOUSELEAVE)
    SendMessageW(candWin.hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(45, 45));
    SendMessageW(candWin.hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(120, 45));
    SendMessageW(candWin.hwnd(), WM_MOUSELEAVE, 0, 0);
    CHECK(IsWindowVisible(candWin.hwnd()) != FALSE);

    // Exercise persisted statusPosX/statusPosY restoration and work-area clamping
    {
        liteime::host::StatusWindow savedStatus;
        CHECK(savedStatus.create(hInst, nullptr));
        liteime::EngineConfig savedCfg;
        savedCfg.statusPosX = 99999;
        savedCfg.statusPosY = 120;
        savedStatus.update(savedCfg);
        RECT savedRc{};
        CHECK(GetWindowRect(savedStatus.hwnd(), &savedRc) != FALSE);
        HMONITOR mon = MonitorFromWindow(savedStatus.hwnd(), MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        CHECK(GetMonitorInfoW(mon, &mi) != FALSE);
        CHECK(savedRc.right <= mi.rcWork.right);
        CHECK(savedRc.left >= mi.rcWork.left);
        CHECK(savedRc.top == 120 || savedRc.top >= mi.rcWork.top);
    }

    std::cout << "StatusPill rect=(" << statusRc.left << "," << statusRc.top << ","
              << statusRc.right << "," << statusRc.bottom << ")\n";
    std::cout << "CandidateWindow rect=(" << candRc.left << "," << candRc.top << ","
              << candRc.right << "," << candRc.bottom << ") header=" << vs.headerBuffer << "\n";

    candWin.hide();
    CHECK(IsWindowVisible(candWin.hwnd()) == FALSE);

    CoUninitialize();
    std::cout << "UI Window Smoke Test PASSED!\n";
    return 0;
}
