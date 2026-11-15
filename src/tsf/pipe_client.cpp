#include "tsf/pipe_client.hpp"
#include "tsf/tsf_compat.hpp"

#include <cstring>

namespace liteime::tsf {

PipeClient::~PipeClient() {
    stopNotify();
    disconnect();
    if (ioEvent_) {
        CloseHandle(static_cast<HANDLE>(ioEvent_));
        ioEvent_ = nullptr;
    }
}

void PipeClient::startNotify(NotifyCallback cb) {
    stopNotify();
    notifyCb_ = std::move(cb);
    notifyStopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!notifyStopEvent_) return;
    try {
        notifyThread_ = std::thread(&PipeClient::notifyLoop, this);
    } catch (...) {
        CloseHandle(static_cast<HANDLE>(notifyStopEvent_));
        notifyStopEvent_ = nullptr;
    }
}

void PipeClient::stopNotify() {
    if (notifyStopEvent_) {
        SetEvent(static_cast<HANDLE>(notifyStopEvent_));
    }
    if (notifyThread_.joinable()) {
        notifyThread_.join();
    }
    if (notifyStopEvent_) {
        CloseHandle(static_cast<HANDLE>(notifyStopEvent_));
        notifyStopEvent_ = nullptr;
    }
    notifyCb_ = {};
}

void PipeClient::notifyLoop() {
    HANDLE stopEvt = static_cast<HANDLE>(notifyStopEvent_);
    HANDLE ioEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ioEvt || !stopEvt) {
        if (ioEvt) CloseHandle(ioEvt);
        return;
    }

    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
    const std::wstring notifyPipeName = ipc::makeNotifyPipeName(sessionId);

    while (WaitForSingleObject(stopEvt, 0) != WAIT_OBJECT_0) {
        HANDLE hPipe = CreateFileW(
            notifyPipeName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            nullptr);
        if (hPipe == INVALID_HANDLE_VALUE) {
            if (WaitForSingleObject(stopEvt, 600) == WAIT_OBJECT_0) break;
            continue;
        }

        DWORD mode = PIPE_READMODE_MESSAGE;
        SetNamedPipeHandleState(hPipe, &mode, nullptr, nullptr);

        ipc::NotifyHello hello{};
        hello.magic = ipc::kProtocolMagic;
        hello.pid = GetCurrentProcessId();
        hello.hwnd = notifyHwnd_.load(std::memory_order_acquire);

        ResetEvent(ioEvt);
        OVERLAPPED wov{};
        wov.hEvent = ioEvt;
        DWORD bw = 0;
        BOOL wok = WriteFile(hPipe, &hello, sizeof(hello), &bw, &wov);
        if (!wok && GetLastError() == ERROR_IO_PENDING) {
            HANDLE waits[2] = {stopEvt, ioEvt};
            DWORD wr = WaitForMultipleObjects(2, waits, FALSE, 250);
            if (wr == WAIT_OBJECT_0 + 1) {
                wok = GetOverlappedResult(hPipe, &wov, &bw, FALSE);
            } else {
                CancelIoEx(hPipe, &wov);
                DWORD dummy = 0;
                GetOverlappedResult(hPipe, &wov, &dummy, TRUE);
                CloseHandle(hPipe);
                if (wr == WAIT_OBJECT_0) break;
                continue;
            }
        }
        if (!wok || bw < sizeof(hello)) {
            CloseHandle(hPipe);
            if (WaitForSingleObject(stopEvt, 300) == WAIT_OBJECT_0) break;
            continue;
        }

        while (WaitForSingleObject(stopEvt, 0) != WAIT_OBJECT_0) {
            ipc::NotifyPacket pkt{};
            ResetEvent(ioEvt);
            OVERLAPPED rov{};
            rov.hEvent = ioEvt;
            DWORD br = 0;
            BOOL rok = ReadFile(hPipe, &pkt, sizeof(pkt), &br, &rov);
            if (!rok && GetLastError() == ERROR_IO_PENDING) {
                HANDLE waits[2] = {stopEvt, ioEvt};
                DWORD wr = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (wr == WAIT_OBJECT_0 + 1) {
                    rok = GetOverlappedResult(hPipe, &rov, &br, FALSE);
                } else {
                    CancelIoEx(hPipe, &rov);
                    DWORD dummy = 0;
                    GetOverlappedResult(hPipe, &rov, &dummy, TRUE);
                    break;
                }
            }
            if (!rok || br < sizeof(pkt)) {
                break;
            }
            if (pkt.magic == ipc::kProtocolMagic &&
                pkt.type == static_cast<uint16_t>(ipc::NotifyType::ApplyResponse) &&
                notifyCb_) {
                notifyCb_(pkt);
            }
        }

        CloseHandle(hPipe);
        if (WaitForSingleObject(stopEvt, 150) == WAIT_OBJECT_0) break;
    }

    CloseHandle(ioEvt);
}

