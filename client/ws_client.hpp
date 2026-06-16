#pragma once

#include <functional>
#include <mutex>
#include <string>

#include <ixwebsocket/IXWebSocket.h>

#include "common/protocol.hpp"

namespace net {

class WsClient {
public:
    using MessageHandler = std::function<void(const Message& message)>;

    WsClient() = default;
    ~WsClient();

    bool Connect(const std::string& url, MessageHandler onMessage);
    void Disconnect();
    bool IsConnected() const;
    bool Send(const Message& message);
    void Poll();

private:
    ix::WebSocket webSocket_;
    MessageHandler onMessage_;
    std::mutex sendMutex_;
};

}  // namespace net
