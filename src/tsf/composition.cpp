#include "tsf/text_service.hpp"
#include "common/utf.hpp"

#include <cmath>
#include <cstring>

namespace liteime::tsf {

namespace {

class CEditSessionBase : public ITfEditSession {
public:
    explicit CEditSessionBase(CTextService* svc, ITfContext* ctx)
        : svc_(svc), ctx_(ctx) {
        if (svc_) svc_->AddRef();
        if (ctx_) ctx_->AddRef();
    }
    virtual ~CEditSessionBase() {
        if (ctx_) ctx_->Release();
        if (svc_) svc_->Release();
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        *ppvObj = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession)) {
            *ppvObj = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG c = InterlockedDecrement(&ref_);
        if (c == 0) delete this;
        return c;
    }

protected:
    LONG ref_ = 1;
    CTextService* svc_;
    ITfContext* ctx_;
};

class CApplyResponseSession final : public CEditSessionBase {
public:
    CApplyResponseSession(CTextService* svc, ITfContext* ctx, const ipc::Response& resp,
                          bool hasPreCaret, int preCaretX, int preCaretY, uint64_t hwndVal,
                          bool requireActiveFocus)
        : CEditSessionBase(svc, ctx),
          resp_(resp),
          hasPreCaret_(hasPreCaret),
          preCaretX_(preCaretX),
          preCaretY_(preCaretY),
          hwndVal_(hwndVal),
          requireActiveFocus_(requireActiveFocus) {}

    STDMETHODIMP DoEditSession(TfEditCookie ec) override {
        if (requireActiveFocus_ && !svc_->isContextAndHwndFocused(ctx_, hwndVal_)) {
            return S_OK;
        }
        ScopedPerMonitorDpi dpiGuard;
        HRESULT hr = svc_->applyResponseInSession(ec, ctx_, resp_);
        if (SUCCEEDED(hr) && resp_.preeditLen > 0) {
            RECT postRc{};
            BOOL postFound = FALSE;
            if (SUCCEEDED(svc_->queryCaretInSession(ec, ctx_, &postRc, &postFound)) && postFound) {
                const int newX = static_cast<int>(postRc.left);
                const int newY = static_cast<int>(postRc.bottom);
                const int newH = (postRc.bottom > postRc.top) ? static_cast<int>(postRc.bottom - postRc.top) : 20;
                if (!hasPreCaret_ || std::abs(newX - preCaretX_) > 2 || std::abs(newY - preCaretY_) > 2) {
                    svc_->sendCaretUpdate(newX, newY, newH, hwndVal_);
                }
            }
        }
        return hr;
    }

private:
    ipc::Response resp_;
    bool hasPreCaret_ = false;
    int preCaretX_ = 0;
    int preCaretY_ = 0;
    uint64_t hwndVal_ = 0;
    bool requireActiveFocus_ = false;
};

class CCaretQuerySession final : public CEditSessionBase {
public:
    CCaretQuerySession(CTextService* svc, ITfContext* ctx, RECT* rc, BOOL* found)
        : CEditSessionBase(svc, ctx), rc_(rc), found_(found) {}

    STDMETHODIMP DoEditSession(TfEditCookie ec) override {
        return svc_->queryCaretInSession(ec, ctx_, rc_, found_);
    }

private:
    RECT* rc_;
    BOOL* found_;
};

class CRefreshCaretSession final : public CEditSessionBase {
public:
    CRefreshCaretSession(CTextService* svc, ITfContext* ctx, uint64_t hwndVal)
        : CEditSessionBase(svc, ctx), hwndVal_(hwndVal) {}

