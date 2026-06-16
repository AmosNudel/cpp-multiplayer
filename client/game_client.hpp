#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "client/tcp_client.hpp"
#include "client/ws_client.hpp"
#include "common/protocol.hpp"

namespace net {

enum class ClientConnectionState {
    Disconnected,
    Connecting,
    Joined,
    Rejected,
};

class GameClient {
public:
    using StateHandler = std::function<void(ClientConnectionState state, const std::string& detail)>;

    bool ConnectDesktop(const std::string& host, uint16_t port, const std::string& playerName,
                        StateHandler onState);
    bool ConnectWeb(const std::string& wsUrl, const std::string& playerName,
                    StateHandler onState);
    void Disconnect();
    void Update();
    void SendInput(const PlayerInput& input);
    void SendChat(const std::string& text);

    ClientConnectionState GetState() const { return state_; }
    int GetLocalPlayerId() const { return localPlayerId_; }
    const std::vector<PlayerState>& GetPlayers() const { return players_; }
    const std::vector<ChatMessage>& GetChatLog() const { return chatLog_; }
    uint32_t GetServerTick() const { return serverTick_; }
    int GetPingMs() const { return pingMs_; }

private:
    void HandleMessage(const Message& message);
    void SetState(ClientConnectionState state, const std::string& detail = "");

    TcpClient tcpClient_;
    WsClient wsClient_;
    bool useWebSocket_ = false;
    ClientConnectionState state_ = ClientConnectionState::Disconnected;
    StateHandler onState_;
    std::string playerName_;
    int localPlayerId_ = 0;
    std::vector<PlayerState> players_;
    std::vector<ChatMessage> chatLog_;
    uint32_t serverTick_ = 0;
    int pingMs_ = 0;
    uint32_t lastPingSentMs_ = 0;
    bool pendingDisconnect_ = false;
    std::string pendingDetail_;
    std::mutex stateMutex_;
};

}  // namespace net
