#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/protocol.hpp"
#include "server/tcp_listener.hpp"
#include "server/ws_listener.hpp"

namespace net {

enum class TransportKind {
    Tcp,
    WebSocket,
};

struct ConnectedClient {
    int id = 0;
    TransportKind transport = TransportKind::Tcp;
    std::string name;
    bool hasJoined = false;
    bool hasMoveTarget = false;
    int targetCol = 0;
    int targetRow = 0;
    std::vector<std::pair<int, int>> movePath;
    size_t pathIndex = 0;
};

class GameServer {
public:
    bool Start(uint16_t tcpPort, uint16_t wsPort);
    void Stop();
    void Run();

private:
    struct IncomingMessage {
        int clientId = 0;
        TransportKind transport = TransportKind::Tcp;
        Message message;
    };

    struct PendingDisconnect {
        int clientId = 0;
        TransportKind transport = TransportKind::Tcp;
    };

    void EnqueueMessage(int clientId, TransportKind transport, const Message& message);
    void EnqueueDisconnect(int clientId, TransportKind transport);
    void ProcessMessages();
    void ProcessDisconnects();
    void HandleMessage(const IncomingMessage& incoming);
    void HandleDisconnect(int clientId, TransportKind transport);
    void SimulateTick();
    void BroadcastWorldState();
    void BroadcastToAll(const Message& message);
    void RecordAndBroadcastChat(const ChatMessage& entry);
    bool SendToClient(int clientId, TransportKind transport, const Message& message);
    std::vector<PlayerState> BuildPlayerSnapshot() const;
    uint32_t NowMs() const;

    TcpListener tcpListener_;
    WsListener wsListener_;
    std::mutex incomingMutex_;
    std::deque<IncomingMessage> incoming_;
    std::deque<PendingDisconnect> pendingDisconnects_;
    std::unordered_map<int, ConnectedClient> clients_;
    std::unordered_map<int, TransportKind> transportByClientId_;
    std::vector<PlayerState> players_;
    std::vector<ChatMessage> chatHistory_;
    uint32_t tick_ = 0;
    bool running_ = false;
};

}  // namespace net
