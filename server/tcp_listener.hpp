#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "common/protocol.hpp"
#include "common/socket_utils.hpp"

namespace net {

class TcpListener {
public:
    using MessageHandler = std::function<void(int clientId, const Message& message)>;
    using DisconnectHandler = std::function<void(int clientId)>;

    TcpListener() = default;
    ~TcpListener();

    bool Start(uint16_t port, MessageHandler onMessage, DisconnectHandler onDisconnect);
    void Stop();
    bool SendTo(int clientId, const Message& message);
    bool IsRunning() const { return running_; }

private:
    struct Client {
        int id = 0;
        SocketHandle socket = kInvalidSocket;
        std::string receiveBuffer;
        std::thread thread;
        std::atomic<bool> alive{true};
    };

    void AcceptLoop();
    void ClientLoop(const std::shared_ptr<Client>& client);

    SocketHandle listenSocket_ = kInvalidSocket;
    std::thread acceptThread_;
    std::atomic<bool> running_{false};
    MessageHandler onMessage_;
    DisconnectHandler onDisconnect_;
    std::mutex clientsMutex_;
    std::vector<std::shared_ptr<Client>> clients_;
    int nextClientId_ = 1;
};

}  // namespace net
