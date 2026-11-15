#include "common/config.hpp"
#include "common/ipc_protocol.hpp"
#include "common/utf.hpp"
#include "engine/bilingual_engine.hpp"
#include "engine/binary_dict.hpp"
#include "host/candidate_window.hpp"
#include "host/host_messages.hpp"
#include "host/pipe_server.hpp"
#include "host/quick_input_dialog.hpp"
#include "host/status_window.hpp"
#include "host/tray_icon.hpp"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr const wchar_t* kHostClassName = L"LiteIME_HostMessageWindow";
constexpr UINT_PTR kTimerFlushUserDict = 1;
constexpr UINT kFlushIntervalMs = 5000;

struct HostApp {
    HINSTANCE hInst = nullptr;
    HWND msgHwnd = nullptr;
    std::unique_ptr<liteime::engine::BinaryDict> dict = std::make_unique<liteime::engine::BinaryDict>();
    liteime::engine::BilingualEngine engine;
    liteime::host::CandidateWindow candidateWin;
    liteime::host::StatusWindow statusWin;
    liteime::host::TrayIcon tray;
    liteime::host::PipeServer pipeServer;
    fs::path dictPath;
    fs::path packsDir;
    fs::path userDictPath;
    fs::path customDictsDir;
    std::vector<std::unique_ptr<liteime::engine::BinaryDict>> extraDicts;
    std::mutex stateMutex;
    std::atomic<bool> uiPostPending{false};
    int lastCaretX = 300;
    int lastCaretY = 300;
    int lastCaretH = 22;
    uint64_t activeConnId = 0;
    uint32_t activeClientPid = 0;
    uint64_t activeTargetHwnd = 0;
    bool imeActive = false;
    bool shouldShowCandidates = false;
    bool forceStatusRedraw = false;
    bool statusShown = false;
    bool lastStatusChinese = true;
    bool lastStatusPunct = true;
    liteime::CandidateLayout lastStatusLayout = liteime::CandidateLayout::Horizontal;
    liteime::SkinTheme lastStatusSkin = liteime::SkinTheme::CloudMist;
    bool lastStatusSquare = true;

    void resolvePaths() {
        wchar_t exePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        fs::path exeDir = fs::path(exePath).parent_path();

        const fs::path candidates[] = {
            exeDir / "data" / "liteime.dict",
            exeDir / "liteime.dict",
            exeDir.parent_path() / "data" / "liteime.dict",
            exeDir.parent_path().parent_path() / "data" / "liteime.dict",
        };
        for (const auto& p : candidates) {
            if (fs::exists(p)) {
                dictPath = p;
                break;
            }
        }

        wchar_t appData[MAX_PATH]{};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
            userDictPath = fs::path(appData) / "LiteIME" / "user_dict.tsv";
        } else {
            userDictPath = exeDir / "user_dict.tsv";
        }
        customDictsDir = userDictPath.parent_path() / "dicts";

