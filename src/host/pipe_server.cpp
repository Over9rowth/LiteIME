#include "host/pipe_server.hpp"
#include "host/host_messages.hpp"
#include "common/utf.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sddl.h>
#include <string>

namespace liteime::host {

namespace {

void cancelSyncIoForThread(HANDLE hThread) {
    if (!hThread) return;
    using FnCancelSync = BOOL(WINAPI*)(HANDLE);
    static auto fn = reinterpret_cast<FnCancelSync>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CancelSynchronousIo")));
    if (fn) {
        fn(hThread);
    }
}

std::wstring makePipeName() {
    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
    return ipc::makePipeName(sessionId);
}

std::wstring makeNotifyPipeName() {
    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
    return ipc::makeNotifyPipeName(sessionId);
}

std::wstring queryCurrentUserSidString() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        return {};
    }
    DWORD len = 0;
    GetTokenInformation(hToken, TokenUser, nullptr, 0, &len);
    std::wstring sidOut;
    if (len > 0) {
        std::vector<uint8_t> buf(len);
        if (GetTokenInformation(hToken, TokenUser, buf.data(), len, &len)) {
            auto* tu = reinterpret_cast<PTOKEN_USER>(buf.data());
            LPWSTR sidStr = nullptr;
            if (ConvertSidToStringSidW(tu->User.Sid, &sidStr) && sidStr) {
                sidOut = sidStr;
                LocalFree(sidStr);
            }
        }
    }
    CloseHandle(hToken);
    return sidOut;
}

// Allow only the current user (including elevated split-token) and AppContainer (Edge/UWP) clients at Low Integrity.
bool buildUserScopedSecurityAttributes(SECURITY_ATTRIBUTES& sa, PSECURITY_DESCRIPTOR& outSd) {
    outSd = nullptr;
    const std::wstring userSid = queryCurrentUserSidString();
    const std::wstring principal = userSid.empty() ? std::wstring(L"CO") : userSid;
    const std::wstring sddl = L"D:(A;;GRGW;;;" + principal + L")(A;;GRGW;;;AC)S:(ML;;NW;;;LW)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &outSd, nullptr)) {
        const std::wstring fallback = L"D:(A;;GRGW;;;" + principal + L")S:(ML;;NW;;;LW)";
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(fallback.c_str(), SDDL_REVISION_1, &outSd, nullptr)) {
            return false;
        }
    }
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = outSd;
    sa.bInheritHandle = FALSE;
    return true;
}

} // namespace

PipeServer::~PipeServer() {
    stop();
}

bool PipeServer::start(RequestHandler onRequest, DisconnectHandler onDisconnect) {
    if (running_.load()) return true;
    onRequest_ = std::move(onRequest);
    onDisconnect_ = std::move(onDisconnect);
    if (!notifyStopEvent_) {
        notifyStopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    } else {
        ResetEvent(notifyStopEvent_);
    }
    running_.store(true);
    acceptThread_ = std::thread(&PipeServer::acceptLoop, this);
    notifyAcceptThread_ = std::thread(&PipeServer::notifyAcceptLoop, this);
    return true;
}

void PipeServer::stop() {
    if (!running_.exchange(false)) return;
    if (notifyStopEvent_) {
        SetEvent(notifyStopEvent_);
    }
    // Wake blocking ConnectNamedPipe by briefly connecting as a dummy client
    std::wstring name = makePipeName();
    HANDLE dummy = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (dummy != INVALID_HANDLE_VALUE) {
        CloseHandle(dummy);
    }
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
    if (notifyAcceptThread_.joinable()) {
        notifyAcceptThread_.join();
    }
    {
        std::lock_guard<std::mutex> lock(notifyMutex_);
        for (const auto& nc : notifyClients_) {
            if (nc.pipe && nc.pipe != INVALID_HANDLE_VALUE) {
                CloseHandle(nc.pipe);
            }
        }
        notifyClients_.clear();
    }
    if (notifyStopEvent_) {
        CloseHandle(notifyStopEvent_);
        notifyStopEvent_ = nullptr;
    }

    // Wake every detached client thread blocked in ReadFile/WriteFile and wait until all exit cleanly.
    auto wakeClients = [this]() {
        std::vector<HANDLE> threads;
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            threads.reserve(clients_.size());
            for (const auto& c : clients_) {
                if (c.thread) threads.push_back(c.thread);
            }
            for (HANDLE th : threads) {
                cancelSyncIoForThread(th);
            }
        }
    };
    wakeClients();
    {
        std::unique_lock<std::mutex> lock(clientsMutex_);
        for (int attempt = 0; attempt < 20 && activeClients_.load(std::memory_order_acquire) > 0; ++attempt) {
            if (clientsCv_.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                    return activeClients_.load(std::memory_order_acquire) == 0;
                })) {
                break;
            }
            lock.unlock();
            wakeClients();
            lock.lock();
        }
    }
}

