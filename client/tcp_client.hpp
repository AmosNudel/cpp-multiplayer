#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "common/protocol.hpp"
#include "common/socket_utils.hpp"

namespace net {

class TcpClient {
public:
    TcpClient() = default;
    ~TcpClient();

    bool Connect(const std::string& host, uint16_t port);
    void Disconnect();
    bool IsConnected() const { return connected_; }
    bool Send(const Message& message);
    void Poll(std::function<void(const Message&)> onMessage);
    bool ConsumeConnectionLost();

private:
    void ReceiveLoop();
    void EnqueueMessage(const Message& message);

    SocketHandle socket_ = kInvalidSocket;
    std::thread receiveThread_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> connectionLost_{false};
    std::string receiveBuffer_;
    std::mutex sendMutex_;
    std::mutex incomingMutex_;
    std::deque<Message> incoming_;
};

}  // namespace net
