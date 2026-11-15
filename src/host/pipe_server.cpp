#include "host/pipe_server.hpp"
#include "host/host_messages.hpp"

#include <algorithm>
#include <chrono>
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
    wchar_t buf[128]{};
    wsprintfW(buf, L"\\\\.\\pipe\\LiteIME_Host_%lu", sessionId);
    return buf;
}

// Allow standard users, Low Integrity, and AppContainer (Edge/UWP) clients to connect.
bool buildPermissiveSecurityAttributes(SECURITY_ATTRIBUTES& sa, PSECURITY_DESCRIPTOR& outSd) {
    outSd = nullptr;
    // SDDL: Allow Everyone (WD) + All App Packages (AC) generic read/write, Low Mandatory Level (LW)
    const wchar_t* sddl = L"D:(A;;GRGW;;;WD)(A;;GRGW;;;AC)S:(ML;;NW;;;LW)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &outSd, nullptr)) {
        // Fallback without AC for older environments
        const wchar_t* fallback = L"D:(A;;GRGW;;;WD)S:(ML;;NW;;;LW)";
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(fallback, SDDL_REVISION_1, &outSd, nullptr)) {
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
    running_.store(true);
    acceptThread_ = std::thread(&PipeServer::acceptLoop, this);
    return true;
}

void PipeServer::stop() {
    if (!running_.exchange(false)) return;
    // Wake blocking ConnectNamedPipe by briefly connecting as a dummy client
    std::wstring name = makePipeName();
    HANDLE dummy = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (dummy != INVALID_HANDLE_VALUE) {
        CloseHandle(dummy);
    }
    if (acceptThread_.joinable()) {
        acceptThread_.join();
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
    bool hasSa = buildPermissiveSecurityAttributes(sa, sd);

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

    while (running_.load(std::memory_order_acquire)) {
        ipc::Request req{};
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(hPipe, &req, sizeof(req), &bytesRead, nullptr);
        if (!ok || bytesRead < sizeof(ipc::Request)) {
            break;
        }

        ipc::Response resp{};
        if (req.magic == ipc::kProtocolMagic && req.version == ipc::kProtocolVersion) {
            if (running_.load(std::memory_order_acquire) && onRequest_) {
                onRequest_(req, resp, connId);
            }
        }

        DWORD bytesWritten = 0;
        if (!WriteFile(hPipe, &resp, sizeof(resp), &bytesWritten, nullptr)) {
            break;
        }
    }
    FlushFileBuffers(hPipe);
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
    if (running_.load(std::memory_order_acquire) && onDisconnect_) {
        onDisconnect_(connId);
    }
}

} // namespace liteime::host
