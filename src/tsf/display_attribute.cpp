#include "tsf/display_attribute.hpp"
#include <atomic>

namespace liteime::tsf {

namespace {
TF_DISPLAYATTRIBUTE makeDefaultInputAttr() {
    TF_DISPLAYATTRIBUTE da{};
    da.crText.type = TF_CT_NONE;
    da.crBk.type = TF_CT_NONE;
    da.lsStyle = TF_LS_DOT;
    da.fBoldLine = FALSE;
    da.crLine.type = TF_CT_NONE;
    da.bAttr = TF_ATTR_INPUT;
    return da;
}
} // namespace

CDisplayAttributeInfo::CDisplayAttributeInfo() : attr_(makeDefaultInputAttr()) {
    DllAddRef();
}

CDisplayAttributeInfo::~CDisplayAttributeInfo() {
    DllRelease();
}

STDMETHODIMP CDisplayAttributeInfo::QueryInterface(REFIID riid, void** ppvObj) {
    if (!ppvObj) return E_INVALIDARG;
    *ppvObj = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfDisplayAttributeInfo)) {
        *ppvObj = static_cast<ITfDisplayAttributeInfo*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CDisplayAttributeInfo::AddRef() {
    return InterlockedIncrement(&refCount_);
}

STDMETHODIMP_(ULONG) CDisplayAttributeInfo::Release() {
    LONG c = InterlockedDecrement(&refCount_);
    if (c == 0) delete this;
    return c;
}

STDMETHODIMP CDisplayAttributeInfo::GetGUID(GUID* pguid) {
    if (!pguid) return E_INVALIDARG;
    *pguid = GUID_LiteIMEDisplayAttrInput;
    return S_OK;
}

STDMETHODIMP CDisplayAttributeInfo::GetDescription(BSTR* pbstrDesc) {
    if (!pbstrDesc) return E_INVALIDARG;
    *pbstrDesc = SysAllocString(L"LiteIME Input");
    return *pbstrDesc ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CDisplayAttributeInfo::GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) {
    if (!pda) return E_INVALIDARG;
    *pda = attr_;
    return S_OK;
}

STDMETHODIMP CDisplayAttributeInfo::SetAttributeInfo(const TF_DISPLAYATTRIBUTE* pda) {
    if (!pda) return E_INVALIDARG;
    attr_ = *pda;
    return S_OK;
}

STDMETHODIMP CDisplayAttributeInfo::Reset() {
    attr_ = makeDefaultInputAttr();
    return S_OK;
}

CEnumDisplayAttributeInfo::CEnumDisplayAttributeInfo() {
    DllAddRef();
}

CEnumDisplayAttributeInfo::~CEnumDisplayAttributeInfo() {
    DllRelease();
}

STDMETHODIMP CEnumDisplayAttributeInfo::QueryInterface(REFIID riid, void** ppvObj) {
    if (!ppvObj) return E_INVALIDARG;
    *ppvObj = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumTfDisplayAttributeInfo)) {
        *ppvObj = static_cast<IEnumTfDisplayAttributeInfo*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CEnumDisplayAttributeInfo::AddRef() {
    return InterlockedIncrement(&refCount_);
}

STDMETHODIMP_(ULONG) CEnumDisplayAttributeInfo::Release() {
    LONG c = InterlockedDecrement(&refCount_);
    if (c == 0) delete this;
    return c;
}

STDMETHODIMP CEnumDisplayAttributeInfo::Clone(IEnumTfDisplayAttributeInfo** ppEnum) {
    if (!ppEnum) return E_INVALIDARG;
    auto* clone = new CEnumDisplayAttributeInfo();
    clone->index_ = index_;
    *ppEnum = clone;
    return S_OK;
}

STDMETHODIMP CEnumDisplayAttributeInfo::Next(ULONG ulCount, ITfDisplayAttributeInfo** rgInfo, ULONG* pcFetched) {
    if (pcFetched) *pcFetched = 0;
    if (ulCount == 0) return S_OK;
    if (!rgInfo) return E_INVALIDARG;

    if (index_ == 0) {
        rgInfo[0] = new CDisplayAttributeInfo();
        index_ = 1;
        if (pcFetched) *pcFetched = 1;
        return (ulCount == 1) ? S_OK : S_FALSE;
    }
    return S_FALSE;
}

STDMETHODIMP CEnumDisplayAttributeInfo::Reset() {
    index_ = 0;
    return S_OK;
}

STDMETHODIMP CEnumDisplayAttributeInfo::Skip(ULONG ulCount) {
    index_ += ulCount;
    return (index_ <= 1) ? S_OK : S_FALSE;
}

} // namespace liteime::tsf
