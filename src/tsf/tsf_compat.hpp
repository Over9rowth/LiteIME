#pragma once

#include <windows.h>
#include <msctf.h>
#include <guiddef.h>

// LiteIME CLSID, Profile GUID, DisplayAttribute GUID
EXTERN_C const CLSID CLSID_LiteIME;
EXTERN_C const GUID GUID_LiteIMEProfile;
EXTERN_C const GUID GUID_LiteIMEDisplayAttrInput;

// SDK GUIDs missing from MinGW libuuid.a
EXTERN_C const GUID GUID_TFCAT_TIPCAP_SECUREMODE;
EXTERN_C const GUID GUID_TFCAT_TIPCAP_UIELEMENTENABLED;
EXTERN_C const GUID GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT;
EXTERN_C const GUID GUID_TFCAT_TIPCAP_COMLESS;
EXTERN_C const GUID GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT;
EXTERN_C const GUID GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT;
EXTERN_C const GUID GUID_TFCAT_TIPCAP_TSF3;

EXTERN_C const IID IID_ITfTextInputProcessorEx;
EXTERN_C const IID IID_ITfDisplayAttributeProvider;
EXTERN_C const IID IID_ITfTextLayoutSink;

#ifndef __ITfTextLayoutSink_INTERFACE_DEFINED__
#define __ITfTextLayoutSink_INTERFACE_DEFINED__
typedef enum {
    TF_LC_CREATE = 0,
    TF_LC_CHANGE = 1,
    TF_LC_DESTROY = 2
} TfLayoutCode;

struct ITfTextLayoutSink : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE OnLayoutChange(
        ITfContext* pic,
        TfLayoutCode lcode,
        ITfContextView* pView) = 0;
};
#endif

#ifndef TF_CLIENTID_NULL
#define TF_CLIENTID_NULL ((TfClientId)0)
#endif
#ifndef TF_INVALID_GUIDATOM
#define TF_INVALID_GUIDATOM ((TfGuidAtom)0)
#endif

#ifndef __ITfTextInputProcessorEx_INTERFACE_DEFINED__
#define __ITfTextInputProcessorEx_INTERFACE_DEFINED__
struct ITfTextInputProcessorEx : public ITfTextInputProcessor {
public:
    virtual HRESULT STDMETHODCALLTYPE ActivateEx(
        ITfThreadMgr* ptim,
        TfClientId tid,
        DWORD dwFlags) = 0;
};
#endif

#ifndef __ITfDisplayAttributeInfo_INTERFACE_DEFINED__
#define __ITfDisplayAttributeInfo_INTERFACE_DEFINED__
typedef enum {
    TF_LS_NONE = 0,
    TF_LS_SOLID = 1,
    TF_LS_DOT = 2,
    TF_LS_DASH = 3,
    TF_LS_SQUIGGLE = 4
} TF_DA_LINESTYLE;

typedef enum {
    TF_CT_NONE = 0,
    TF_CT_SYSCOLOR = 1,
    TF_CT_COLORREF = 2
} TF_DA_COLORTYPE;

typedef struct TF_DA_COLOR {
    TF_DA_COLORTYPE type;
    union {
        int nIndex;
        COLORREF cr;
    };
} TF_DA_COLOR;

typedef enum {
    TF_ATTR_INPUT = 0,
    TF_ATTR_TARGET_CONVERTED = 1,
    TF_ATTR_CONVERTED = 2,
    TF_ATTR_TARGET_NOTCONVERTED = 3,
    TF_ATTR_INPUT_ERROR = 4,
    TF_ATTR_FIXEDCONVERTED = 5,
    TF_ATTR_OTHER = -1
} TF_DA_ATTR_INFO;

typedef struct TF_DISPLAYATTRIBUTE {
    TF_DA_COLOR crText;
    TF_DA_COLOR crBk;
    TF_DA_LINESTYLE lsStyle;
    BOOL fBoldLine;
    TF_DA_COLOR crLine;
    TF_DA_ATTR_INFO bAttr;
} TF_DISPLAYATTRIBUTE;

struct ITfDisplayAttributeInfo : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE GetGUID(GUID* pguid) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDescription(BSTR* pbstrDesc) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetAttributeInfo(const TF_DISPLAYATTRIBUTE* pda) = 0;
    virtual HRESULT STDMETHODCALLTYPE Reset() = 0;
};
#endif

