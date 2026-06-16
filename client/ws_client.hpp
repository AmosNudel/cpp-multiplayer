#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <string>

#include <ixwebsocket/IXWebSocket.h>

#include "common/protocol.hpp"

namespace net {

class WsClient {
public:
    using OpenHandler = std::function<void()>;
    using ErrorHandler = std::function<void(const std::string& reason)>;

    WsClient() = default;
    ~WsClient();

    bool Connect(const std::string& url, OpenHandler onOpen = nullptr,
                 ErrorHandler onError = nullptr);
    void Disconnect();
    bool IsConnected() const;
    bool Send(const Message& message);
    void Poll(std::function<void(const Message&)> onMessage);
    bool ConsumeConnectionLost();

private:
    void EnqueueMessage(const Message& message);
    void EnqueueError(const std::string& reason);

    ix::WebSocket webSocket_;
    OpenHandler onOpen_;
    ErrorHandler onError_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> connectionLost_{false};
    std::mutex sendMutex_;
    std::mutex incomingMutex_;
    std::deque<Message> incoming_;
    std::string pendingError_;
};

}  // namespace net
