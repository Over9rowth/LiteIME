#include "tsf/text_service.hpp"
#include "tsf/display_attribute.hpp"

namespace liteime::tsf {

namespace {
constexpr const wchar_t* kTsfNotifyClass = L"LiteIME_TsfNotifyWindow";
constexpr UINT kWmApplyNotify = WM_APP + 201;
} // namespace

CTextService::CTextService() {
    DllAddRef();
}

CTextService::~CTextService() {
    pipe_.stopNotify();
    uninitNotifyWindow();
    clearCompositionRef();
    uninitKeyEventSink();
    uninitThreadMgrSink();
    updateTextEditSink(nullptr);
    if (threadMgr_) {
        threadMgr_->Release();
        threadMgr_ = nullptr;
    }
    DllRelease();
}

STDMETHODIMP CTextService::QueryInterface(REFIID riid, void** ppvObj) {
    if (!ppvObj) return E_INVALIDARG;
    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfTextInputProcessor) ||
        IsEqualIID(riid, IID_ITfTextInputProcessorEx)) {
        *ppvObj = static_cast<ITfTextInputProcessorEx*>(this);
    } else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink)) {
        *ppvObj = static_cast<ITfThreadMgrEventSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfTextEditSink)) {
        *ppvObj = static_cast<ITfTextEditSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfKeyEventSink)) {
        *ppvObj = static_cast<ITfKeyEventSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfCompositionSink)) {
        *ppvObj = static_cast<ITfCompositionSink*>(this);
    } else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider)) {
        *ppvObj = static_cast<ITfDisplayAttributeProvider*>(this);
    } else {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CTextService::AddRef() {
    return InterlockedIncrement(&refCount_);
}

STDMETHODIMP_(ULONG) CTextService::Release() {
    LONG c = InterlockedDecrement(&refCount_);
    if (c == 0) delete this;
    return c;
}

STDMETHODIMP CTextService::Activate(ITfThreadMgr* ptim, TfClientId tid) {
    return ActivateEx(ptim, tid, 0);
}

STDMETHODIMP CTextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD) {
    if (!ptim) return E_INVALIDARG;

    threadMgr_ = ptim;
    threadMgr_->AddRef();
    clientId_ = tid;

    initDisplayAttributeAtom();

    if (!initThreadMgrSink() || !initKeyEventSink()) {
        Deactivate();
        return E_FAIL;
    }

    ITfDocumentMgr* pdim = nullptr;
    const bool hasFocus = SUCCEEDED(threadMgr_->GetFocus(&pdim)) && pdim;
    if (pdim) {
        updateTextEditSink(pdim);
        pdim->Release();
    }

    // Wake up host process proactively; only claim focus if this thread actually holds focus
    ipc::Request req{};
    req.type = hasFocus ? ipc::MsgType::FocusChanged : ipc::MsgType::Ping;
    req.focusGained = hasFocus ? 1 : 0;
    req.hwnd = reinterpret_cast<uint64_t>(GetFocus());
    if (hasFocus && req.hwnd != 0) activeHwnd_ = req.hwnd;
    ipc::Response resp{};
    pipe_.sendRequest(req, resp);

    initNotifyWindow();
    pipe_.startNotify([this](const ipc::NotifyPacket& pkt) {
        enqueueNotify(pkt);
    });

    return S_OK;
}

STDMETHODIMP CTextService::Deactivate() {
    pipe_.stopNotify();
    uninitNotifyWindow();

    ipc::Request req{};
    req.type = ipc::MsgType::FocusChanged;
    req.focusGained = 0;
    req.hwnd = reinterpret_cast<uint64_t>(GetFocus());
    ipc::Response resp{};
    pipe_.sendRequest(req, resp);

    activeHwnd_ = 0;
    clearCompositionRef();
    updateTextEditSink(nullptr);
    uninitKeyEventSink();
    uninitThreadMgrSink();

    if (threadMgr_) {
        threadMgr_->Release();
        threadMgr_ = nullptr;
    }
    clientId_ = TF_CLIENTID_NULL;
    return S_OK;
}

void CTextService::initNotifyWindow() {
    if (notifyHwnd_) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = notifyWndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = kTsfNotifyClass;
    RegisterClassExW(&wc);

    notifyHwnd_ = CreateWindowExW(
        0, kTsfNotifyClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_hInst, this);
}

void CTextService::uninitNotifyWindow() {
    if (notifyHwnd_) {
        DestroyWindow(notifyHwnd_);
        notifyHwnd_ = nullptr;
    }
    std::lock_guard<std::mutex> lock(notifyMutex_);
    pendingNotifies_.clear();
}

