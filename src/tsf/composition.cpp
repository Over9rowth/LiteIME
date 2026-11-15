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
                          RECT* postRc = nullptr, BOOL* postFound = nullptr)
        : CEditSessionBase(svc, ctx), resp_(resp), postRc_(postRc), postFound_(postFound) {}

    STDMETHODIMP DoEditSession(TfEditCookie ec) override {
        HRESULT hr = svc_->applyResponseInSession(ec, ctx_, resp_);
        if (SUCCEEDED(hr) && resp_.preeditLen > 0 && postRc_ && postFound_) {
            svc_->queryCaretInSession(ec, ctx_, postRc_, postFound_);
        }
        return hr;
    }

private:
    ipc::Response resp_;
    RECT* postRc_;
    BOOL* postFound_;
};

class ScopedPerMonitorDpi {
public:
    ScopedPerMonitorDpi() {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (!user32) return;
        using FnSetThreadDpi = HANDLE(WINAPI*)(HANDLE);
        setFn_ = reinterpret_cast<FnSetThreadDpi>(
            reinterpret_cast<void*>(GetProcAddress(user32, "SetThreadDpiAwarenessContext")));
        if (setFn_) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == ((DPI_AWARENESS_CONTEXT)-4)
            prev_ = setFn_(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4)));
        }
    }
    ~ScopedPerMonitorDpi() {
        if (setFn_ && prev_) {
            setFn_(prev_);
        }
    }

private:
    HANDLE(WINAPI* setFn_)(HANDLE) = nullptr;
    HANDLE prev_ = nullptr;
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

    if (!testOnly && !isKeyUp) {
        fillCaretPos(pic, req);
    }

    ipc::Response resp{};
    if (!pipe_.sendRequest(req, resp)) {
        return S_OK; // Fail open cleanly if host is unavailable
    }

    if (pfEaten) {
        *pfEaten = resp.consumed ? TRUE : FALSE;
    }

    if (!testOnly && resp.consumed && pic) {
        const bool hasTextChange = (resp.commitLen > 0 || resp.preeditLen > 0 || isComposing_);
        if (hasTextChange) {
            RECT postRc{};
            BOOL postFound = FALSE;
            {
                ScopedPerMonitorDpi dpiGuard;
                auto* sess = new CApplyResponseSession(this, pic, resp, &postRc, &postFound);
                HRESULT hrSession = S_OK;
                pic->RequestEditSession(clientId_, sess, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hrSession);
                sess->Release();
            }
            if (postFound && resp.preeditLen > 0) {
                int newX = static_cast<int>(postRc.left);
                int newY = static_cast<int>(postRc.bottom);
                int newH = (postRc.bottom > postRc.top) ? static_cast<int>(postRc.bottom - postRc.top) : 20;
                if (!req.hasCaret || std::abs(newX - req.caretX) > 2 || std::abs(newY - req.caretY) > 2) {
                    ipc::Request upd{};
                    upd.type = ipc::MsgType::UpdateCaret;
                    upd.hasCaret = 1;
                    upd.caretX = newX;
                    upd.caretY = newY;
                    upd.caretHeight = newH;
                    upd.hwnd = req.hwnd;
                    ipc::Response updResp{};
                    pipe_.sendRequest(upd, updResp);
                }
            }
        }
    }
    return S_OK;
}

} // namespace liteime::tsf
