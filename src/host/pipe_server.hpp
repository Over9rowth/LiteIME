#pragma once

#include "common/ipc_protocol.hpp"
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace liteime::host {

class PipeServer {
public:
    using RequestHandler = std::function<void(const ipc::Request&, ipc::Response&, uint64_t connId)>;
    using DisconnectHandler = std::function<void(uint64_t connId)>;

    PipeServer() = default;
    ~PipeServer();

    bool start(RequestHandler onRequest, DisconnectHandler onDisconnect = {});
    void stop();

private:
    struct ActiveClient {
        HANDLE pipe = INVALID_HANDLE_VALUE;
        HANDLE thread = nullptr;
    };

    void acceptLoop();
    void handleClient(HANDLE hPipe, uint64_t connId);

    RequestHandler onRequest_;
    DisconnectHandler onDisconnect_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> nextConnId_{1};
    std::atomic<int> activeClients_{0};
    std::mutex clientsMutex_;
    std::condition_variable clientsCv_;
    std::vector<ActiveClient> clients_;
    std::thread acceptThread_;
};

} // namespace liteime::host