void CTextService::enqueueNotify(const ipc::NotifyPacket& pkt) {
    HWND dst = notifyHwnd_;
    if (!dst) return;
    {
        std::lock_guard<std::mutex> lock(notifyMutex_);
        if (pendingNotifies_.size() < 32) {
            pendingNotifies_.push_back(pkt);
        }
    }
    PostMessageW(dst, kWmApplyNotify, 0, 0);
}

void CTextService::drainNotifyQueue() {
    std::vector<ipc::NotifyPacket> batch;
    {
        std::lock_guard<std::mutex> lock(notifyMutex_);
        batch.swap(pendingNotifies_);
    }
    if (batch.empty() || !threadMgr_) return;

    for (const auto& pkt : batch) {
        if (pkt.targetHwnd != 0 && activeHwnd_ != 0 && pkt.targetHwnd != activeHwnd_ && !isComposing_) {
            continue;
        }
        const ipc::Response& resp = pkt.response;
        const bool hasTextChange = (resp.commitLen > 0 || resp.preeditLen > 0 || isComposing_);
        if (!hasTextChange) continue;

        ITfContext* pic = textEditContext_;
        if (pic) {
            pic->AddRef();
        } else {
            ITfDocumentMgr* pdim = nullptr;
            if (SUCCEEDED(threadMgr_->GetFocus(&pdim)) && pdim) {
                pdim->GetTop(&pic);
                pdim->Release();
            }
        }
        if (pic) {
            applyResponseAsync(pic, resp, false, 0, 0, activeHwnd_);
            pic->Release();
        }
    }
}

LRESULT CALLBACK CTextService::notifyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    auto* self = reinterpret_cast<CTextService*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self && msg == kWmApplyNotify) {
        self->drainNotifyQueue();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void CTextService::initDisplayAttributeAtom() {
    ITfCategoryMgr* pCatMgr = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfCategoryMgr, reinterpret_cast<void**>(&pCatMgr))) && pCatMgr) {
        pCatMgr->RegisterGUID(GUID_LiteIMEDisplayAttrInput, &displayAttrAtom_);
        pCatMgr->Release();
    }
}

bool CTextService::initThreadMgrSink() {
    ITfSource* pSource = nullptr;
    if (FAILED(threadMgr_->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&pSource))) || !pSource) {
        return false;
    }
    HRESULT hr = pSource->AdviseSink(IID_ITfThreadMgrEventSink,
                                     static_cast<ITfThreadMgrEventSink*>(this),
                                     &threadMgrCookie_);
    pSource->Release();
    return SUCCEEDED(hr);
}

void CTextService::uninitThreadMgrSink() {
    if (!threadMgr_ || threadMgrCookie_ == TF_INVALID_COOKIE) return;
    ITfSource* pSource = nullptr;
    if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&pSource))) && pSource) {
        pSource->UnadviseSink(threadMgrCookie_);
        pSource->Release();
    }
    threadMgrCookie_ = TF_INVALID_COOKIE;
}

bool CTextService::initKeyEventSink() {
    ITfKeystrokeMgr* pKeyMgr = nullptr;
    if (FAILED(threadMgr_->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&pKeyMgr))) || !pKeyMgr) {
        return false;
    }
    HRESULT hr = pKeyMgr->AdviseKeyEventSink(clientId_, static_cast<ITfKeyEventSink*>(this), TRUE);
    pKeyMgr->Release();
    return SUCCEEDED(hr);
}

void CTextService::uninitKeyEventSink() {
    if (!threadMgr_ || clientId_ == TF_CLIENTID_NULL) return;
    ITfKeystrokeMgr* pKeyMgr = nullptr;
    if (SUCCEEDED(threadMgr_->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&pKeyMgr))) && pKeyMgr) {
        pKeyMgr->UnadviseKeyEventSink(clientId_);
        pKeyMgr->Release();
    }
}

void CTextService::updateTextEditSink(ITfDocumentMgr* pdim) {
    if (textEditContext_) {
        if (textEditCookie_ != TF_INVALID_COOKIE) {
            ITfSource* pSource = nullptr;
            if (SUCCEEDED(textEditContext_->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&pSource))) && pSource) {
                pSource->UnadviseSink(textEditCookie_);
                pSource->Release();
            }
            textEditCookie_ = TF_INVALID_COOKIE;
        }
        textEditContext_->Release();
        textEditContext_ = nullptr;
    }

    if (!pdim) return;
    ITfContext* pCtx = nullptr;
    if (SUCCEEDED(pdim->GetTop(&pCtx)) && pCtx) {
        ITfSource* pSource = nullptr;
        if (SUCCEEDED(pCtx->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&pSource))) && pSource) {
            if (SUCCEEDED(pSource->AdviseSink(IID_ITfTextEditSink, static_cast<ITfTextEditSink*>(this), &textEditCookie_))) {
                textEditContext_ = pCtx;
                textEditContext_->AddRef();
            }
            pSource->Release();
        }
        pCtx->Release();
    }
}