void PipeServer::acceptLoop() {
    const std::wstring name = makePipeName();
    SECURITY_ATTRIBUTES sa{};
    PSECURITY_DESCRIPTOR sd = nullptr;
    bool hasSa = buildUserScopedSecurityAttributes(sa, sd);

    while (running_.load()) {
        HANDLE hPipe = CreateNamedPipeW(
            name.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            sizeof(ipc::Response) * 4,
            sizeof(ipc::Request) * 4,
            0,
            hasSa ? &sa : nullptr);

        if (hPipe == INVALID_HANDLE_VALUE) {
            Sleep(50);
            continue;
        }

        BOOL connected = ConnectNamedPipe(hPipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!running_.load()) {
            CloseHandle(hPipe);
            break;
        }
        if (connected) {
            uint64_t connId = nextConnId_.fetch_add(1, std::memory_order_relaxed);
            activeClients_.fetch_add(1, std::memory_order_acq_rel);
            try {
                std::thread(&PipeServer::handleClient, this, hPipe, connId).detach();
            } catch (...) {
                activeClients_.fetch_sub(1, std::memory_order_acq_rel);
                CloseHandle(hPipe);
            }
        } else {
            CloseHandle(hPipe);
        }
    }

    if (sd) LocalFree(sd);
}

void PipeServer::handleClient(HANDLE hPipe, uint64_t connId) {
    HANDLE hSelfThread = nullptr;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                    &hSelfThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_.push_back(ActiveClient{hPipe, hSelfThread});
    }

    struct ClientLifecycleGuard {
        PipeServer* self;
        HANDLE hPipe;
        HANDLE hThread;
        ~ClientLifecycleGuard() {
            {
                std::lock_guard<std::mutex> lock(self->clientsMutex_);
                auto it = std::remove_if(self->clients_.begin(), self->clients_.end(),
                                         [this](const ActiveClient& c) { return c.pipe == hPipe; });
                self->clients_.erase(it, self->clients_.end());
                self->activeClients_.fetch_sub(1, std::memory_order_acq_rel);
            }
            if (hThread) CloseHandle(hThread);
            self->clientsCv_.notify_all();
        }
    } clientGuard{this, hPipe, hSelfThread};

    ULONG clientPid = 0;
    GetNamedPipeClientProcessId(hPipe, &clientPid);

    while (running_.load(std::memory_order_acquire)) {
        ipc::Request req{};
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(hPipe, &req, sizeof(req), &bytesRead, nullptr);
        if (!ok || bytesRead < sizeof(ipc::Request)) {
            break;
        }

        ipc::Response resp{};
        const bool isV1 = (req.version == ipc::kProtocolVersionV1);
        const bool isV2 = (req.version == ipc::kProtocolVersionV2);
        const bool isV3 = (req.version == ipc::kProtocolVersion);
        if (req.magic == ipc::kProtocolMagic && (isV1 || isV2 || isV3)) {
            if (running_.load(std::memory_order_acquire) && onRequest_) {
                onRequest_(req, resp, connId, static_cast<uint32_t>(clientPid));
            }
        }

        DWORD bytesWritten = 0;
        if (isV1) {
            ipc::ResponseV1 respV1{};
            respV1.magic = resp.magic;
            respV1.version = ipc::kProtocolVersionV1;
            respV1.consumed = resp.consumed;
            respV1.isEnglish = resp.isEnglish;
            respV1.isFullWidth = resp.isFullWidth;
            respV1.isCnPunct = resp.isCnPunct;
            if (resp.preeditLen > 0) {
                const size_t n = liteime::copyUtf8Bounded(
                    std::string_view(resp.preedit, std::min<size_t>(resp.preeditLen, sizeof(resp.preedit))),
                    respV1.preedit, sizeof(respV1.preedit));
                respV1.preeditLen = static_cast<uint16_t>(n);
                respV1.caretPos = respV1.preeditLen;
            }
            if (resp.commitLen > 0) {
                const size_t n = liteime::copyUtf8Bounded(
                    std::string_view(resp.commit, std::min<size_t>(resp.commitLen, sizeof(resp.commit))),
                    respV1.commit, sizeof(respV1.commit));
                respV1.commitLen = static_cast<uint16_t>(n);
            }
            if (!WriteFile(hPipe, &respV1, sizeof(respV1), &bytesWritten, nullptr)) {
                break;
            }
        } else if (isV2) {
            ipc::ResponseV2 respV2{};
            respV2.magic = resp.magic;
            respV2.version = ipc::kProtocolVersionV2;
            respV2.consumed = resp.consumed;
            respV2.isEnglish = resp.isEnglish;
            respV2.isFullWidth = resp.isFullWidth;
            respV2.isCnPunct = resp.isCnPunct;
            respV2.isComposing = resp.isComposing;
            respV2.reserved = resp.reserved;
            respV2.preeditLen = resp.preeditLen;
            respV2.commitLen = resp.commitLen;
            respV2.caretPos = resp.caretPos;
            if (resp.preeditLen > 0) {
                std::memcpy(respV2.preedit, resp.preedit, std::min<size_t>(resp.preeditLen, sizeof(respV2.preedit)));
            }
            if (resp.commitLen > 0) {
                std::memcpy(respV2.commit, resp.commit, std::min<size_t>(resp.commitLen, sizeof(respV2.commit)));
            }
            if (!WriteFile(hPipe, &respV2, sizeof(respV2), &bytesWritten, nullptr)) {
                break;
            }
        } else {
            if (!WriteFile(hPipe, &resp, sizeof(resp), &bytesWritten, nullptr)) {
                break;
            }
        }
    }
    FlushFileBuffers(hPipe);
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
    if (running_.load(std::memory_order_acquire) && onDisconnect_) {
        onDisconnect_(connId);
    }
}

