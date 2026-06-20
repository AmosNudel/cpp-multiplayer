#include "game_client.hpp"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

#include "common/config.hpp"

#if defined(PLATFORM_WEB)
#include <emscripten.h>
#endif

namespace net {
namespace {

struct AnimDebugState {
    PlayerAnim anim = PlayerAnim::Idle;
    uint32_t animStartTick = 0;
    EntityState state = EntityState::Idle;
    uint32_t stateStartTick = 0;
};

#if defined(PLATFORM_WEB)
EM_JS(int, JsNetDebugEnabled, (), {
    try {
        if (typeof window === 'undefined') {
            return 0;
        }

        const params = new URLSearchParams(window.location.search || '');
        const query = (params.get('netdebug') || '').toLowerCase();
        if (query === '1' || query === 'true' || query === 'yes') {
            return 1;
        }

        const local = window.localStorage
            ? (window.localStorage.getItem('NET_DEBUG_WS') || '').toLowerCase()
            : '';
        if (local === '1' || local === 'true' || local === 'yes') {
            return 1;
        }

        if (window.NET_DEBUG_WS === true || window.NET_DEBUG_WS === 1) {
            return 1;
        }
    } catch (e) {
    }
    return 0;
});
#endif

bool IsNetworkDebugEnabled() {
    static int cached = -1;
    if (cached >= 0) {
        return cached == 1;
    }

    const char* value = std::getenv("NET_DEBUG_WS");
    if (value != nullptr && value[0] != '\0') {
        cached = (value[0] == '1' || value[0] == 't' || value[0] == 'T' || value[0] == 'y' ||
                  value[0] == 'Y')
                     ? 1
                     : 0;
        return cached == 1;
    }

#if defined(PLATFORM_WEB)
    cached = JsNetDebugEnabled() != 0 ? 1 : 0;
#else
    cached = 0;
#endif
    return cached == 1;
}

void DebugNetLog(const char* format, ...) {
    if (!IsNetworkDebugEnabled()) {
        return;
    }

    std::va_list args;
    va_start(args, format);
    std::fputs("[net-debug] ", stdout);
    std::vfprintf(stdout, format, args);
    std::fputc('\n', stdout);
    va_end(args);
}

uint32_t NowMs() {
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now().time_since_epoch();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

}  // namespace

bool GameClient::ConnectDesktop(const std::string& host, uint16_t port,
                                const std::string& playerName, StateHandler onState) {
#if defined(PLATFORM_WEB)
    (void)host;
    (void)port;
    (void)playerName;
    (void)onState;
    return false;
#else
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
#endif
}

bool GameClient::ConnectWeb(const std::string& wsUrl, const std::string& playerName,
                            StateHandler onState) {
    Disconnect();
    useWebSocket_ = true;
    playerName_ = playerName;
    onState_ = std::move(onState);
    SetState(ClientConnectionState::Connecting, "Connecting via WebSocket to " + wsUrl + "...");
    DebugNetLog("connect ws url=%s player=%s", wsUrl.c_str(), playerName.c_str());

    auto onOpen = [this]() {
        DebugNetLog("ws open, sending join request player=%s", playerName_.c_str());
        wsClient_.Send(MakeJoinRequest(playerName_));
    };
    auto onError = [this](const std::string& reason) {
        DebugNetLog("ws error reason=%s", reason.c_str());
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
#if !defined(PLATFORM_WEB)
    tcpClient_.Disconnect();
#endif
    wsClient_.Disconnect();
    localPlayerId_ = 0;
    players_.clear();
    enemies_.clear();
    chatLog_.clear();
    session_ = {};
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
#if !defined(PLATFORM_WEB)
        tcpClient_.Poll(onMessage);
        if (tcpClient_.ConsumeConnectionLost() && state_ == ClientConnectionState::Joined) {
            pendingDisconnect_ = true;
            pendingDetail_ = "Connection lost";
        }
#endif
    }

    if (pendingDisconnect_) {
        const bool wasRejected = (state_ == ClientConnectionState::Rejected);
        const std::string detail = pendingDetail_;
        pendingDisconnect_ = false;
        pendingDetail_.clear();

#if !defined(PLATFORM_WEB)
        tcpClient_.Disconnect();
#endif
        wsClient_.Disconnect();
        localPlayerId_ = 0;
        players_.clear();
        enemies_.clear();
        chatLog_.clear();
        session_ = {};
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
        SendActive(ping);
    }
}

void GameClient::SendMoveRequest(int col, int row) {
    if (state_ != ClientConnectionState::Joined) {
        return;
    }

    SendActive(MakeMoveRequest(col, row));
}

void GameClient::SendAttackRequest(int enemyId) {
    if (state_ != ClientConnectionState::Joined || enemyId < 0) {
        return;
    }

    SendActive(MakeAttackRequest(enemyId));
}

void GameClient::SendCancelCombat() {
    if (state_ != ClientConnectionState::Joined) {
        return;
    }

    SendActive(MakeCancelCombatRequest());
}

void GameClient::SendDisengage() {
    if (state_ != ClientConnectionState::Joined) {
        return;
    }

    SendActive(MakeDisengageRequest());
}

void GameClient::SendRespawnEnemy(int enemyId) {
    if (state_ != ClientConnectionState::Joined) {
        return;
    }

    SendActive(MakeRespawnEnemyRequest(enemyId));
}

void GameClient::SendSetReady(bool ready) {
    if (state_ != ClientConnectionState::Joined) {
        return;
    }

    SendActive(MakeSetReadyRequest(ready));
}

void GameClient::SendSetArenaReset(bool selected) {
    if (state_ != ClientConnectionState::Joined) {
        return;
    }

    SendActive(MakeSetArenaResetRequest(selected));
}

void GameClient::SendReturnToHub() {
    if (state_ != ClientConnectionState::Joined) {
        return;
    }

    SendActive(MakeReturnToHubRequest());
}

void GameClient::SendRejoinArena() {
    if (state_ != ClientConnectionState::Joined) {
        return;
    }

    SendActive(MakeRejoinArenaRequest());
}

void GameClient::SendRespawnInArena() {
    if (state_ != ClientConnectionState::Joined) {
        return;
    }

    SendActive(MakeRespawnInArenaRequest());
}

void GameClient::SendVoteSkillBranch(SkillBranch branch) {
    if (state_ != ClientConnectionState::Joined) {
        return;
    }

    SendActive(MakeVoteSkillBranchRequest(branch));
}

void GameClient::SendUseSkill(int skillId, int col, int row) {
    if (state_ != ClientConnectionState::Joined) {
        return;
    }

    SendActive(MakeUseSkillRequest(skillId, col, row));
}

void GameClient::SendChat(const std::string& text) {
    if (state_ != ClientConnectionState::Joined || text.empty()) {
        return;
    }

    SendActive(MakeChatSend(text));
}

void GameClient::SendActive(const Message& message) {
    if (useWebSocket_) {
        wsClient_.Send(message);
        return;
    }
#if !defined(PLATFORM_WEB)
    tcpClient_.Send(message);
#endif
}

void GameClient::HandleMessage(const Message& message) {
    static uint32_t worldStateCount = 0;
    static std::unordered_map<int, AnimDebugState> playerAnimDebug;
    static std::unordered_map<int, AnimDebugState> enemyAnimDebug;

    switch (message.type) {
        case MessageType::JoinAccepted:
            DebugNetLog("join accepted playerId=%d players=%zu enemies=%zu",
                        message.joinAccepted.playerId,
                        message.joinAccepted.players.size(),
                        message.joinAccepted.enemies.size());
            localPlayerId_ = message.joinAccepted.playerId;
            players_ = message.joinAccepted.players;
            enemies_ = message.joinAccepted.enemies;
            session_ = message.joinAccepted.session;
            chatLog_.clear();
            SetState(ClientConnectionState::Joined, "Joined game");
            break;
        case MessageType::JoinRejected:
            DebugNetLog("join rejected reason=%s", message.joinRejected.reason.c_str());
            pendingDisconnect_ = true;
            pendingDetail_ = message.joinRejected.reason;
            SetState(ClientConnectionState::Rejected, message.joinRejected.reason);
            break;
        case MessageType::WorldState:
            ++worldStateCount;
            if (serverTick_ > 0) {
                if (message.worldState.tick < serverTick_) {
                    DebugNetLog("world tick regressed prev=%u curr=%u", serverTick_,
                                message.worldState.tick);
                } else {
                    const uint32_t delta = message.worldState.tick - serverTick_;
                    if (delta == 0 && (worldStateCount % 40 == 0)) {
                        DebugNetLog("world duplicate tick=%u count=%u", message.worldState.tick,
                                    worldStateCount);
                    }
                    if (delta > 3) {
                        DebugNetLog("world tick gap=%u prev=%u curr=%u players=%zu enemies=%zu",
                                    delta, serverTick_, message.worldState.tick,
                                    message.worldState.players.size(),
                                    message.worldState.enemies.size());
                    }
                }
            }
            if (worldStateCount % 20 == 0) {
                DebugNetLog("world stream count=%u tick=%u players=%zu enemies=%zu ping=%dms",
                            worldStateCount, message.worldState.tick,
                            message.worldState.players.size(),
                            message.worldState.enemies.size(), pingMs_);
            }

            if (IsNetworkDebugEnabled()) {
                for (const PlayerState& player : message.worldState.players) {
                    AnimDebugState& prev = playerAnimDebug[player.id];
                    if (prev.animStartTick > 0 &&
                        (player.anim != prev.anim || player.animStartTick != prev.animStartTick)) {
                        const uint32_t restartDelta =
                            player.animStartTick >= prev.animStartTick
                                ? (player.animStartTick - prev.animStartTick)
                                : 0;
                        DebugNetLog(
                            "player anim id=%d %s(%u)->%s(%u) state=%s tick=%u deltaStart=%u",
                            player.id,
                            PlayerAnimName(prev.anim), prev.animStartTick,
                            PlayerAnimName(player.anim), player.animStartTick,
                            EntityStateName(player.state),
                            message.worldState.tick,
                            restartDelta);
                    }
                    if (prev.stateStartTick > 0 &&
                        (player.state != prev.state || player.stateStartTick != prev.stateStartTick)) {
                        DebugNetLog(
                            "player state id=%d %s(%u)->%s(%u) anim=%s tick=%u",
                            player.id,
                            EntityStateName(prev.state), prev.stateStartTick,
                            EntityStateName(player.state), player.stateStartTick,
                            PlayerAnimName(player.anim),
                            message.worldState.tick);
                    }
                    prev.anim = player.anim;
                    prev.animStartTick = player.animStartTick;
                    prev.state = player.state;
                    prev.stateStartTick = player.stateStartTick;
                }

                for (const EnemyState& enemy : message.worldState.enemies) {
                    AnimDebugState& prev = enemyAnimDebug[enemy.id];
                    if (prev.animStartTick > 0 &&
                        (enemy.anim != prev.anim || enemy.animStartTick != prev.animStartTick)) {
                        const uint32_t restartDelta =
                            enemy.animStartTick >= prev.animStartTick
                                ? (enemy.animStartTick - prev.animStartTick)
                                : 0;
                        DebugNetLog(
                            "enemy anim id=%d %s(%u)->%s(%u) state=%s tick=%u deltaStart=%u",
                            enemy.id,
                            PlayerAnimName(prev.anim), prev.animStartTick,
                            PlayerAnimName(enemy.anim), enemy.animStartTick,
                            EntityStateName(enemy.state),
                            message.worldState.tick,
                            restartDelta);
                    }
                    if (prev.stateStartTick > 0 &&
                        (enemy.state != prev.state || enemy.stateStartTick != prev.stateStartTick)) {
                        DebugNetLog(
                            "enemy state id=%d %s(%u)->%s(%u) anim=%s tick=%u",
                            enemy.id,
                            EntityStateName(prev.state), prev.stateStartTick,
                            EntityStateName(enemy.state), enemy.stateStartTick,
                            PlayerAnimName(enemy.anim),
                            message.worldState.tick);
                    }
                    prev.anim = enemy.anim;
                    prev.animStartTick = enemy.animStartTick;
                    prev.state = enemy.state;
                    prev.stateStartTick = enemy.stateStartTick;
                }
            }

            serverTick_ = message.worldState.tick;
            players_ = message.worldState.players;
            enemies_ = message.worldState.enemies;
            session_ = message.worldState.session;
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
