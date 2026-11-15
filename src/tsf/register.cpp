#include "tsf/tsf_compat.hpp"

#include <array>
#include <string>

namespace {

constexpr LANGID kLangZhCn = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
constexpr const wchar_t* kImeDesc = L"\u6781\u7b80\u53cc\u8bed\u8f93\u5165\u6cd5 (LiteIME)";

const std::array<const GUID*, 8> kCategories = {
    &GUID_TFCAT_TIP_KEYBOARD,
    &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
    &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
    &GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
    &GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
    &GUID_TFCAT_TIPCAP_COMLESS,
    &GUID_TFCAT_TIPCAP_SECUREMODE,
    &GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
};

bool formatGuidString(REFGUID guid, wchar_t* buf, size_t len) {
    return StringFromGUID2(guid, buf, static_cast<int>(len)) > 0;
}

LONG registerComServer(const wchar_t* dllPath) {
    wchar_t clsidStr[64]{};
    if (!formatGuidString(CLSID_LiteIME, clsidStr, std::size(clsidStr))) return E_FAIL;

    std::wstring keyPath = std::wstring(L"CLSID\\") + clsidStr;
    HKEY hKey = nullptr;
    LONG rc = RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath.c_str(), 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (rc != ERROR_SUCCESS) return rc;

    RegSetValueExW(hKey, nullptr, 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(kImeDesc),
                   static_cast<DWORD>((wcslen(kImeDesc) + 1) * sizeof(wchar_t)));

    HKEY hInproc = nullptr;
    rc = RegCreateKeyExW(hKey, L"InprocServer32", 0, nullptr,
                         REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hInproc, nullptr);
    if (rc == ERROR_SUCCESS) {
        RegSetValueExW(hInproc, nullptr, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(dllPath),
                       static_cast<DWORD>((wcslen(dllPath) + 1) * sizeof(wchar_t)));
        const wchar_t* model = L"Apartment";
        RegSetValueExW(hInproc, L"ThreadingModel", 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(model),
                       static_cast<DWORD>((wcslen(model) + 1) * sizeof(wchar_t)));
        RegCloseKey(hInproc);
    }
    RegCloseKey(hKey);
    return rc;
}

void unregisterComServer() {
    wchar_t clsidStr[64]{};
    if (!formatGuidString(CLSID_LiteIME, clsidStr, std::size(clsidStr))) return;
    std::wstring keyPath = std::wstring(L"CLSID\\") + clsidStr;
    RegDeleteTreeW(HKEY_CLASSES_ROOT, keyPath.c_str());
}

} // namespace

STDAPI DllRegisterServer() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool didInitCom = SUCCEEDED(hr);

    wchar_t dllPath[MAX_PATH]{};
    DWORD pathLen = GetModuleFileNameW(g_hInst, dllPath, MAX_PATH);
    if (pathLen == 0) {
        if (didInitCom) CoUninitialize();
        return HRESULT_FROM_WIN32(GetLastError());
    }

    LONG regRc = registerComServer(dllPath);
    if (regRc != ERROR_SUCCESS) {
        if (didInitCom) CoUninitialize();
        return HRESULT_FROM_WIN32(regRc);
    }

    ITfInputProcessorProfileMgr* pProfileMgr = nullptr;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                          IID_ITfInputProcessorProfileMgr, reinterpret_cast<void**>(&pProfileMgr));
    if (SUCCEEDED(hr) && pProfileMgr) {
        hr = pProfileMgr->RegisterProfile(
            CLSID_LiteIME,
            kLangZhCn,
            GUID_LiteIMEProfile,
            kImeDesc,
            static_cast<ULONG>(wcslen(kImeDesc)),
            dllPath,
            pathLen,
            0,
            nullptr,
            0,
            TRUE,
            0);
        pProfileMgr->Release();
    }

    if (SUCCEEDED(hr)) {
        ITfCategoryMgr* pCatMgr = nullptr;
        hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                              IID_ITfCategoryMgr, reinterpret_cast<void**>(&pCatMgr));
        if (SUCCEEDED(hr) && pCatMgr) {
            for (const GUID* cat : kCategories) {
                pCatMgr->RegisterCategory(CLSID_LiteIME, *cat, CLSID_LiteIME);
            }
            pCatMgr->Release();
        }
    }

    if (didInitCom) CoUninitialize();
    return hr;
}

STDAPI DllUnregisterServer() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool didInitCom = SUCCEEDED(hr);

    ITfCategoryMgr* pCatMgr = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfCategoryMgr, reinterpret_cast<void**>(&pCatMgr))) && pCatMgr) {
        for (const GUID* cat : kCategories) {
            pCatMgr->UnregisterCategory(CLSID_LiteIME, *cat, CLSID_LiteIME);
        }
        pCatMgr->Release();
    }

    ITfInputProcessorProfileMgr* pProfileMgr = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfInputProcessorProfileMgr, reinterpret_cast<void**>(&pProfileMgr))) && pProfileMgr) {
        pProfileMgr->UnregisterProfile(CLSID_LiteIME, kLangZhCn, GUID_LiteIMEProfile, 0);
        pProfileMgr->Release();
    }

    unregisterComServer();
    if (didInitCom) CoUninitialize();
    return S_OK;
}