#ifndef __IEnumTfDisplayAttributeInfo_INTERFACE_DEFINED__
#define __IEnumTfDisplayAttributeInfo_INTERFACE_DEFINED__
struct IEnumTfDisplayAttributeInfo : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE Clone(IEnumTfDisplayAttributeInfo** ppEnum) = 0;
    virtual HRESULT STDMETHODCALLTYPE Next(ULONG ulCount, ITfDisplayAttributeInfo** rgInfo, ULONG* pcFetched) = 0;
    virtual HRESULT STDMETHODCALLTYPE Reset() = 0;
    virtual HRESULT STDMETHODCALLTYPE Skip(ULONG ulCount) = 0;
};
#endif

#ifndef __ITfDisplayAttributeProvider_INTERFACE_DEFINED__
#define __ITfDisplayAttributeProvider_INTERFACE_DEFINED__
struct ITfDisplayAttributeProvider : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) = 0;
};
#endif

#ifndef __ITfInputProcessorProfileMgr_INTERFACE_DEFINED__
#define __ITfInputProcessorProfileMgr_INTERFACE_DEFINED__
struct IEnumTfInputProcessorProfiles;
typedef struct TF_INPUTPROCESSORPROFILE {
    DWORD dwProfileType;
    LANGID langid;
    CLSID clsid;
    GUID guidProfile;
    GUID catid;
    HKL hklSubstitute;
    DWORD dwCaps;
    HKL hkl;
    DWORD dwFlags;
} TF_INPUTPROCESSORPROFILE;

struct ITfInputProcessorProfileMgr : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE ActivateProfile(DWORD dwProfileType, LANGID langid, REFCLSID clsid, REFGUID guidProfile, HKL hkl, DWORD dwFlags) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeactivateProfile(DWORD dwProfileType, LANGID langid, REFCLSID clsid, REFGUID guidProfile, HKL hkl, DWORD dwFlags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProfile(DWORD dwProfileType, LANGID langid, REFCLSID clsid, REFGUID guidProfile, HKL hkl, TF_INPUTPROCESSORPROFILE* pProfile) = 0;
    virtual HRESULT STDMETHODCALLTYPE EnumProfiles(LANGID langid, IEnumTfInputProcessorProfiles** ppEnum) = 0;
    virtual HRESULT STDMETHODCALLTYPE ReleaseInputProcessor(REFCLSID rclsid, DWORD dwFlags) = 0;
    virtual HRESULT STDMETHODCALLTYPE RegisterProfile(REFCLSID rclsid, LANGID langid, REFGUID guidProfile, const WCHAR* pchDesc, ULONG cchDesc, const WCHAR* pchIconFile, ULONG cchFile, ULONG uIconIndex, HKL hklsubstitute, DWORD dwPreferredLayout, BOOL bEnabledByDefault, DWORD dwFlags) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnregisterProfile(REFCLSID rclsid, LANGID langid, REFGUID guidProfile, DWORD dwFlags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetActiveProfile(REFGUID catid, TF_INPUTPROCESSORPROFILE* pProfile) = 0;
};
#endif

// Global DLL helpers
extern HINSTANCE g_hInst;
void DllAddRef();
void DllRelease();

namespace liteime::tsf {
class ScopedPerMonitorDpi {
public:
    ScopedPerMonitorDpi() {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (!user32) return;
        using FnSetThreadDpi = HANDLE(WINAPI*)(HANDLE);
        setFn_ = reinterpret_cast<FnSetThreadDpi>(
            reinterpret_cast<void*>(GetProcAddress(user32, "SetThreadDpiAwarenessContext")));
        if (setFn_) {
            prev_ = setFn_(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4)));
        }
    }
    ~ScopedPerMonitorDpi() {
        if (setFn_ && prev_) {
            setFn_(prev_);
        }
    }

private:
    using FnSetThreadDpi = HANDLE(WINAPI*)(HANDLE);
    FnSetThreadDpi setFn_ = nullptr;
    HANDLE prev_ = nullptr;
};

HRESULT CreateTextServiceInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObj);
}