void PipeServer::notifyAcceptLoop() {
    const std::wstring name = makeNotifyPipeName();
    SECURITY_ATTRIBUTES sa{};
    PSECURITY_DESCRIPTOR sd = nullptr;
    bool hasSa = buildUserScopedSecurityAttributes(sa, sd);

    HANDLE connectEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    while (running_.load(std::memory_order_acquire)) {
        HANDLE hPipe = CreateNamedPipeW(
            name.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            sizeof(ipc::NotifyPacket) * 4,
            sizeof(ipc::NotifyHello) * 4,
            0,
            hasSa ? &sa : nullptr);
        if (hPipe == INVALID_HANDLE_VALUE) {
            Sleep(50);
            continue;
        }

        ResetEvent(connectEvt);
        OVERLAPPED ov{};
        ov.hEvent = connectEvt;
        BOOL connected = ConnectNamedPipe(hPipe, &ov);
        if (!connected) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                HANDLE waits[2] = {notifyStopEvent_, connectEvt};
                DWORD wr = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (wr != WAIT_OBJECT_0 + 1) {
                    CancelIoEx(hPipe, &ov);
                    CloseHandle(hPipe);
                    break;
                }
                DWORD dummy = 0;
                connected = GetOverlappedResult(hPipe, &ov, &dummy, FALSE);
            } else if (err == ERROR_PIPE_CONNECTED) {
                connected = TRUE;
            }
        }

        if (!running_.load(std::memory_order_acquire) || !connected) {
            CloseHandle(hPipe);
            if (!running_.load(std::memory_order_acquire)) break;
            continue;
        }

        // Briefly read NotifyHello with a bounded timeout so an unresponsive peer cannot stall acceptLoop
        ResetEvent(connectEvt);
        OVERLAPPED rov{};
        rov.hEvent = connectEvt;
        ipc::NotifyHello hello{};
        DWORD br = 0;
        BOOL rok = ReadFile(hPipe, &hello, sizeof(hello), &br, &rov);
        if (!rok && GetLastError() == ERROR_IO_PENDING) {
            DWORD wr = WaitForSingleObject(connectEvt, 150);
            if (wr == WAIT_OBJECT_0) {
                rok = GetOverlappedResult(hPipe, &rov, &br, FALSE);
            } else {
                CancelIoEx(hPipe, &rov);
                DWORD dummy = 0;
                GetOverlappedResult(hPipe, &rov, &dummy, TRUE);
            }
        }

        ULONG actualPid = 0;
        if (!rok || br < sizeof(ipc::NotifyHello) || hello.magic != ipc::kProtocolMagic ||
            !GetNamedPipeClientProcessId(hPipe, &actualPid) || actualPid == 0) {
            CloseHandle(hPipe);
            continue;
        }

        std::lock_guard<std::mutex> lock(notifyMutex_);
        // Prune dead handles before adding the new one
        notifyClients_.erase(
            std::remove_if(notifyClients_.begin(), notifyClients_.end(), [](const NotifyClient& nc) {
                DWORD avail = 0;
                if (!PeekNamedPipe(nc.pipe, nullptr, 0, nullptr, &avail, nullptr)) {
                    DWORD err = GetLastError();
                    if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED || err == ERROR_INVALID_HANDLE) {
                        CloseHandle(nc.pipe);
                        return true;
                    }
                }
                return false;
            }),
            notifyClients_.end());
        notifyClients_.push_back(NotifyClient{hPipe, static_cast<uint32_t>(actualPid), hello.hwnd});
    }

    if (connectEvt) CloseHandle(connectEvt);
    if (sd) LocalFree(sd);
}

