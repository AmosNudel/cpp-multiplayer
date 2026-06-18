#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/protocol.hpp"
#include "common/session.hpp"
#include "server/tcp_listener.hpp"
#include "server/ws_listener.hpp"

namespace net {

enum class TransportKind {
    Tcp,
    WebSocket,
};

enum class PlayerComboPhase : uint8_t {
    None,
    Attack1,
    PauseAfter1,
    Attack2,
    PauseAfter2,
    Attack3,
};

enum class BossComboPhase : uint8_t {
    None,
    Swing1,
    PauseAfter1,
    Swing2,
    PauseAfter2,
    Swing3,
};

struct EnemyMovementState {
    bool hasMoveTarget = false;
    std::vector<std::pair<int, int>> movePath;
    size_t pathIndex = 0;
    std::vector<std::pair<int, int>> patrolWaypoints;
    size_t patrolWaypointIndex = 0;
    bool chasingPlayer = false;
    int chaseTargetId = -1;
    uint32_t patrolIdleUntilTick = 0;
    BossComboPhase bossComboPhase = BossComboPhase::None;
    uint32_t bossComboPhaseStartTick = 0;
    bool bossComboSwingDamageDealt = false;
    int bossComboPattern[3] = {1, 1, 1};
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
    int pendingAttackEnemyId = -1;
    PlayerComboPhase comboPhase = PlayerComboPhase::None;
    uint32_t comboPhaseStartTick = 0;
    bool comboSwingDamageDealt = false;
    uint32_t lastAttackTick = 0;
    bool hasDisengageTarget = false;
    float disengageStartX = 0.0f;
    float disengageStartY = 0.0f;
    float disengageTargetX = 0.0f;
    float disengageTargetY = 0.0f;
    int pendingMoveCol = -1;
    int pendingMoveRow = -1;
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
    void UpdateSession();
    void HandleSetReady(int clientId, bool ready);
    void HandleSetArenaReset(int clientId, bool selected);
    void HandleReturnToHub(int clientId);
    void HandleRejoinArena(int clientId);
    void HandleRespawnInArena(int clientId);
    void ReturnAllArenaPlayersToHub();
    bool CanPlayerRejoinArena(const PlayerState& player) const;
    bool CanPlayerRespawnInArena(const PlayerState& player) const;
    void StartArena();
    void EndArena();
    void ResetArena();
    void BroadcastWorldState();
    void BroadcastToAll(const Message& message);
    void RecordAndBroadcastChat(const ChatMessage& entry);
    bool SendToClient(int clientId, TransportKind transport, const Message& message);
    std::vector<PlayerState> BuildPlayerSnapshot(SceneId scene) const;
    SessionSnapshot BuildSessionSnapshot() const;
    WorldState BuildWorldStateForClient(int clientId) const;
    int CountPlayersInScene(SceneId scene) const;
    int CountReadyHubPlayers() const;
    int CountArenaResetHubPlayers() const;
    void ClearAllReady();
    void ClearAllArenaReset();
    bool HasActiveArenaSession() const;
    void ResetPlayerForScene(PlayerState& player, ConnectedClient* client, SceneId scene,
                             int spawnCol, int spawnRow);
    uint32_t NowMs() const;

    TcpListener tcpListener_;
    WsListener wsListener_;
    std::mutex incomingMutex_;
    std::deque<IncomingMessage> incoming_;
    std::deque<PendingDisconnect> pendingDisconnects_;
    std::unordered_map<int, ConnectedClient> clients_;
    std::unordered_map<int, TransportKind> transportByClientId_;
    std::vector<PlayerState> players_;
    std::vector<EnemyState> enemies_;
    std::unordered_map<int, EnemyMovementState> enemyMovement_;
    std::vector<ChatMessage> chatHistory_;
    SessionPhase sessionPhase_ = SessionPhase::HubIdle;
    uint32_t sessionPhaseEndsAtTick_ = 0;
    uint32_t allDeadReturnAtTick_ = 0;
    uint32_t arenaJoinOpensAtTick_ = 0;
    uint32_t arenaSessionEndsAtTick_ = 0;
    uint32_t arenaVictoryEndsAtTick_ = 0;
    uint32_t tick_ = 0;
    bool running_ = false;
};

}  // namespace net
