#pragma once

#include "tsf/tsf_compat.hpp"

namespace liteime::tsf {

class CDisplayAttributeInfo final : public ITfDisplayAttributeInfo {
public:
    CDisplayAttributeInfo();
    ~CDisplayAttributeInfo();

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP GetGUID(GUID* pguid) override;
    STDMETHODIMP GetDescription(BSTR* pbstrDesc) override;
    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) override;
    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE* pda) override;
    STDMETHODIMP Reset() override;

private:
    LONG refCount_ = 1;
    TF_DISPLAYATTRIBUTE attr_{};
};

class CEnumDisplayAttributeInfo final : public IEnumTfDisplayAttributeInfo {
public:
    CEnumDisplayAttributeInfo();
    ~CEnumDisplayAttributeInfo();

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** ppEnum) override;
    STDMETHODIMP Next(ULONG ulCount, ITfDisplayAttributeInfo** rgInfo, ULONG* pcFetched) override;
    STDMETHODIMP Reset() override;
    STDMETHODIMP Skip(ULONG ulCount) override;

private:
    LONG refCount_ = 1;
    ULONG index_ = 0;
};

} // namespace liteime::tsf
