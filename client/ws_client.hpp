#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <string>

#if !defined(PLATFORM_WEB)
#include <ixwebsocket/IXWebSocket.h>
#endif

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

#if defined(PLATFORM_WEB)
    void HandleBrowserOpen();
    void HandleBrowserMessage(const std::string& json);
    void HandleBrowserError(const std::string& reason);
    void HandleBrowserClose();
    bool MatchesSocket(int socketId) const { return socketId_ == socketId; }
#endif

private:
    void EnqueueMessage(const Message& message);
    void EnqueueError(const std::string& reason);

#if defined(PLATFORM_WEB)
    int socketId_ = 0;
#else
    ix::WebSocket webSocket_;
#endif
    OpenHandler onOpen_;
    ErrorHandler onError_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> connectionLost_{false};
    std::atomic<bool> pendingOpen_{false};
    std::mutex sendMutex_;
    std::mutex incomingMutex_;
    std::deque<Message> incoming_;
    std::string pendingError_;
};

}  // namespace net
