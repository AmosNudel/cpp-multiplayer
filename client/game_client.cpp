#include "game_client.hpp"

#include <algorithm>
#include <chrono>

#include "common/config.hpp"

namespace net {
namespace {

uint32_t NowMs() {
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now().time_since_epoch();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

}  // namespace

bool GameClient::ConnectDesktop(const std::string& host, uint16_t port,
                                const std::string& playerName, StateHandler onState) {
    Disconnect();
    useWebSocket_ = false;
    playerName_ = playerName;
    onState_ = std::move(onState);
    SetState(ClientConnectionState::Connecting, "Connecting via TCP...");

    if (!tcpClient_.Connect(host, port)) {
        SetState(ClientConnectionState::Disconnected, "TCP connection failed");
        return false;
    }

    tcpClient_.Send(MakeJoinRequest(playerName_));
    return true;
}

bool GameClient::ConnectWeb(const std::string& wsUrl, const std::string& playerName,
                            StateHandler onState) {
    Disconnect();
    useWebSocket_ = true;
    playerName_ = playerName;
    onState_ = std::move(onState);
    SetState(ClientConnectionState::Connecting, "Connecting via WebSocket to " + wsUrl + "...");

    auto onOpen = [this]() {
        wsClient_.Send(MakeJoinRequest(playerName_));
    };
    auto onError = [this](const std::string& reason) {
        pendingDisconnect_ = true;
        pendingDetail_ = reason;
    };

    if (!wsClient_.Connect(wsUrl, onOpen, onError)) {
        SetState(ClientConnectionState::Disconnected, "WebSocket setup failed");
        return false;
    }

    return true;
}

void GameClient::Disconnect() {
    tcpClient_.Disconnect();
    wsClient_.Disconnect();
    localPlayerId_ = 0;
    players_.clear();
    enemies_.clear();
    chatLog_.clear();
    serverTick_ = 0;
    pingMs_ = 0;
    pendingDisconnect_ = false;
    pendingDetail_.clear();
    SetState(ClientConnectionState::Disconnected);
}

void GameClient::Update() {
    auto onMessage = [this](const Message& message) { HandleMessage(message); };

    if (useWebSocket_) {
        wsClient_.Poll(onMessage);
        if (wsClient_.ConsumeConnectionLost() && state_ == ClientConnectionState::Joined) {
            pendingDisconnect_ = true;
            pendingDetail_ = "Connection lost";
        }
    } else {
        tcpClient_.Poll(onMessage);
        if (tcpClient_.ConsumeConnectionLost() && state_ == ClientConnectionState::Joined) {
            pendingDisconnect_ = true;
            pendingDetail_ = "Connection lost";
        }
    }

    if (pendingDisconnect_) {
        const bool wasRejected = (state_ == ClientConnectionState::Rejected);
        const std::string detail = pendingDetail_;
        pendingDisconnect_ = false;
        pendingDetail_.clear();

        tcpClient_.Disconnect();
        wsClient_.Disconnect();
        localPlayerId_ = 0;
        players_.clear();
        enemies_.clear();
        chatLog_.clear();
        serverTick_ = 0;
        pingMs_ = 0;

        if (wasRejected) {
            SetState(ClientConnectionState::Rejected, detail);
        } else {
            SetState(ClientConnectionState::Disconnected,
                     detail.empty() ? "Disconnected" : detail);
        }
        return;
    }

    if (state_ != ClientConnectionState::Joined) {
        return;
    }

    const uint32_t now = NowMs();
    if (now - lastPingSentMs_ > 2000) {
        lastPingSentMs_ = now;
        const Message ping = MakePing(now);
        if (useWebSocket_) {
            wsClient_.Send(ping);
        } else {
            tcpClient_.Send(ping);
        }
    }
}

void GameClient::SendMoveRequest(int col, int row) {
    if (state_ != ClientConnectionState::Joined) {
        return;
    }

    const Message message = MakeMoveRequest(col, row);
    if (useWebSocket_) {
        wsClient_.Send(message);
    } else {
        tcpClient_.Send(message);
    }
}

void GameClient::SendChat(const std::string& text) {
    if (state_ != ClientConnectionState::Joined || text.empty()) {
        return;
    }

    const Message message = MakeChatSend(text);
    if (useWebSocket_) {
        wsClient_.Send(message);
    } else {
        tcpClient_.Send(message);
    }
}

void GameClient::HandleMessage(const Message& message) {
    switch (message.type) {
        case MessageType::JoinAccepted:
            localPlayerId_ = message.joinAccepted.playerId;
            players_ = message.joinAccepted.players;
            enemies_ = message.joinAccepted.enemies;
            chatLog_.clear();
            SetState(ClientConnectionState::Joined, "Joined game");
            break;
        case MessageType::JoinRejected:
            pendingDisconnect_ = true;
            pendingDetail_ = message.joinRejected.reason;
            SetState(ClientConnectionState::Rejected, message.joinRejected.reason);
            break;
        case MessageType::WorldState:
            serverTick_ = message.worldState.tick;
            players_ = message.worldState.players;
            enemies_ = message.worldState.enemies;
            break;
        case MessageType::PlayerLeft:
            players_.erase(
                std::remove_if(players_.begin(), players_.end(),
                               [&](const PlayerState& player) {
                                   return player.id == message.playerLeft.playerId;
                               }),
                players_.end());
            break;
        case MessageType::Pong: {
            const uint32_t now = NowMs();
            pingMs_ = static_cast<int>(now - message.pong.clientTimeMs);
            break;
        }
        case MessageType::Chat: {
            if (message.chat.text.empty()) {
                break;
            }
            chatLog_.push_back(message.chat);
            if (static_cast<int>(chatLog_.size()) > kMaxChatHistory) {
                chatLog_.erase(chatLog_.begin());
            }
            break;
        }
        case MessageType::ChatHistory:
            chatLog_ = message.chatHistory.messages;
            break;
        default:
            break;
    }
}

void GameClient::SetState(ClientConnectionState state, const std::string& detail) {
    state_ = state;
    if (onState_) {
        onState_(state, detail);
    }
}

}  // namespace net