        const fs::path packCandidates[] = {
            dictPath.empty() ? fs::path{} : (dictPath.parent_path() / "packs"),
            exeDir / "data" / "packs",
            exeDir.parent_path() / "data" / "packs",
        };
        for (const auto& p : packCandidates) {
            if (!p.empty() && fs::exists(p) && fs::is_directory(p)) {
                packsDir = p;
                break;
            }
        }
    }

    void reloadExtraDictionaries() {
        liteime::EngineConfig cfg;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            cfg = engine.config();
        }

        std::vector<std::unique_ptr<liteime::engine::BinaryDict>> loaded;
        auto loadExtra = [&](const fs::path& p) {
            if (p.empty() || !fs::exists(p)) return;
            auto d = std::make_unique<liteime::engine::BinaryDict>();
            if (d->loadFile(p)) {
                loaded.push_back(std::move(d));
            }
        };

        if (!packsDir.empty()) {
            if (cfg.enableEnPack) loadExtra(packsDir / "common_en.dict");
            if (cfg.enableCsPack) loadExtra(packsDir / "computer.dict");
            if (cfg.enableProPack) loadExtra(packsDir / "pro_fields.dict");
            if (cfg.enableExtPack) loadExtra(packsDir / "ext_cn.dict");
        }

        std::error_code ec;
        fs::create_directories(customDictsDir, ec);
        if (cfg.enableCustomDicts && fs::exists(customDictsDir, ec)) {
            for (const auto& entry : fs::directory_iterator(customDictsDir, ec)) {
                if (!entry.is_regular_file()) continue;
                fs::path p = entry.path();
                std::string ext = p.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (ext == ".yaml" || ext == ".tsv" || ext == ".txt") {
                    fs::path compiled = customDictsDir / (p.filename().string() + ".compiled.dict");
                    bool needCompile = !fs::exists(compiled, ec);
                    if (!needCompile) {
                        auto srcTime = fs::last_write_time(p, ec);
                        auto dstTime = fs::last_write_time(compiled, ec);
                        if (!ec && srcTime > dstTime) needCompile = true;
                    }
                    if (needCompile) {
                        std::vector<liteime::engine::RawPhraseRecord> phrases;
                        std::vector<liteime::engine::RawEnglishRecord> english;
                        if (liteime::engine::BinaryDict::parseDictYaml(p, phrases, english, false) &&
                            (!phrases.empty() || !english.empty())) {
                            liteime::engine::BinaryDict::compileToFile(compiled, std::move(phrases), std::move(english));
                        }
                    }
                }
            }
            for (const auto& entry : fs::directory_iterator(customDictsDir, ec)) {
                if (!entry.is_regular_file()) continue;
                fs::path p = entry.path();
                std::string ext = p.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (ext == ".dict") {
                    loadExtra(p);
                }
            }
        }

        std::lock_guard<std::mutex> lock(stateMutex);
        engine.clearExtraDictionaries();
        extraDicts = std::move(loaded);
        for (const auto& d : extraDicts) {
            engine.addExtraDictionary(d.get());
        }
    }

    void loadDictionaries() {
        auto newMain = std::make_unique<liteime::engine::BinaryDict>();
        bool mainLoaded = (!dictPath.empty() && newMain->loadFile(dictPath));
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            if (mainLoaded) {
                dict = std::move(newMain);
            }
            engine.setDictionary(dict.get());
            if (engine.userDict().saveIfDirty()) {
                engine.userDict().load(userDictPath);
            }
            liteime::engine::UserDict::loadConfig(engine.userDict().configPath(), engine.config());
        }
        reloadExtraDictionaries();
    }

    void scheduleUiSync() {
        if (msgHwnd && !uiPostPending.exchange(true, std::memory_order_acq_rel)) {
            if (!PostMessageW(msgHwnd, liteime::host::WM_APP_SYNC_UI, 0, 0)) {
                uiPostPending.store(false, std::memory_order_release);
            }
        }
    }

    void syncStatusWindow(bool active, const liteime::EngineConfig& cfg, bool forceRedraw = false) {
        const bool shouldShow = active && cfg.showStatusBar;
        if (!shouldShow) {
            if (statusShown) {
                statusWin.setVisible(false);
                statusShown = false;
            }
            return;
        }
        const bool changed = forceRedraw || !statusShown ||
                             lastStatusChinese != cfg.chineseMode ||
                             lastStatusPunct != cfg.chinesePunct ||
                             lastStatusLayout != cfg.layout ||
                             lastStatusSkin != cfg.skin ||
                             lastStatusSquare != cfg.squareFrame;
        if (changed) {
            statusWin.update(cfg);
            statusShown = true;
            lastStatusChinese = cfg.chineseMode;
            lastStatusPunct = cfg.chinesePunct;
            lastStatusLayout = cfg.layout;
            lastStatusSkin = cfg.skin;
            lastStatusSquare = cfg.squareFrame;
        }
    }

    void syncUiFromState() {
        uiPostPending.store(false, std::memory_order_release);

        bool activeSnap = false;
        bool forceStatusSnap = false;
        liteime::EngineConfig cfgSnap;
        liteime::host::CandidateViewState vs;
        bool showCand = false;

        {
            std::lock_guard<std::mutex> lock(stateMutex);
            activeSnap = imeActive;
            forceStatusSnap = std::exchange(forceStatusRedraw, false);
            cfgSnap = engine.config();
            showCand = imeActive && shouldShowCandidates && engine.isComposing() && !engine.allCandidates().empty();
            if (showCand) {
                vs.visible = true;
                vs.headerBuffer = engine.formattedHeader();
                vs.items = engine.currentPageItems();
                vs.selectedIndex = engine.selectedIndexInPage();
                vs.page = engine.currentPage();
                vs.totalPages = engine.totalPages();
                vs.chineseMode = cfgSnap.chineseMode;
                vs.skin = cfgSnap.skin;
                vs.layout = cfgSnap.layout;
                vs.squareFrame = cfgSnap.squareFrame;
                vs.caretX = lastCaretX;
                vs.caretY = lastCaretY;
                vs.caretHeight = lastCaretH;
            }
        }

        syncStatusWindow(activeSnap, cfgSnap, forceStatusSnap);
        tray.updateTip(cfgSnap);
        if (showCand) {
            candidateWin.update(vs);
        } else {
            candidateWin.hide();
        }
    }

    void onClientDisconnected(uint64_t connId) {
        bool needUi = false;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            if (connId != 0 && connId == activeConnId) {
                imeActive = false;
                activeConnId = 0;
                activeClientPid = 0;
                activeTargetHwnd = 0;
                shouldShowCandidates = false;
                engine.notifyFocusChanged(false);
                needUi = true;
            }
        }
        if (needUi) {
            scheduleUiSync();
        }
    }

    static bool isForegroundQuickInputAuthorized(uint32_t clientPid, uint64_t reqHwnd) {
        if (clientPid == 0 || reqHwnd == 0) return false;
        HWND targetHwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(reqHwnd));
        if (!IsWindow(targetHwnd)) return false;
        HWND fg = GetForegroundWindow();
        if (!fg) return false;
        DWORD fgPid = 0;
        DWORD reqPid = 0;
        GetWindowThreadProcessId(fg, &fgPid);
        GetWindowThreadProcessId(targetHwnd, &reqPid);
        return fgPid == clientPid && reqPid == clientPid;
    }

    static void fillResponseFromAction(const liteime::engine::KeyActionResult& act,
                                       const liteime::EngineConfig& cfgSnap,
                                       bool composing,
                                       liteime::ipc::Response& resp) {
        resp.magic = liteime::ipc::kProtocolMagic;
        resp.version = liteime::ipc::kProtocolVersion;
        resp.consumed = act.consumed ? 1 : 0;
        resp.isEnglish = cfgSnap.chineseMode ? 0 : 1;
        resp.isFullWidth = cfgSnap.fullWidth ? 1 : 0;
        resp.isCnPunct = cfgSnap.chinesePunct ? 1 : 0;
        resp.isComposing = composing ? 1 : 0;
        resp.preeditLen = 0;
        resp.commitLen = 0;
        resp.caretPos = 0;

        if (!act.preeditText.empty()) {
            size_t len = liteime::copyUtf8Bounded(act.preeditText, resp.preedit, sizeof(resp.preedit));
            resp.preeditLen = static_cast<uint16_t>(len);
            resp.caretPos = resp.preeditLen;
        }
        if (!act.commitText.empty()) {
            size_t len = liteime::copyUtf8Bounded(act.commitText, resp.commit, sizeof(resp.commit));
            resp.commitLen = static_cast<uint16_t>(len);
        }
    }

    void pushNotifyResponse(uint32_t targetPid, uint64_t targetHwnd, const liteime::ipc::Response& resp) {
        if (targetPid == 0) return;
        liteime::ipc::NotifyPacket pkt{};
        pkt.magic = liteime::ipc::kProtocolMagic;
        pkt.type = static_cast<uint16_t>(liteime::ipc::NotifyType::ApplyResponse);
        pkt.targetHwnd = targetHwnd;
        pkt.response = resp;
        pipeServer.sendNotify(targetPid, pkt);
    }

    void handleIpc(const liteime::ipc::Request& req, liteime::ipc::Response& resp,
                   uint64_t connId, uint32_t clientPid) {
        resp.magic = liteime::ipc::kProtocolMagic;
        resp.version = liteime::ipc::kProtocolVersion;

        liteime::engine::KeyActionResult act{};
        bool needUi = false;
        bool openQuickDlg = false;
        bool composing = false;
        liteime::EngineConfig cfgSnap;

        {
            std::lock_guard<std::mutex> lock(stateMutex);
            switch (req.type) {
                case liteime::ipc::MsgType::KeyTest:
                    act = engine.handleKey(req.vk, req.modifiers, req.isKeyUp != 0, true);
                    break;
                case liteime::ipc::MsgType::KeyEvent:
                    activeConnId = connId;
                    activeClientPid = clientPid;
                    if (req.hwnd != 0) activeTargetHwnd = req.hwnd;
                    imeActive = true;
                    if (req.hasCaret != 0) {
                        lastCaretX = req.caretX;
                        lastCaretY = req.caretY;
                        lastCaretH = std::clamp(req.caretHeight, 12, 64);
                    }
                    act = engine.handleKey(req.vk, req.modifiers, req.isKeyUp != 0, false);
                    if (act.isQuickInput && !isForegroundQuickInputAuthorized(clientPid, req.hwnd)) {
                        act.commitText.clear();
                    }
                    shouldShowCandidates = engine.isComposing() && !engine.allCandidates().empty() &&
                                           (act.showCandidates || (req.isKeyUp != 0 && !act.consumed));
                    openQuickDlg = act.openQuickInputDialog;
                    needUi = true;
                    break;
                case liteime::ipc::MsgType::FocusChanged:
                    if (req.focusGained != 0) {
                        activeConnId = connId;
                        activeClientPid = clientPid;
                        activeTargetHwnd = req.hwnd;
                        imeActive = true;
                        engine.notifyFocusChanged(true);
                        shouldShowCandidates = false;
                        needUi = true;
                    } else if (activeConnId == 0 || connId == activeConnId) {
                        imeActive = false;
                        activeConnId = 0;
                        activeClientPid = 0;
                        activeTargetHwnd = 0;
                        shouldShowCandidates = false;
                        engine.notifyFocusChanged(false);
                        needUi = true;
                    }
                    break;
                case liteime::ipc::MsgType::ResetComposition:
                    if (activeConnId == 0 || connId == activeConnId) {
                        engine.resetComposition();
                        shouldShowCandidates = false;
                        needUi = true;
                    }
                    break;
                case liteime::ipc::MsgType::UpdateCaret:
                    if (req.hasCaret != 0 && (activeConnId == 0 || connId == activeConnId)) {
                        lastCaretX = req.caretX;
                        lastCaretY = req.caretY;
                        lastCaretH = std::clamp(req.caretHeight, 12, 64);
                        needUi = shouldShowCandidates;
                    }
                    act.consumed = true;
                    break;
                case liteime::ipc::MsgType::Ping:
                default:
                    break;
            }
            composing = engine.isComposing();
            cfgSnap = engine.config();
        }

        if (needUi) {
            scheduleUiSync();
        }
        if (openQuickDlg && msgHwnd) {
            PostMessageW(msgHwnd, liteime::host::WM_APP_OPEN_QUICK_INPUT, 0, 0);
        }

        fillResponseFromAction(act, cfgSnap, composing, resp);
    }

    void handleAction(liteime::host::HostAction action) {
        if (action == liteime::host::HostAction::OpenCustomDictFolder) {
            std::error_code ec;
            fs::create_directories(customDictsDir, ec);
            ShellExecuteW(nullptr, L"open", customDictsDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }
        if (action == liteime::host::HostAction::OpenQuickInputManager) {
            if (msgHwnd) {
                PostMessageW(msgHwnd, liteime::host::WM_APP_OPEN_QUICK_INPUT, 0, 0);
            }
            return;
        }
        if (action == liteime::host::HostAction::ExitHost) {
            liteime::EngineConfig cfgSnap;
            fs::path cfgPath;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                cfgSnap = engine.config();
                cfgPath = engine.userDict().configPath();
            }
            liteime::engine::UserDict::saveConfig(cfgPath, cfgSnap);
            PostQuitMessage(0);
            return;
        }

        bool needReloadExtra = false;
        bool needReloadAll = false;
        bool cancelledComposition = false;
        uint32_t notifyPid = 0;
        uint64_t notifyHwnd = 0;
        liteime::EngineConfig cfgSnap;
        fs::path cfgPath;

        {
            std::lock_guard<std::mutex> lock(stateMutex);
            auto& cfg = engine.config();
            switch (action) {
                case liteime::host::HostAction::ToggleChineseEnglish:
                    cfg.chineseMode = !cfg.chineseMode;
                    if (!cfg.chineseMode && engine.isComposing()) {
                        engine.resetComposition();
                        shouldShowCandidates = false;
                        cancelledComposition = true;
                        notifyPid = activeClientPid;
                        notifyHwnd = activeTargetHwnd;
                    }
                    break;
                case liteime::host::HostAction::TogglePunct:
                    cfg.chinesePunct = !cfg.chinesePunct;
                    break;
                case liteime::host::HostAction::ToggleMixedEnglish:
                    cfg.mixedEnglish = !cfg.mixedEnglish;
                    break;
                case liteime::host::HostAction::ToggleLayout:
                    cfg.layout = (cfg.layout == liteime::CandidateLayout::Horizontal)
                        ? liteime::CandidateLayout::Vertical
                        : liteime::CandidateLayout::Horizontal;
                    break;
                case liteime::host::HostAction::ToggleSquareFrame:
                    cfg.squareFrame = !cfg.squareFrame;
                    break;
                case liteime::host::HostAction::SetPreeditPreview:
                    cfg.inlinePreedit = liteime::InlinePreeditMode::CandidatePreview;
                    break;
                case liteime::host::HostAction::SetPreeditPinyin:
                    cfg.inlinePreedit = liteime::InlinePreeditMode::CommittedPlusPinyin;
                    break;
                case liteime::host::HostAction::SetPreeditNone:
                    cfg.inlinePreedit = liteime::InlinePreeditMode::None;
                    break;
                case liteime::host::HostAction::CycleSkin:
                    cfg.skin = static_cast<liteime::SkinTheme>((static_cast<int>(cfg.skin) + 1) % 4);
                    break;
                case liteime::host::HostAction::SetSkinCloudMist:
                    cfg.skin = liteime::SkinTheme::CloudMist;
                    break;
                case liteime::host::HostAction::SetSkinMatchaJade:
                    cfg.skin = liteime::SkinTheme::MatchaJade;
                    break;
                case liteime::host::HostAction::SetSkinSakuraCream:
                    cfg.skin = liteime::SkinTheme::SakuraCream;
                    break;
                case liteime::host::HostAction::SetSkinObsidianInk:
                    cfg.skin = liteime::SkinTheme::ObsidianInk;
                    break;
                case liteime::host::HostAction::ToggleFuzzyConsonants: {
                    bool next = !cfg.fuzzy.zh_z;
                    cfg.fuzzy.zh_z = next;
                    cfg.fuzzy.ch_c = next;
                    cfg.fuzzy.sh_s = next;
                    break;
                }
                case liteime::host::HostAction::ToggleFuzzyNasals: {
                    bool next = !cfg.fuzzy.n_l;
                    cfg.fuzzy.n_l = next;
                    cfg.fuzzy.an_ang = next;
                    cfg.fuzzy.en_eng = next;
                    cfg.fuzzy.in_ing = next;
                    break;
                }
                case liteime::host::HostAction::ToggleStatusBar:
                    cfg.showStatusBar = !cfg.showStatusBar;
                    break;
                case liteime::host::HostAction::SetPageSize5:
                    cfg.pageSize = 5;
                    break;
                case liteime::host::HostAction::SetPageSize6:
                    cfg.pageSize = 6;
                    break;
                case liteime::host::HostAction::SetPageSize7:
                    cfg.pageSize = 7;
                    break;
                case liteime::host::HostAction::SetPageSize8:
                    cfg.pageSize = 8;
                    break;
                case liteime::host::HostAction::ToggleFuzzyRL:
                    cfg.fuzzy.r_l = !cfg.fuzzy.r_l;
                    break;
                case liteime::host::HostAction::ToggleFuzzyFH:
                    cfg.fuzzy.f_h = !cfg.fuzzy.f_h;
                    break;
                case liteime::host::HostAction::ToggleTypoCorrection:
                    cfg.typoCorrection = !cfg.typoCorrection;
                    break;
                case liteime::host::HostAction::ToggleLooseInitial:
                    cfg.looseInitialMatch = !cfg.looseInitialMatch;
                    break;
                case liteime::host::HostAction::TogglePackComputer:
                    cfg.enableCsPack = !cfg.enableCsPack;
                    needReloadExtra = true;
                    break;
                case liteime::host::HostAction::TogglePackCommonEn:
                    cfg.enableEnPack = !cfg.enableEnPack;
                    needReloadExtra = true;
                    break;
                case liteime::host::HostAction::TogglePackExtCn:
                    cfg.enableExtPack = !cfg.enableExtPack;
                    needReloadExtra = true;
                    break;
                case liteime::host::HostAction::TogglePackProFields:
                    cfg.enableProPack = !cfg.enableProPack;
                    needReloadExtra = true;
                    break;
                case liteime::host::HostAction::ReloadDict:
                    needReloadAll = true;
                    break;
                default:
                    break;
            }
            forceStatusRedraw = true;
            cfgSnap = cfg;
            cfgPath = engine.userDict().configPath();
        }

        if (needReloadAll) {
            resolvePaths();
            loadDictionaries();
        } else if (needReloadExtra) {
            reloadExtraDictionaries();
        }
        liteime::engine::UserDict::saveConfig(cfgPath, cfgSnap);
        syncUiFromState();
        if (cancelledComposition && notifyPid != 0) {
            liteime::ipc::Response resetResp{};
            fillResponseFromAction(liteime::engine::KeyActionResult{true, false, false, false, {}, {}, {}},
                                   cfgSnap, false, resetResp);
            pushNotifyResponse(notifyPid, notifyHwnd, resetResp);
        }
    }
};

