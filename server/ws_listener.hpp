#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <ixwebsocket/IXWebSocketServer.h>

#include "common/protocol.hpp"

namespace net {

class WsListener {
public:
    using MessageHandler = std::function<void(int clientId, const Message& message)>;
    using DisconnectHandler = std::function<void(int clientId)>;

    WsListener() = default;
    ~WsListener();

    bool Start(uint16_t port, MessageHandler onMessage, DisconnectHandler onDisconnect);
    void Stop();
    bool SendTo(int clientId, const Message& message);

private:
    struct ClientEntry {
        std::weak_ptr<ix::WebSocket> socket;
        std::shared_ptr<ix::ConnectionState> connection;
    };

    void HandleMessage(const std::shared_ptr<ix::ConnectionState>& connectionState,
                       ix::WebSocket& webSocket,
                       const ix::WebSocketMessagePtr& message);

    std::unique_ptr<ix::WebSocketServer> server_;
    MessageHandler onMessage_;
    DisconnectHandler onDisconnect_;
    std::mutex clientsMutex_;
    std::unordered_map<int, ClientEntry> clientsById_;
    std::unordered_map<std::shared_ptr<ix::ConnectionState>, int> idByConnection_;
    int nextClientId_ = 10000;
};

}  // namespace net
