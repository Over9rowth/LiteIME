#include "tsf/tsf_compat.hpp"
#include <atomic>

HINSTANCE g_hInst = nullptr;
static std::atomic<LONG> g_dllRefCount{0};

void DllAddRef() {
    g_dllRefCount.fetch_add(1, std::memory_order_relaxed);
}

void DllRelease() {
    g_dllRefCount.fetch_sub(1, std::memory_order_relaxed);
}

BOOL APIENTRY DllMain(HINSTANCE hInstance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = hInstance;
        DisableThreadLibraryCalls(hInstance);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow() {
    return (g_dllRefCount.load(std::memory_order_acquire) <= 0) ? S_OK : S_FALSE;
}