LRESULT CALLBACK hostWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    HostApp* app = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = reinterpret_cast<HostApp*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    } else {
        app = reinterpret_cast<HostApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!app) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case liteime::host::WM_APP_SYNC_UI:
            app->syncUiFromState();
            return 0;
        case liteime::host::WM_APP_ENGINE_ACTION:
            app->handleAction(static_cast<liteime::host::HostAction>(wParam));
            return 0;
        case liteime::host::WM_APP_PIN_CANDIDATE: {
            int candIdx = static_cast<int>(LOWORD(wParam));
            WORD slotOrUnpin = HIWORD(wParam);
            bool needSync = false;
            {
                std::lock_guard<std::mutex> lock(app->stateMutex);
                bool changed = (slotOrUnpin == 0xFFFF)
                    ? app->engine.unpinCandidateOnPage(candIdx)
                    : app->engine.pinCandidateOnPage(candIdx, static_cast<int>(slotOrUnpin));
                if (changed && app->engine.isComposing()) {
                    app->shouldShowCandidates = true;
                    needSync = true;
                }
            }
            if (needSync) {
                app->syncUiFromState();
            }
            return 0;
        }
        case liteime::host::WM_APP_REMOVE_USER_WORD: {
            int candIdx = static_cast<int>(wParam);
            bool needSync = false;
            {
                std::lock_guard<std::mutex> lock(app->stateMutex);
                if (app->engine.removeUserWordOnPage(candIdx) && app->engine.isComposing()) {
                    app->shouldShowCandidates = true;
                    needSync = true;
                }
            }
            if (needSync) {
                app->syncUiFromState();
            }
            return 0;
        }
        case liteime::host::WM_APP_CANDIDATE_CLICK: {
            const int slotIdx = static_cast<int>(wParam);
            liteime::engine::KeyActionResult act{};
            liteime::EngineConfig cfgSnap{};
            bool composing = false;
            uint32_t notifyPid = 0;
            uint64_t notifyHwnd = 0;
            {
                std::lock_guard<std::mutex> lock(app->stateMutex);
                if (app->engine.isComposing()) {
                    act = app->engine.selectCandidateOnPage(slotIdx);
                    if (act.isQuickInput &&
                        !HostApp::isForegroundQuickInputAuthorized(app->activeClientPid, app->activeTargetHwnd)) {
                        act.commitText.clear();
                    }
                    composing = app->engine.isComposing();
                    app->shouldShowCandidates = composing && !app->engine.allCandidates().empty() && act.showCandidates;
                    cfgSnap = app->engine.config();
                    notifyPid = app->activeClientPid;
                    notifyHwnd = app->activeTargetHwnd;
                }
            }
            app->syncUiFromState();
            if (act.openQuickInputDialog && app->msgHwnd) {
                PostMessageW(app->msgHwnd, liteime::host::WM_APP_OPEN_QUICK_INPUT, 0, 0);
            }
            if (act.consumed && notifyPid != 0) {
                liteime::ipc::Response resp{};
                HostApp::fillResponseFromAction(act, cfgSnap, composing, resp);
                app->pushNotifyResponse(notifyPid, notifyHwnd, resp);
            }
            return 0;
        }
        case liteime::host::WM_APP_CANDIDATE_PAGE: {
            const bool isNext = (wParam != 0);
            liteime::engine::KeyActionResult act{};
            liteime::EngineConfig cfgSnap{};
            bool composing = false;
            uint32_t notifyPid = 0;
            uint64_t notifyHwnd = 0;
            {
                std::lock_guard<std::mutex> lock(app->stateMutex);
                if (app->engine.isComposing()) {
                    act = isNext ? app->engine.nextPage() : app->engine.prevPage();
                    composing = app->engine.isComposing();
                    app->shouldShowCandidates = composing && !app->engine.allCandidates().empty() && act.showCandidates;
                    cfgSnap = app->engine.config();
                    notifyPid = app->activeClientPid;
                    notifyHwnd = app->activeTargetHwnd;
                }
            }
            app->syncUiFromState();
            if (act.consumed && notifyPid != 0 && !act.preeditText.empty()) {
                liteime::ipc::Response resp{};
                HostApp::fillResponseFromAction(act, cfgSnap, composing, resp);
                app->pushNotifyResponse(notifyPid, notifyHwnd, resp);
            }
            return 0;
        }
        case liteime::host::WM_APP_STATUS_POS_CHANGED: {
            const int newX = static_cast<int>(static_cast<intptr_t>(wParam));
            const int newY = static_cast<int>(static_cast<intptr_t>(lParam));
            liteime::EngineConfig cfgSnap;
            fs::path cfgPath;
            {
                std::lock_guard<std::mutex> lock(app->stateMutex);
                auto& cfg = app->engine.config();
                cfg.statusPosX = newX;
                cfg.statusPosY = newY;
                cfgSnap = cfg;
                cfgPath = app->engine.userDict().configPath();
            }
            liteime::engine::UserDict::saveConfig(cfgPath, cfgSnap);
            return 0;
        }
        case WM_TIMER:
            if (wParam == kTimerFlushUserDict) {
                std::lock_guard<std::mutex> lock(app->stateMutex);
                app->engine.userDict().saveIfDirty();
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        case WM_DISPLAYCHANGE:
            app->statusWin.clampToWorkArea();
            return 0;
        case WM_ENDSESSION:
            if (wParam != FALSE) {
                std::lock_guard<std::mutex> lock(app->stateMutex);
                app->engine.userDict().saveIfDirty();
            }
            return 0;
        case liteime::host::WM_APP_OPEN_QUICK_INPUT: {
            std::vector<liteime::engine::QuickInputItem> items;
            {
                std::lock_guard<std::mutex> lock(app->stateMutex);
                items = app->engine.userDict().quickInputs();
            }
            if (liteime::host::editQuickInputItems(nullptr, items)) {
                std::lock_guard<std::mutex> lock(app->stateMutex);
                app->engine.userDict().setQuickInputs(std::move(items));
                app->engine.userDict().saveQuickInputs();
            }
            return 0;
        }
        case liteime::host::WM_APP_TRAY_CALLBACK:
            if (lParam == WM_RBUTTONUP) {
                liteime::EngineConfig cfgSnap;
                {
                    std::lock_guard<std::mutex> lock(app->stateMutex);
                    cfgSnap = app->engine.config();
                }
                app->tray.showContextMenu(cfgSnap);
            } else if (lParam == WM_LBUTTONDBLCLK) {
                app->handleAction(liteime::host::HostAction::ToggleChineseEnglish);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void enablePerMonitorDpi() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    using FnSetDpiCtx = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    auto fn = reinterpret_cast<FnSetDpiCtx>(reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
    if (fn) {
        fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\LiteIMEHost_SingleInstance");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    enablePerMonitorDpi();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HostApp app;
    app.hInst = hInst;
    app.resolvePaths();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = hostWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kHostClassName;
    RegisterClassExW(&wc);

    app.msgHwnd = CreateWindowExW(0, kHostClassName, L"LiteIMEHost", 0,
                                  0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, &app);
    if (!app.msgHwnd) {
        CloseHandle(mutex);
        CoUninitialize();
        return 1;
    }

    app.candidateWin.create(hInst);
    app.candidateWin.setHostHwnd(app.msgHwnd);
    app.statusWin.create(hInst, app.msgHwnd);
    app.tray.create(app.msgHwnd, hInst);
    app.pipeServer.start(
        [&app](const liteime::ipc::Request& req, liteime::ipc::Response& resp, uint64_t connId, uint32_t clientPid) {
            app.handleIpc(req, resp, connId, clientPid);
        },
        [&app](uint64_t connId) {
            app.onClientDisconnected(connId);
        });
    app.loadDictionaries();
    {
        std::lock_guard<std::mutex> lock(app.stateMutex);
        app.tray.updateTip(app.engine.config());
    }
    SetTimer(app.msgHwnd, kTimerFlushUserDict, kFlushIntervalMs, nullptr);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    KillTimer(app.msgHwnd, kTimerFlushUserDict);
    app.pipeServer.stop();
    app.tray.destroy();
    app.engine.userDict().saveIfDirty();

    CloseHandle(mutex);
    CoUninitialize();
    return 0;
}
