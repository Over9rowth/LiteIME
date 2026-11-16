#pragma once

#include "tsf/tsf_compat.hpp"
#include "tsf/pipe_client.hpp"
#include "tsf/inproc_candidate_window.hpp"
#include "common/ipc_protocol.hpp"

#include <mutex>
#include <string>
#include <vector>

namespace liteime::tsf {

class CTextService final : public ITfTextInputProcessorEx,
                           public ITfThreadMgrEventSink,
                           public ITfTextEditSink,
                           public ITfTextLayoutSink,
                           public ITfKeyEventSink,
                           public ITfCompositionSink,
                           public ITfDisplayAttributeProvider {
public:
    CTextService();
    ~CTextService();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfTextInputProcessor / ITfTextInputProcessorEx
    STDMETHODIMP Activate(ITfThreadMgr* ptim, TfClientId tid) override;
    STDMETHODIMP Deactivate() override;
    STDMETHODIMP ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) override;

    // ITfThreadMgrEventSink
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* pdim) override;
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* pdim) override;
    STDMETHODIMP OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr* pdimPrevFocus) override;
    STDMETHODIMP OnPushContext(ITfContext* pic) override;
    STDMETHODIMP OnPopContext(ITfContext* pic) override;

    // ITfTextEditSink
    STDMETHODIMP OnEndEdit(ITfContext* pic, TfEditCookie ecReadOnly, ITfEditRecord* pEditRecord) override;

    // ITfTextLayoutSink
    STDMETHODIMP OnLayoutChange(ITfContext* pic, TfLayoutCode lcode, ITfContextView* pView) override;

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL fForeground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) override;

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition* pComposition) override;

    // ITfDisplayAttributeProvider
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) override;
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) override;

    // Composition helpers used by edit sessions
    HRESULT applyResponseInSession(TfEditCookie ec, ITfContext* pContext, const ipc::Response& resp);
    HRESULT queryCaretInSession(TfEditCookie ec, ITfContext* pContext, RECT* outRect, BOOL* outFound);
    bool isContextAndHwndFocused(ITfContext* pContext, uint64_t expectedHwnd) const;
    void sendCaretUpdate(int caretX, int caretY, int caretH, uint64_t hwndVal);
    uint64_t currentActiveHwnd() const { return activeHwnd_; }
    void clearCompositionRef();

private:
    bool initThreadMgrSink();
    void uninitThreadMgrSink();
    bool initKeyEventSink();
    void uninitKeyEventSink();
    void initNotifyWindow();
    void uninitNotifyWindow();
    void enqueueNotify(const ipc::NotifyPacket& pkt);
    void drainNotifyQueue();
    static LRESULT CALLBACK notifyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void updateTextEditSink(ITfDocumentMgr* pdim);
    void initDisplayAttributeAtom();

    void fillCaretPos(ITfContext* pic, ipc::Request& req);
    bool shouldUseInProcessOverlay(uint64_t focusHwnd) const;
    void syncOverlayFromResponse(const ipc::Response& resp, int caretX, int caretY, int caretH, uint64_t hwndVal, bool inProcUi);
    void handleOverlaySelect(int slotIdx);
    void handleOverlayPage(bool isNext);
    void applyResponseAsync(ITfContext* pic, const ipc::Response& resp,
                            bool hasPreCaret, int preCaretX, int preCaretY, uint64_t hwndVal,
                            bool requireActiveFocus = false);
    uint32_t currentModifiers() const;
    HRESULT handleKeyInternal(ITfContext* pic, WPARAM wParam, LPARAM lParam, bool isKeyUp, bool testOnly, BOOL* pfEaten);

    LONG refCount_ = 1;
    ITfThreadMgr* threadMgr_ = nullptr;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    DWORD threadMgrCookie_ = TF_INVALID_COOKIE;
    DWORD textEditCookie_ = TF_INVALID_COOKIE;
    DWORD textLayoutCookie_ = TF_INVALID_COOKIE;
    ITfContext* textEditContext_ = nullptr;
    ITfComposition* composition_ = nullptr;
    TfGuidAtom displayAttrAtom_ = TF_INVALID_GUIDATOM;
    bool isComposing_ = false;
    bool ignoreNextEndEdit_ = false;
    bool lastCaretValid_ = false;
    int lastCaretX_ = 300;
    int lastCaretY_ = 300;
    int lastCaretH_ = 22;
    uint64_t activeHwnd_ = 0;

    HWND notifyHwnd_ = nullptr;
    std::mutex notifyMutex_;
    std::vector<ipc::NotifyPacket> pendingNotifies_;

    InProcCandidateWindow inProcCand_;
    PipeClient pipe_;
};

} // namespace liteime::tsf
