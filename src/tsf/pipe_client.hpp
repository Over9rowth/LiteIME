#pragma once

#include "common/ipc_protocol.hpp"
#include <cstdint>
#include <string>

namespace liteime::tsf {

class PipeClient {
public:
    PipeClient() = default;
    ~PipeClient();

    bool sendRequest(const ipc::Request& req, ipc::Response& resp);
    void disconnect();

private:
    bool ensureConnected();
    void tryLaunchHost();
    std::wstring pipeName() const;

    void* pipeHandle_ = nullptr; // HANDLE
    void* ioEvent_ = nullptr;    // HANDLE (manual-reset event for bounded overlapped IPC)
    uint64_t lastLaunchAttemptMs_ = 0;
};

} // namespace liteime::tsf
