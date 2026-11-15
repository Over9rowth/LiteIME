#include "tsf/tsf_compat.hpp"

class CClassFactory final : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        *ppvObj = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *ppvObj = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        DllAddRef();
        return 1;
    }

    STDMETHODIMP_(ULONG) Release() override {
        DllRelease();
        return 1;
    }

    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        *ppvObj = nullptr;
        if (pUnkOuter != nullptr) return CLASS_E_NOAGGREGATION;
        return liteime::tsf::CreateTextServiceInstance(pUnkOuter, riid, ppvObj);
    }

    STDMETHODIMP LockServer(BOOL fLock) override {
        if (fLock) DllAddRef();
        else DllRelease();
        return S_OK;
    }
};

static CClassFactory g_classFactory;

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppvObj) {
    if (!ppvObj) return E_INVALIDARG;
    *ppvObj = nullptr;
    if (!IsEqualCLSID(rclsid, CLSID_LiteIME)) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    return g_classFactory.QueryInterface(riid, ppvObj);
}