void PipeClient::disconnect() {
    if (pipeHandle_ && pipeHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(static_cast<HANDLE>(pipeHandle_));
    }
    pipeHandle_ = nullptr;
}

std::wstring PipeClient::pipeName() const {
    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
    wchar_t buf[128]{};
    wsprintfW(buf, L"\\\\.\\pipe\\LiteIME_Host_%lu", sessionId);
    return buf;
}

void PipeClient::tryLaunchHost() {
    uint64_t now = GetTickCount64();
    if (now - lastLaunchAttemptMs_ < 3000) return;
    lastLaunchAttemptMs_ = now;

    wchar_t dllPath[MAX_PATH]{};
    if (GetModuleFileNameW(g_hInst, dllPath, MAX_PATH) == 0) return;

    std::wstring hostPath(dllPath);
    size_t slash = hostPath.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return;
    hostPath.resize(slash + 1);
    hostPath += L"LiteIMEHost.exe";

    if (GetFileAttributesW(hostPath.c_str()) == INVALID_FILE_ATTRIBUTES) return;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(hostPath.c_str(), nullptr, nullptr, nullptr, FALSE,
                       DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

bool PipeClient::ensureConnected() {
    if (pipeHandle_ && pipeHandle_ != INVALID_HANDLE_VALUE) {
        return true;
    }

    if (!ioEvent_) {
        ioEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ioEvent_) return false;
    }

    std::wstring name = pipeName();
    HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND) {
            tryLaunchHost();
            // Retry briefly for cold-start host startup (up to 3 x 120ms = 360ms)
            for (int w = 0; w < 3 && h == INVALID_HANDLE_VALUE; ++w) {
                if (WaitNamedPipeW(name.c_str(), 120)) {
                    h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
                } else {
                    Sleep(40);
                }
            }
        } else if (err == ERROR_PIPE_BUSY) {
            if (WaitNamedPipeW(name.c_str(), 40)) {
                h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            }
        }
    }

    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(h, &mode, nullptr, nullptr);
    pipeHandle_ = h;
    return true;
}

bool PipeClient::sendRequest(const ipc::Request& req, ipc::Response& resp) {
    constexpr DWORD kIpcTimeoutMs = 250;
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!ensureConnected()) return false;

        HANDLE hPipe = static_cast<HANDLE>(pipeHandle_);
        HANDLE hEvt = static_cast<HANDLE>(ioEvent_);
        ResetEvent(hEvt);

        OVERLAPPED ov{};
        ov.hEvent = hEvt;
        DWORD bytesRead = 0;
        BOOL ok = TransactNamedPipe(
            hPipe,
            const_cast<ipc::Request*>(&req),
            sizeof(req),
            &resp,
            sizeof(resp),
            &bytesRead,
            &ov);

        bool timedOut = false;
        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                DWORD wait = WaitForSingleObject(hEvt, kIpcTimeoutMs);
                if (wait == WAIT_OBJECT_0) {
                    ok = GetOverlappedResult(hPipe, &ov, &bytesRead, FALSE);
                } else {
                    timedOut = true;
                    CancelIoEx(hPipe, &ov);
                    DWORD dummy = 0;
                    GetOverlappedResult(hPipe, &ov, &dummy, TRUE);
                }
            }
        }

        if (ok && bytesRead >= sizeof(ipc::Response) && resp.magic == ipc::kProtocolMagic) {
            return true;
        }
        disconnect();
        if (timedOut) {
            return false;
        }
    }
    return false;
}

} // namespace liteime::tsf