    STDMETHODIMP DoEditSession(TfEditCookie ec) override {
        ScopedPerMonitorDpi dpiGuard;
        RECT rc{};
        BOOL found = FALSE;
        if (SUCCEEDED(svc_->queryCaretInSession(ec, ctx_, &rc, &found)) && found) {
            const int h = (rc.bottom > rc.top) ? static_cast<int>(rc.bottom - rc.top) : 20;
            svc_->sendCaretUpdate(static_cast<int>(rc.left), static_cast<int>(rc.bottom), h, hwndVal_);
        }
        return S_OK;
    }

private:
    uint64_t hwndVal_ = 0;
};

void setRangeDisplayAttr(TfEditCookie ec, ITfContext* ctx, ITfRange* range, TfGuidAtom atom) {
    if (atom == TF_INVALID_GUIDATOM || !ctx || !range) return;
    ITfProperty* pProp = nullptr;
    if (SUCCEEDED(ctx->GetProperty(GUID_PROP_ATTRIBUTE, &pProp)) && pProp) {
        VARIANT var;
        VariantInit(&var);
        var.vt = VT_I4;
        var.lVal = static_cast<LONG>(atom);
        pProp->SetValue(ec, range, &var);
        VariantClear(&var);
        pProp->Release();
    }
}

void clearRangeDisplayAttr(TfEditCookie ec, ITfContext* ctx, ITfRange* range) {
    if (!ctx || !range) return;
    ITfProperty* pProp = nullptr;
    if (SUCCEEDED(ctx->GetProperty(GUID_PROP_ATTRIBUTE, &pProp)) && pProp) {
        pProp->Clear(ec, range);
        pProp->Release();
    }
}

void setCaretAtRangeEnd(TfEditCookie ec, ITfContext* ctx, ITfRange* range) {
    if (!ctx || !range) return;
    ITfRange* endRange = nullptr;
    if (SUCCEEDED(range->Clone(&endRange)) && endRange) {
        endRange->Collapse(ec, TF_ANCHOR_END);
        TF_SELECTION sel{};
        sel.range = endRange;
        sel.style.ase = TF_AE_NONE;
        sel.style.fInterimChar = FALSE;
        ctx->SetSelection(ec, 1, &sel);
        endRange->Release();
    }
}

} // namespace

void CTextService::clearCompositionRef() {
    if (composition_) {
        composition_->Release();
        composition_ = nullptr;
    }
    isComposing_ = false;
}

