#include "game_client.hpp"

#include <algorithm>
#include <chrono>

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

    auto onMessage = [this](const Message& message) { HandleMessage(message); };
    if (!tcpClient_.Connect(host, port, onMessage)) {
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
    SetState(ClientConnectionState::Connecting, "Connecting via WebSocket...");

    auto onMessage = [this](const Message& message) { HandleMessage(message); };
    if (!wsClient_.Connect(wsUrl, onMessage)) {
        SetState(ClientConnectionState::Disconnected, "WebSocket setup failed");
        return false;
    }

    wsClient_.Send(MakeJoinRequest(playerName_));
    return true;
}

void GameClient::Disconnect() {
    tcpClient_.Disconnect();
    wsClient_.Disconnect();
    localPlayerId_ = 0;
    players_.clear();
    serverTick_ = 0;
    pingMs_ = 0;
    SetState(ClientConnectionState::Disconnected);
}

void GameClient::Update() {
    if (useWebSocket_) {
        wsClient_.Poll();
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

void GameClient::SendInput(const PlayerInput& input) {
    if (state_ != ClientConnectionState::Joined) {
        return;
    }

    const Message message = MakePlayerInput(input);
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
            SetState(ClientConnectionState::Joined, "Joined game");
            break;
        case MessageType::JoinRejected:
            SetState(ClientConnectionState::Rejected, message.joinRejected.reason);
            Disconnect();
            break;
        case MessageType::WorldState:
            serverTick_ = message.worldState.tick;
            players_ = message.worldState.players;
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