STDMETHODIMP CTextService::OnInitDocumentMgr(ITfDocumentMgr*) { return S_OK; }
STDMETHODIMP CTextService::OnUninitDocumentMgr(ITfDocumentMgr*) { return S_OK; }
STDMETHODIMP CTextService::OnPushContext(ITfContext*) { return S_OK; }
STDMETHODIMP CTextService::OnPopContext(ITfContext*) { return S_OK; }

STDMETHODIMP CTextService::OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr*) {
    updateTextEditSink(pdimFocus);
    ipc::Request req{};
    req.type = ipc::MsgType::FocusChanged;
    req.focusGained = (pdimFocus != nullptr) ? 1 : 0;
    req.hwnd = reinterpret_cast<uint64_t>(GetFocus());
    ipc::Response resp{};
    pipe_.sendRequest(req, resp);
    if (!pdimFocus) clearCompositionRef();
    return S_OK;
}

STDMETHODIMP CTextService::OnEndEdit(ITfContext*, TfEditCookie, ITfEditRecord*) {
    // Composition cancellation by external clicks is already notified via
    // ITfCompositionSink::OnCompositionTerminated and OnSetFocus.
    // Do NOT reset the host engine here on !isComposing_, otherwise async
    // selection updates from Chromium/Electron/Win11 Notepad kill the
    // candidate window immediately after KeyDown.
    return S_OK;
}

STDMETHODIMP CTextService::OnSetFocus(BOOL fForeground) {
    ipc::Request req{};
    req.type = ipc::MsgType::FocusChanged;
    req.focusGained = fForeground ? 1 : 0;
    req.hwnd = reinterpret_cast<uint64_t>(GetFocus());
    ipc::Response resp{};
    pipe_.sendRequest(req, resp);
    if (!fForeground) clearCompositionRef();
    return S_OK;
}

uint32_t CTextService::currentModifiers() const {
    uint32_t m = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000)   m |= ipc::ModShift;
    if (GetKeyState(VK_CONTROL) & 0x8000) m |= ipc::ModCtrl;
    if (GetKeyState(VK_MENU) & 0x8000)    m |= ipc::ModAlt;
    if (GetKeyState(VK_CAPITAL) & 0x0001) m |= ipc::ModCaps;
    return m;
}

STDMETHODIMP CTextService::OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT) {
        BOOL dummy = FALSE;
        handleKeyInternal(pic, wParam, lParam, false, false, &dummy);
        if (pfEaten) *pfEaten = FALSE;
        return S_OK;
    }
    return handleKeyInternal(pic, wParam, lParam, false, true, pfEaten);
}

STDMETHODIMP CTextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    return handleKeyInternal(pic, wParam, lParam, false, false, pfEaten);
}

STDMETHODIMP CTextService::OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT) {
        BOOL dummy = FALSE;
        handleKeyInternal(pic, wParam, lParam, true, false, &dummy);
        if (pfEaten) *pfEaten = FALSE;
        return S_OK;
    }
    return handleKeyInternal(pic, wParam, lParam, true, true, pfEaten);
}

STDMETHODIMP CTextService::OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    return handleKeyInternal(pic, wParam, lParam, true, false, pfEaten);
}

STDMETHODIMP CTextService::OnPreservedKey(ITfContext*, REFGUID, BOOL* pfEaten) {
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP CTextService::OnCompositionTerminated(TfEditCookie, ITfComposition*) {
    clearCompositionRef();
    ipc::Request req{};
    req.type = ipc::MsgType::ResetComposition;
    ipc::Response resp{};
    pipe_.sendRequest(req, resp);
    return S_OK;
}

STDMETHODIMP CTextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) {
    if (!ppEnum) return E_INVALIDARG;
    *ppEnum = new CEnumDisplayAttributeInfo();
    return S_OK;
}

STDMETHODIMP CTextService::GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) {
    if (!ppInfo) return E_INVALIDARG;
    *ppInfo = nullptr;
    if (IsEqualGUID(guid, GUID_LiteIMEDisplayAttrInput)) {
        *ppInfo = new CDisplayAttributeInfo();
        return S_OK;
    }
    return E_INVALIDARG;
}

HRESULT CreateTextServiceInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObj) {
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;
    auto* svc = new CTextService();
    HRESULT hr = svc->QueryInterface(riid, ppvObj);
    svc->Release();
    return hr;
}

} // namespace liteime::tsf