bool PipeServer::sendNotify(uint32_t targetPid, const ipc::NotifyPacket& pkt) {
    if (targetPid == 0 || pkt.targetHwnd == 0 || !running_.load(std::memory_order_acquire)) return false;

    std::lock_guard<std::mutex> lock(notifyMutex_);
    auto it = notifyClients_.begin();
    while (it != notifyClients_.end()) {
        DWORD avail = 0;
        if (!PeekNamedPipe(it->pipe, nullptr, 0, nullptr, &avail, nullptr)) {
            DWORD err = GetLastError();
            if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED || err == ERROR_INVALID_HANDLE) {
                CloseHandle(it->pipe);
                it = notifyClients_.erase(it);
                continue;
            }
        }
        ++it;
    }

    bool hasExactMatch = false;
    for (const auto& nc : notifyClients_) {
        if (nc.pid == targetPid && nc.hwnd == pkt.targetHwnd) {
            hasExactMatch = true;
            break;
        }
    }

    HWND targetHwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(pkt.targetHwnd));
    HWND targetRoot = IsWindow(targetHwnd) ? GetAncestor(targetHwnd, GA_ROOT) : nullptr;

    bool sentAny = false;
    HANDLE writeEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    it = notifyClients_.begin();
    while (it != notifyClients_.end()) {
        if (it->pid == targetPid) {
            bool match = false;
            if (hasExactMatch) {
                match = (it->hwnd == pkt.targetHwnd);
            } else if (it->hwnd == 0) {
                match = true;
            } else if (targetRoot != nullptr) {
                HWND ncHwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(it->hwnd));
                if (IsWindow(ncHwnd) && GetAncestor(ncHwnd, GA_ROOT) == targetRoot) {
                    match = true;
                }
            }
            if (!match) {
                ++it;
                continue;
            }

            ResetEvent(writeEvt);
            OVERLAPPED ov{};
            ov.hEvent = writeEvt;
            DWORD bw = 0;
            BOOL ok = WriteFile(it->pipe, &pkt, sizeof(pkt), &bw, &ov);
            if (!ok && GetLastError() == ERROR_IO_PENDING) {
                if (writeEvt && WaitForSingleObject(writeEvt, 50) == WAIT_OBJECT_0) {
                    ok = GetOverlappedResult(it->pipe, &ov, &bw, FALSE);
                } else {
                    CancelIoEx(it->pipe, &ov);
                    DWORD dummy = 0;
                    GetOverlappedResult(it->pipe, &ov, &dummy, TRUE);
                }
            }
            if (ok && bw == sizeof(pkt)) {
                sentAny = true;
            } else {
                CloseHandle(it->pipe);
                it = notifyClients_.erase(it);
                continue;
            }
        }
        ++it;
    }
    if (writeEvt) CloseHandle(writeEvt);
    return sentAny;
}

} // namespace liteime::host
