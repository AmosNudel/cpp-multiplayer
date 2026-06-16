#include "ws_listener.hpp"

#include <iostream>

namespace net {

WsListener::~WsListener() {
    Stop();
}

bool WsListener::Start(uint16_t port, MessageHandler onMessage, DisconnectHandler onDisconnect) {
    onMessage_ = std::move(onMessage);
    onDisconnect_ = std::move(onDisconnect);

    server_ = std::make_unique<ix::WebSocketServer>(
        static_cast<int>(port),
        "0.0.0.0",
        ix::SocketServer::kDefaultTcpBacklog,
        ix::SocketServer::kDefaultMaxConnections,
        ix::WebSocketServer::kDefaultHandShakeTimeoutSecs,
        ix::SocketServer::kDefaultAddressFamily);

    server_->setOnConnectionCallback(
        [this](std::weak_ptr<ix::WebSocket> webSocket,
               std::shared_ptr<ix::ConnectionState> connectionState) {
            int clientId = 0;
            {
                std::lock_guard<std::mutex> lock(clientsMutex_);
                clientId = nextClientId_++;
                clientsById_[clientId] = ClientEntry{webSocket, connectionState};
                idByConnection_[connectionState] = clientId;
            }
            std::cout << "[ws] client " << clientId << " connected from "
                      << connectionState->getRemoteIp() << "\n";
        });

    server_->setOnClientMessageCallback(
        [this](std::shared_ptr<ix::ConnectionState> connectionState,
               ix::WebSocket& webSocket,
               const ix::WebSocketMessagePtr& message) {
            HandleMessage(connectionState, webSocket, message);
        });

    const auto result = server_->listen();
    if (!result.first) {
        std::cerr << "[ws] failed to listen on port " << port << ": " << result.second << "\n";
        server_.reset();
        return false;
    }

    server_->start();
    std::cout << "[ws] listening on port " << port << "\n";
    return true;
}

void WsListener::Stop() {
    if (server_) {
        server_->stop();
        server_.reset();
    }
    std::lock_guard<std::mutex> lock(clientsMutex_);
    clientsById_.clear();
    idByConnection_.clear();
}

bool WsListener::SendTo(int clientId, const Message& message) {
    std::shared_ptr<ix::WebSocket> socket;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto it = clientsById_.find(clientId);
        if (it == clientsById_.end()) {
            return false;
        }
        socket = it->second.socket.lock();
    }

    if (!socket) {
        return false;
    }

    const std::string json = SerializeMessage(message);
    return socket->sendText(json).success;
}

void WsListener::HandleMessage(const std::shared_ptr<ix::ConnectionState>& connectionState,
                               ix::WebSocket& webSocket,
                               const ix::WebSocketMessagePtr& message) {
    (void)webSocket;

    if (!message) {
        return;
    }

    if (message->type == ix::WebSocketMessageType::Close) {
        int clientId = 0;
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            auto it = idByConnection_.find(connectionState);
            if (it != idByConnection_.end()) {
                clientId = it->second;
                clientsById_.erase(clientId);
                idByConnection_.erase(it);
            }
        }

        if (clientId != 0) {
            std::cout << "[ws] client " << clientId << " disconnected\n";
            if (onDisconnect_) {
                onDisconnect_(clientId);
            }
        }
        return;
    }

    if (message->type != ix::WebSocketMessageType::Message) {
        return;
    }

    int clientId = 0;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto it = idByConnection_.find(connectionState);
        if (it != idByConnection_.end()) {
            clientId = it->second;
        }
    }

    if (clientId == 0 || !onMessage_) {
        return;
    }

    std::optional<Message> parsed = DeserializeMessage(message->str);
    if (parsed) {
        onMessage_(clientId, *parsed);
    }
}

}  // namespace net
