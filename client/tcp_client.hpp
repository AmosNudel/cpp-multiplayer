#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "common/protocol.hpp"
#include "common/socket_utils.hpp"

namespace net {

class TcpClient {
public:
    using MessageHandler = std::function<void(const Message& message)>;

    TcpClient() = default;
    ~TcpClient();

    bool Connect(const std::string& host, uint16_t port, MessageHandler onMessage);
    void Disconnect();
    bool IsConnected() const { return connected_; }
    bool Send(const Message& message);

private:
    void ReceiveLoop();

    SocketHandle socket_ = kInvalidSocket;
    std::thread receiveThread_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    MessageHandler onMessage_;
    std::string receiveBuffer_;
    std::mutex sendMutex_;
};

}  // namespace net
