#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/config.hpp"
#include "common/enemies.hpp"
#include "common/entity_state.hpp"
#include "common/session.hpp"

namespace net {

enum class MessageType {
    JoinRequest,
    JoinAccepted,
    JoinRejected,
    MoveRequest,
    AttackRequest,
    CancelCombatRequest,
    DisengageRequest,
    RespawnEnemyRequest,
    SetReadyRequest,
    ReturnToHubRequest,
    RejoinArenaRequest,
    WorldState,
    PlayerLeft,
    Ping,
    Pong,
    Chat,
    ChatHistory,
};

struct MoveRequest {
    int col = 0;
    int row = 0;
};

struct AttackRequest {
    int enemyId = 0;
};

struct CancelCombatRequest {};

struct DisengageRequest {};

struct RespawnEnemyRequest {
    int enemyId = kDefaultGoblinId;
};

struct SetReadyRequest {
    bool ready = true;
};

struct ReturnToHubRequest {};

struct RejoinArenaRequest {};

struct SessionSnapshot {
    SessionPhase phase = SessionPhase::HubIdle;
    uint32_t phaseEndsAtTick = 0;
    uint32_t allDeadReturnAtTick = 0;
    uint32_t arenaJoinOpensAtTick = 0;
    uint32_t arenaSessionEndsAtTick = 0;
    std::vector<int> readyPlayerIds;
    int hubPlayerCount = 0;
    int arenaPlayerCount = 0;
};

struct PlayerState {
    int id = 0;
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
    EntityState state = EntityState::Idle;
    uint32_t stateStartTick = 0;
    PlayerAnim anim = PlayerAnim::Idle;
    uint32_t animStartTick = 0;
    bool facingRight = true;
    int hp = 0;
    int shield = 0;
    int targetId = -1;
    int moveTargetCol = -1;
    int moveTargetRow = -1;
    SceneId sceneId = SceneId::Hub;
    bool isReady = false;
    uint32_t arenaRejoinAtTick = 0;
};

struct JoinRequest {
    std::string name;
};

struct JoinAccepted {
    int playerId = 0;
    std::vector<PlayerState> players;
    std::vector<EnemyState> enemies;
    SessionSnapshot session;
};

struct JoinRejected {
    std::string reason;
};

struct PlayerLeft {
    int playerId = 0;
};

struct WorldState {
    uint32_t tick = 0;
    std::vector<PlayerState> players;
    std::vector<EnemyState> enemies;
    SessionSnapshot session;
};

struct PingMessage {
    uint32_t clientTimeMs = 0;
};

struct PongMessage {
    uint32_t clientTimeMs = 0;
    uint32_t serverTimeMs = 0;
};

struct ChatMessage {
    int playerId = 0;
    std::string name;
    std::string text;
};

struct ChatHistoryMessage {
    std::vector<ChatMessage> messages;
};

struct Message {
    MessageType type = MessageType::JoinRequest;
    JoinRequest joinRequest;
    JoinAccepted joinAccepted;
    JoinRejected joinRejected;
    MoveRequest moveRequest;
    AttackRequest attackRequest;
    CancelCombatRequest cancelCombatRequest;
    DisengageRequest disengageRequest;
    RespawnEnemyRequest respawnEnemyRequest;
    SetReadyRequest setReadyRequest;
    ReturnToHubRequest returnToHubRequest;
    RejoinArenaRequest rejoinArenaRequest;
    WorldState worldState;
    PlayerLeft playerLeft;
    PingMessage ping;
    PongMessage pong;
    ChatMessage chat;
    ChatHistoryMessage chatHistory;
};

const char* MessageTypeName(MessageType type);
MessageType ParseMessageType(const std::string& value);

Message MakeJoinRequest(const std::string& name);
Message MakeJoinAccepted(int playerId, const std::vector<PlayerState>& players,
                         const std::vector<EnemyState>& enemies,
                         const SessionSnapshot& session);
Message MakeJoinRejected(const std::string& reason);
Message MakeMoveRequest(int col, int row);
Message MakeAttackRequest(int enemyId);
Message MakeCancelCombatRequest();
Message MakeDisengageRequest();
Message MakeRespawnEnemyRequest(int enemyId = kRespawnAllDeadEnemiesId);
Message MakeSetReadyRequest(bool ready);
Message MakeReturnToHubRequest();
Message MakeRejoinArenaRequest();
Message MakeWorldState(uint32_t tick, const std::vector<PlayerState>& players,
                       const std::vector<EnemyState>& enemies,
                       const SessionSnapshot& session);
Message MakePlayerLeft(int playerId);
Message MakePing(uint32_t clientTimeMs);
Message MakePong(uint32_t clientTimeMs, uint32_t serverTimeMs);
Message MakeChatSend(const std::string& text);
Message MakeChatBroadcast(int playerId, const std::string& name, const std::string& text);
Message MakeChatHistory(const std::vector<ChatMessage>& messages);

std::string SerializeMessage(const Message& message);
std::optional<Message> DeserializeMessage(const std::string& json);

// TCP uses a 4-byte big-endian length prefix before each JSON payload.
std::vector<uint8_t> FrameTcpMessage(const std::string& json);
std::optional<std::string> TryExtractTcpMessage(std::string& buffer);

// WebSocket sends raw JSON (the WebSocket frame already provides boundaries).
inline std::string FrameWsMessage(const std::string& json) { return json; }

}  // namespace net