HRESULT CTextService::queryCaretInSession(TfEditCookie ec, ITfContext* pContext, RECT* outRect, BOOL* outFound) {
    if (outFound) *outFound = FALSE;
    if (!pContext || !outRect) return E_INVALIDARG;

    ITfContextView* pView = nullptr;
    if (FAILED(pContext->GetActiveView(&pView)) || !pView) return E_FAIL;

    ITfRange* targetRange = nullptr;
    if (composition_) {
        composition_->GetRange(&targetRange);
    } else {
        TF_SELECTION sel{};
        ULONG fetched = 0;
        if (SUCCEEDED(pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched > 0) {
            targetRange = sel.range;
        }
    }

    HRESULT hr = E_FAIL;
    if (targetRange) {
        BOOL clipped = FALSE;
        RECT rc{};
        hr = pView->GetTextExt(ec, targetRange, &rc, &clipped);
        if (SUCCEEDED(hr) && (rc.left != 0 || rc.top != 0 || rc.right != 0 || rc.bottom != 0)) {
            *outRect = rc;
            if (outFound) *outFound = TRUE;
        }
        targetRange->Release();
    }

    pView->Release();
    return hr;
}

void CTextService::fillCaretPos(ITfContext* pic, ipc::Request& req) {
    req.hasCaret = 0;
    ScopedPerMonitorDpi dpiGuard;
    if (pic) {
        RECT rc{};
        BOOL found = FALSE;
        auto* sess = new CCaretQuerySession(this, pic, &rc, &found);
        HRESULT hrSession = S_OK;
        pic->RequestEditSession(clientId_, sess, TF_ES_SYNC | TF_ES_READ, &hrSession);
        sess->Release();
        if (found) {
            req.hasCaret = 1;
            req.caretX = rc.left;
            req.caretY = rc.bottom;
            req.caretHeight = (rc.bottom > rc.top) ? (rc.bottom - rc.top) : 20;
            return;
        }
    }

    // Fallback via GUI thread caret (for apps with incomplete GetTextExt)
    GUITHREADINFO gti{};
    gti.cbSize = sizeof(gti);
    if (GetGUIThreadInfo(0, &gti) && gti.hwndCaret) {
        POINT pt{gti.rcCaret.left, gti.rcCaret.bottom};
        ClientToScreen(gti.hwndCaret, &pt);
        req.hasCaret = 1;
        req.caretX = pt.x;
        req.caretY = pt.y;
        req.caretHeight = (gti.rcCaret.bottom > gti.rcCaret.top) ? (gti.rcCaret.bottom - gti.rcCaret.top) : 20;
        return;
    }

    // Fallback when neither GetTextExt nor Win32 caret is ready yet (e.g. XAML TextBox first key)
    if (!lastCaretValid_) {
        HWND targetWin = req.hwnd ? reinterpret_cast<HWND>(static_cast<uintptr_t>(req.hwnd)) : GetFocus();
        if (!targetWin || !IsWindow(targetWin)) targetWin = GetForegroundWindow();
        RECT wrc{};
        if (targetWin && IsWindow(targetWin) && GetWindowRect(targetWin, &wrc)) {
            req.hasCaret = 1;
            req.caretX = static_cast<int>(wrc.left) + 48;
            req.caretY = std::min<int>(static_cast<int>(wrc.top) + 84, static_cast<int>(wrc.bottom) - 16);
            req.caretHeight = 22;
        }
    }
}

HRESULT CTextService::applyResponseInSession(TfEditCookie ec, ITfContext* pContext, const ipc::Response& resp) {
    if (!pContext) return E_INVALIDARG;

    const std::string commitUtf8(resp.commit, std::min<size_t>(resp.commitLen, ipc::kMaxCommitBytes - 1));
    const std::string preeditUtf8(resp.preedit, std::min<size_t>(resp.preeditLen, ipc::kMaxPreeditBytes - 1));
    const std::wstring commitW = utf8_to_utf16(commitUtf8);
    const std::wstring preeditW = utf8_to_utf16(preeditUtf8);

    ignoreNextEndEdit_ = true;

    // 1. Commit text if present
    if (!commitW.empty()) {
        if (composition_) {
            ITfRange* pRange = nullptr;
            if (SUCCEEDED(composition_->GetRange(&pRange)) && pRange) {
                clearRangeDisplayAttr(ec, pContext, pRange);
                pRange->SetText(ec, 0, commitW.c_str(), static_cast<LONG>(commitW.size()));
                setCaretAtRangeEnd(ec, pContext, pRange);
                pRange->Release();
            }
            composition_->EndComposition(ec);
            clearCompositionRef();
        } else {
            ITfInsertAtSelection* pInsert = nullptr;
            if (SUCCEEDED(pContext->QueryInterface(IID_ITfInsertAtSelection, reinterpret_cast<void**>(&pInsert))) && pInsert) {
                ITfRange* pRange = nullptr;
                if (SUCCEEDED(pInsert->InsertTextAtSelection(ec, 0, commitW.c_str(),
                                                             static_cast<LONG>(commitW.size()), &pRange)) && pRange) {
                    setCaretAtRangeEnd(ec, pContext, pRange);
                    pRange->Release();
                }
                pInsert->Release();
            }
        }
    }

    // 2. Update or start inline preedit composition if present
    if (!preeditW.empty()) {
        if (!composition_) {
            ITfRange* pInsertRange = nullptr;
            ITfInsertAtSelection* pInsert = nullptr;
            if (SUCCEEDED(pContext->QueryInterface(IID_ITfInsertAtSelection, reinterpret_cast<void**>(&pInsert))) && pInsert) {
                pInsert->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, L"", 0, &pInsertRange);
                pInsert->Release();
            }
            if (!pInsertRange) {
                TF_SELECTION sel{};
                ULONG fetched = 0;
                if (SUCCEEDED(pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched > 0) {
                    pInsertRange = sel.range;
                }
            }
            if (pInsertRange) {
                ITfContextComposition* pCtxComp = nullptr;
                if (SUCCEEDED(pContext->QueryInterface(IID_ITfContextComposition, reinterpret_cast<void**>(&pCtxComp))) && pCtxComp) {
                    pCtxComp->StartComposition(ec, pInsertRange, static_cast<ITfCompositionSink*>(this), &composition_);
                    pCtxComp->Release();
                }
                pInsertRange->Release();
            }
        }
        if (composition_) {
            isComposing_ = true;
            ITfRange* pRange = nullptr;
            if (SUCCEEDED(composition_->GetRange(&pRange)) && pRange) {
                pRange->SetText(ec, 0, preeditW.c_str(), static_cast<LONG>(preeditW.size()));
                setRangeDisplayAttr(ec, pContext, pRange, displayAttrAtom_);
                setCaretAtRangeEnd(ec, pContext, pRange);
                pRange->Release();
            }
        }
    } else if (commitW.empty() && composition_) {
        // Composition cancelled/emptied (e.g. Backspace/Escape)
        ITfRange* pRange = nullptr;
        if (SUCCEEDED(composition_->GetRange(&pRange)) && pRange) {
            clearRangeDisplayAttr(ec, pContext, pRange);
            pRange->SetText(ec, 0, L"", 0);
            setCaretAtRangeEnd(ec, pContext, pRange);
            pRange->Release();
        }
        composition_->EndComposition(ec);
        clearCompositionRef();
    }

    ignoreNextEndEdit_ = false;
    return S_OK;
}

void CTextService::sendCaretUpdate(int caretX, int caretY, int caretH, uint64_t hwndVal) {
    lastCaretValid_ = true;
    lastCaretX_ = caretX;
    lastCaretY_ = caretY;
    lastCaretH_ = caretH;
    if (inProcCand_.isVisible()) {
        inProcCand_.moveCaret(caretX, caretY, caretH);
    }

    ipc::Request upd{};
    upd.type = ipc::MsgType::UpdateCaret;
    upd.hasCaret = 1;
    upd.caretX = caretX;
    upd.caretY = caretY;
    upd.caretHeight = caretH;
    upd.hwnd = hwndVal;
    ipc::Response updResp{};
    pipe_.sendRequest(upd, updResp);
}

STDMETHODIMP CTextService::OnLayoutChange(ITfContext* pic, TfLayoutCode lcode, ITfContextView*) {
    if (!isComposing_ || !pic || clientId_ == TF_CLIENTID_NULL) return S_OK;
    if (lcode != TF_LC_CREATE && lcode != TF_LC_CHANGE) return S_OK;
    auto* sess = new CRefreshCaretSession(this, pic, activeHwnd_);
    HRESULT hrSession = S_OK;
    pic->RequestEditSession(clientId_, sess, TF_ES_ASYNCDONTCARE | TF_ES_READ, &hrSession);
    sess->Release();
    return S_OK;
}

bool CTextService::shouldUseInProcessOverlay(uint64_t focusHwnd) const {
    auto isElevatedBand = [](HWND h) -> bool {
        if (!h || !IsWindow(h)) return false;
        HWND root = GetAncestor(h, GA_ROOT);
        if (!root || !IsWindow(root)) root = h;
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (!user32) return false;
        using FnGetWindowBand = BOOL(WINAPI*)(HWND, DWORD*);
        auto fn = reinterpret_cast<FnGetWindowBand>(
            reinterpret_cast<void*>(GetProcAddress(user32, "GetWindowBand")));
        if (!fn) return false;
        DWORD band = 0;
        return fn(root, &band) && band > 1; // 1 == ZBID_DESKTOP
    };

    if (focusHwnd != 0 && isElevatedBand(reinterpret_cast<HWND>(static_cast<uintptr_t>(focusHwnd)))) {
        return true;
    }
    HWND fg = GetForegroundWindow();
    DWORD fgPid = 0;
    if (fg && GetWindowThreadProcessId(fg, &fgPid) && fgPid == GetCurrentProcessId() && isElevatedBand(fg)) {
        return true;
    }

    wchar_t exePath[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (n > 0) {
        const wchar_t* base = exePath;
        for (DWORD i = 0; i < n; ++i) {
            if (exePath[i] == L'\\' || exePath[i] == L'/') base = exePath + i + 1;
        }
        static const wchar_t* const kElevatedShellHosts[] = {
            L"SearchHost.exe",
            L"SearchApp.exe",
            L"SearchUI.exe",
            L"StartMenuExperienceHost.exe",
            L"ShellExperienceHost.exe",
            L"LockApp.exe",
        };
        for (const wchar_t* name : kElevatedShellHosts) {
            if (_wcsicmp(base, name) == 0) return true;
        }
    }
    return false;
}

void CTextService::syncOverlayFromResponse(const ipc::Response& resp,
                                           int caretX, int caretY, int caretH,
                                           uint64_t hwndVal, bool inProcUi) {
    if (!inProcUi || resp.showCandidates == 0 || resp.candCount == 0) {
        inProcCand_.hide();
        return;
    }
    inProcCand_.update(
        resp, caretX, caretY, caretH, hwndVal,
        [this](int idx) { handleOverlaySelect(idx); },
        [this](bool next) { handleOverlayPage(next); });
}

void CTextService::handleOverlaySelect(int slotIdx) {
    if (!threadMgr_) return;
    ipc::Request req{};
    req.type = ipc::MsgType::SelectCandidate;
    req.vk = static_cast<uint32_t>(slotIdx);
    req.hwnd = activeHwnd_ ? activeHwnd_ : reinterpret_cast<uint64_t>(GetFocus());
    req.reserved |= ipc::ReqFlagInProcUi;

    ipc::Response resp{};
    if (!pipe_.sendRequest(req, resp)) return;

    syncOverlayFromResponse(resp, lastCaretX_, lastCaretY_, lastCaretH_, req.hwnd, true);
    ITfDocumentMgr* pdim = nullptr;
    ITfContext* pic = nullptr;
    if (SUCCEEDED(threadMgr_->GetFocus(&pdim)) && pdim) {
        pdim->GetTop(&pic);
        pdim->Release();
    }
    if (pic) {
        if (resp.consumed || (isComposing_ && !resp.isComposing)) {
            applyResponseAsync(pic, resp, lastCaretValid_, lastCaretX_, lastCaretY_, req.hwnd, true);
        }
        pic->Release();
    }
}

void CTextService::handleOverlayPage(bool isNext) {
    if (!threadMgr_) return;
    ipc::Request req{};
    req.type = isNext ? ipc::MsgType::NextPage : ipc::MsgType::PrevPage;
    req.hwnd = activeHwnd_ ? activeHwnd_ : reinterpret_cast<uint64_t>(GetFocus());
    req.reserved |= ipc::ReqFlagInProcUi;

    ipc::Response resp{};
    if (!pipe_.sendRequest(req, resp)) return;

    syncOverlayFromResponse(resp, lastCaretX_, lastCaretY_, lastCaretH_, req.hwnd, true);
    ITfDocumentMgr* pdim = nullptr;
    ITfContext* pic = nullptr;
    if (SUCCEEDED(threadMgr_->GetFocus(&pdim)) && pdim) {
        pdim->GetTop(&pic);
        pdim->Release();
    }
    if (pic) {
        if (resp.consumed && resp.preeditLen > 0) {
            applyResponseAsync(pic, resp, lastCaretValid_, lastCaretX_, lastCaretY_, req.hwnd, true);
        }
        pic->Release();
    }
}

void CTextService::applyResponseAsync(ITfContext* pic, const ipc::Response& resp,
                                      bool hasPreCaret, int preCaretX, int preCaretY, uint64_t hwndVal,
                                      bool requireActiveFocus) {
    if (!pic || clientId_ == TF_CLIENTID_NULL) return;
    const bool hasTextChange = (resp.commitLen > 0 || resp.preeditLen > 0 || isComposing_);
    if (!hasTextChange) return;

    auto* sess = new CApplyResponseSession(this, pic, resp, hasPreCaret, preCaretX, preCaretY, hwndVal, requireActiveFocus);
    HRESULT hrSession = S_OK;
    const HRESULT hrReq = pic->RequestEditSession(
        clientId_, sess, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hrSession);
    if (FAILED(hrReq) || (hrReq == S_OK && FAILED(hrSession))) {
        clearCompositionRef();
    }
    sess->Release();
}

HRESULT CTextService::handleKeyInternal(ITfContext* pic, WPARAM wParam, LPARAM lParam,
                                        bool isKeyUp, bool testOnly, BOOL* pfEaten) {
    if (pfEaten) *pfEaten = FALSE;

    ipc::Request req{};
    req.type = testOnly ? ipc::MsgType::KeyTest : ipc::MsgType::KeyEvent;
    req.vk = static_cast<uint32_t>(wParam);
    req.scanCode = static_cast<uint32_t>((lParam >> 16) & 0xFF);
    req.modifiers = currentModifiers();
    req.isKeyUp = isKeyUp ? 1 : 0;
    req.hwnd = reinterpret_cast<uint64_t>(GetFocus());
    const bool useInProc = !testOnly && shouldUseInProcessOverlay(req.hwnd);
    if (useInProc) {
        req.reserved |= ipc::ReqFlagInProcUi;
    }

    if (!testOnly && req.hwnd != 0) {
        if (activeHwnd_ != 0 && activeHwnd_ != req.hwnd) {
            lastCaretValid_ = false;
            inProcCand_.hide();
            std::lock_guard<std::mutex> lock(notifyMutex_);
            pendingNotifies_.clear();
        }
        activeHwnd_ = req.hwnd;
        pipe_.setNotifyHwnd(req.hwnd);
    }

    if (!testOnly && !isKeyUp) {
        fillCaretPos(pic, req);
        if (req.hasCaret != 0) {
            lastCaretValid_ = true;
            lastCaretX_ = req.caretX;
            lastCaretY_ = req.caretY;
            lastCaretH_ = req.caretHeight;
        }
    }

    ipc::Response resp{};
    if (!pipe_.sendRequest(req, resp)) {
        return S_OK; // Fail open cleanly if host is unavailable
    }

    if (pfEaten) {
        *pfEaten = resp.consumed ? TRUE : FALSE;
    }

    if (!testOnly) {
        const int cx = (req.hasCaret != 0) ? req.caretX : lastCaretX_;
        const int cy = (req.hasCaret != 0) ? req.caretY : lastCaretY_;
        const int ch = (req.hasCaret != 0) ? req.caretHeight : lastCaretH_;
        syncOverlayFromResponse(resp, cx, cy, ch, req.hwnd, useInProc);
    }

    // Apply edits when key was consumed OR when host silently left composing state while TSF still shows inline preedit.
    if (!testOnly && pic && (resp.consumed || (isComposing_ && !resp.isComposing))) {
        applyResponseAsync(pic, resp, req.hasCaret != 0, req.caretX, req.caretY, req.hwnd);
    }
    return S_OK;
}

} // namespace liteime::tsf
