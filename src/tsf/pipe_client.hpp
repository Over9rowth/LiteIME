#pragma once

#include "common/ipc_protocol.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace liteime::tsf {

class PipeClient {
public:
    using NotifyCallback = std::function<void(const ipc::NotifyPacket&)>;

    PipeClient() = default;
    ~PipeClient();

    bool sendRequest(const ipc::Request& req, ipc::Response& resp);
    void disconnect();

    void startNotify(NotifyCallback cb);
    void stopNotify();
    void setNotifyHwnd(uint64_t hwnd) { notifyHwnd_.store(hwnd, std::memory_order_release); }

private:
    bool ensureConnected();
    void tryLaunchHost();
    void notifyLoop();
    std::wstring pipeName() const;

    void* pipeHandle_ = nullptr; // HANDLE
    void* ioEvent_ = nullptr;    // HANDLE (manual-reset event for bounded overlapped IPC)
    uint64_t lastLaunchAttemptMs_ = 0;

    NotifyCallback notifyCb_;
    std::atomic<uint64_t> notifyHwnd_{0};
    void* notifyStopEvent_ = nullptr; // HANDLE
    std::thread notifyThread_;
};

} // namespace liteime::tsf
