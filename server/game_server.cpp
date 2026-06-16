#include "game_server.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <thread>

#include "common/combat.hpp"
#include "common/config.hpp"
#include "common/enemies.hpp"
#include "common/entity_state.hpp"
#include "common/grid.hpp"
#include "common/grid_map.hpp"
#include "common/pathfinding.hpp"

namespace net {
namespace {

TransportKind TransportForClientId(int clientId) {
    return clientId >= 10000 ? TransportKind::WebSocket : TransportKind::Tcp;
}

std::string SanitizeChatText(const std::string& text) {
    std::string trimmed;
    trimmed.reserve(text.size());

    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }

    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    for (size_t i = start; i < end && trimmed.size() < kMaxChatLength; ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (ch >= 32 && ch <= 126) {
            trimmed.push_back(static_cast<char>(ch));
        }
    }

    return trimmed;
}

void UpdateFacingFromDirection(PlayerState& player, float dx, float dy) {
    (void)dy;
    if (dx > 0.01f) {
        player.facingRight = true;
    } else if (dx < -0.01f) {
        player.facingRight = false;
    }
}

void UpdateEnemyFacing(EnemyState& enemy, float dx, float dy) {
    (void)dy;
    if (dx > 0.01f) {
        enemy.facingRight = true;
    } else if (dx < -0.01f) {
        enemy.facingRight = false;
    }
}

void SetMoveFacingToward(PlayerState& player, float targetX, float targetY) {
    UpdateFacingFromDirection(player, targetX - player.x, targetY - player.y);
}

void ClearPlayerMove(ConnectedClient& client, PlayerState& player) {
    client.hasMoveTarget = false;
    client.movePath.clear();
    client.pathIndex = 0;
    player.moveTargetCol = -1;
    player.moveTargetRow = -1;
}

bool StartPlayerPath(ConnectedClient& client, PlayerState& player, const GridMap& map,
                     int goalCol, int goalRow, uint32_t tick) {
    const int startCol = WorldToCellCol(player.x);
    const int startRow = WorldToCellRow(player.y);
    std::vector<GridPoint> path = FindPath(map, startCol, startRow, goalCol, goalRow);
    if (path.empty()) {
        return false;
    }

    client.movePath = std::move(path);
    client.pathIndex = 0;
    if (client.movePath.size() > 1 && client.movePath[0].first == startCol &&
        client.movePath[0].second == startRow) {
        client.pathIndex = 1;
    }
    if (client.pathIndex >= client.movePath.size()) {
        ClearPlayerMove(client, player);
        return false;
    }

    client.hasMoveTarget = true;
    client.targetCol = goalCol;
    client.targetRow = goalRow;
    player.moveTargetCol = goalCol;
    player.moveTargetRow = goalRow;

    const auto& firstWaypoint = client.movePath[client.pathIndex];
    SetMoveFacingToward(player, CellCenterX(firstWaypoint.first),
                        CellCenterY(firstWaypoint.second));
    TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                     EntityState::Moving, tick);
    return true;
}

PlayerState* FindPlayer(std::vector<PlayerState>& players, int playerId) {
    for (PlayerState& player : players) {
        if (player.id == playerId) {
            return &player;
        }
    }
    return nullptr;
}

const PlayerState* FindPlayerConst(const std::vector<PlayerState>& players, int playerId) {
    for (const PlayerState& player : players) {
        if (player.id == playerId) {
            return &player;
        }
    }
    return nullptr;
}

EnemyState* FindEnemy(std::vector<EnemyState>& enemies, int enemyId) {
    for (EnemyState& enemy : enemies) {
        if (enemy.id == enemyId) {
            return &enemy;
        }
    }
    return nullptr;
}

void EndPlayerCombat(PlayerState& player, std::vector<EnemyState>& enemies, uint32_t tick) {
    if (player.targetId >= 0) {
        if (EnemyState* enemy = FindEnemy(enemies, player.targetId)) {
            enemy->targetId = -1;
            if (IsAlive(enemy->state) && enemy->state == EntityState::Combat) {
                TransitionEntity(enemy->state, enemy->stateStartTick, enemy->anim,
                                 enemy->animStartTick, EntityState::Idle, tick);
            }
        }
    }
    player.targetId = -1;
    if (IsAlive(player.state) && player.state == EntityState::Combat) {
        TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                         EntityState::Idle, tick);
    }
}

void ResetPlayerCombo(ConnectedClient& client) {
    client.comboPhase = PlayerComboPhase::None;
    client.comboPhaseStartTick = 0;
    client.comboSwingDamageDealt = false;
}

void StartPlayerComboPhase(PlayerState& player, ConnectedClient& client, PlayerComboPhase phase,
                           PlayerAnim anim, uint32_t tick) {
    client.comboPhase = phase;
    client.comboPhaseStartTick = tick;
    client.comboSwingDamageDealt = false;
    SetEntityAnim(player.anim, player.animStartTick, anim, tick);
}

void BeginCombat(PlayerState& player, EnemyState& enemy, ConnectedClient& client, uint32_t tick) {
    player.targetId = enemy.id;
    enemy.targetId = player.id;
    TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                     EntityState::Combat, tick);
    TransitionEntity(enemy.state, enemy.stateStartTick, enemy.anim, enemy.animStartTick,
                     EntityState::Combat, tick);
    UpdateFacingFromDirection(player, enemy.x - player.x, enemy.y - player.y);
    UpdateEnemyFacing(enemy, player.x - enemy.x, player.y - enemy.y);
    StartPlayerComboPhase(player, client, PlayerComboPhase::Attack1, PlayerAnim::Attack1, tick);
}

void CancelPlayerCombat(PlayerState& player, ConnectedClient& client,
                        std::vector<EnemyState>& enemies, uint32_t tick) {
    client.pendingAttackEnemyId = -1;
    ResetPlayerCombo(client);
    ClearPlayerMove(client, player);
    EndPlayerCombat(player, enemies, tick);
}

void ApplyDamageToEnemy(EnemyState& enemy, int damage, uint32_t tick) {
    if (!IsAlive(enemy.state)) {
        return;
    }

    enemy.hp -= damage;
    if (enemy.hp <= 0) {
        enemy.hp = 0;
        TransitionEntity(enemy.state, enemy.stateStartTick, enemy.anim, enemy.animStartTick,
                         EntityState::Dead, tick);
        return;
    }

    TransitionEntity(enemy.state, enemy.stateStartTick, enemy.anim, enemy.animStartTick,
                     EntityState::Hit, tick);
}

void ApplyDamageToPlayer(PlayerState& player, int damage, uint32_t tick) {
    if (!IsAlive(player.state)) {
        return;
    }

    player.hp -= damage;
    if (player.hp <= 0) {
        player.hp = 0;
        TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                         EntityState::Dead, tick);
        return;
    }

    TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                     EntityState::Hit, tick);
}

void TryApplyComboSwingDamage(PlayerState& player, ConnectedClient& client, EnemyState& enemy,
                              uint32_t tick) {
    if (!IsAttackAnim(player.anim) || client.comboSwingDamageDealt) {
        return;
    }

    const int damageFrame = AttackDamageFrame(player.anim);
    if (damageFrame < 0) {
        return;
    }

    const int currentFrame = CurrentAnimFrame(player.anim, tick, player.animStartTick);
    if (currentFrame != damageFrame) {
        return;
    }

    client.comboSwingDamageDealt = true;
    ApplyDamageToEnemy(enemy, kPlayerAttackDamage, tick);
    if (!IsAlive(enemy.state)) {
        player.targetId = -1;
        ResetPlayerCombo(client);
        TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                         EntityState::Idle, tick);
    }
}

void UpdatePlayerCombo(PlayerState& player, ConnectedClient& client,
                       std::vector<EnemyState>& enemies, uint32_t tick) {
    EnemyState* enemy = FindEnemy(enemies, player.targetId);
    if (enemy == nullptr || !IsAlive(enemy->state) ||
        !IsInMeleeRange(player.x, player.y, enemy->x, enemy->y)) {
        CancelPlayerCombat(player, client, enemies, tick);
        return;
    }

    UpdateFacingFromDirection(player, enemy->x - player.x, enemy->y - player.y);
    TryApplyComboSwingDamage(player, client, *enemy, tick);

    if (!IsAlive(enemy->state)) {
        return;
    }

    switch (client.comboPhase) {
        case PlayerComboPhase::Attack1:
            if (IsAnimFinished(PlayerAnim::Attack1, tick, player.animStartTick)) {
                StartPlayerComboPhase(player, client, PlayerComboPhase::PauseAfter1,
                                      PlayerAnim::Idle, tick);
            }
            break;
        case PlayerComboPhase::PauseAfter1:
            if (tick - client.comboPhaseStartTick >= static_cast<uint32_t>(kComboPauseTicks)) {
                StartPlayerComboPhase(player, client, PlayerComboPhase::Attack2,
                                      PlayerAnim::Attack2, tick);
            }
            break;
        case PlayerComboPhase::Attack2:
            if (IsAnimFinished(PlayerAnim::Attack2, tick, player.animStartTick)) {
                StartPlayerComboPhase(player, client, PlayerComboPhase::PauseAfter2,
                                      PlayerAnim::Idle, tick);
            }
            break;
        case PlayerComboPhase::PauseAfter2:
            if (tick - client.comboPhaseStartTick >= static_cast<uint32_t>(kComboPauseTicks)) {
                StartPlayerComboPhase(player, client, PlayerComboPhase::Attack3,
                                      PlayerAnim::Attack3, tick);
            }
            break;
        case PlayerComboPhase::Attack3:
            if (IsAnimFinished(PlayerAnim::Attack3, tick, player.animStartTick)) {
                StartPlayerComboPhase(player, client, PlayerComboPhase::Attack1,
                                      PlayerAnim::Attack1, tick);
            }
            break;
        case PlayerComboPhase::None:
            StartPlayerComboPhase(player, client, PlayerComboPhase::Attack1,
                                  PlayerAnim::Attack1, tick);
            break;
    }
}

bool TryPerformEnemyAttack(EnemyState& enemy, std::vector<PlayerState>& players, uint32_t tick) {
    if (enemy.state != EntityState::Combat || enemy.targetId < 0) {
        return false;
    }

    const PlayerState* player = FindPlayerConst(players, enemy.targetId);
    if (player == nullptr || !IsAlive(player->state) || player->state == EntityState::Hit) {
        return false;
    }

    if (!IsInMeleeRange(enemy.x, enemy.y, player->x, player->y)) {
        return false;
    }

    if (tick - enemy.lastAttackTick < static_cast<uint32_t>(kGoblinAttackCooldownTicks)) {
        return false;
    }

    enemy.lastAttackTick = tick;
    UpdateEnemyFacing(enemy, player->x - enemy.x, player->y - enemy.y);
    SetEntityAnim(enemy.anim, enemy.animStartTick, PlayerAnim::Attack1, tick);

    if (PlayerState* mutablePlayer = FindPlayer(players, enemy.targetId)) {
        ApplyDamageToPlayer(*mutablePlayer, kGoblinAttackDamage, tick);
    }
    return true;
}

void TryCompletePendingEngage(ConnectedClient& client, PlayerState& player,
                              std::vector<EnemyState>& enemies, uint32_t tick) {
    if (client.pendingAttackEnemyId < 0) {
        return;
    }

    const int enemyId = client.pendingAttackEnemyId;
    client.pendingAttackEnemyId = -1;

    EnemyState* enemy = FindEnemy(enemies, enemyId);
    if (enemy == nullptr || !IsAlive(enemy->state)) {
        EndPlayerCombat(player, enemies, tick);
        TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                         EntityState::Idle, tick);
        return;
    }

    if (!IsInMeleeRange(player.x, player.y, enemy->x, enemy->y)) {
        TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                         EntityState::Idle, tick);
        return;
    }

    BeginCombat(player, *enemy, client, tick);
}

bool StepPlayerMovement(PlayerState& player, ConnectedClient& client, uint32_t tick) {
    if (!client.hasMoveTarget || client.pathIndex >= client.movePath.size()) {
        return false;
    }

    float remainingStep = kPlayerSpeed * kTickDuration;
    static constexpr float kArriveEpsilon = 0.5f;
    bool moved = false;

    while (remainingStep > 0.0f && client.pathIndex < client.movePath.size()) {
        const auto& waypoint = client.movePath[client.pathIndex];
        const float targetX = CellCenterX(waypoint.first);
        const float targetY = CellCenterY(waypoint.second);
        float dx = targetX - player.x;
        float dy = targetY - player.y;
        const float dist = std::sqrt(dx * dx + dy * dy);

        if (dist <= kArriveEpsilon) {
            player.x = targetX;
            player.y = targetY;
            ++client.pathIndex;
            moved = true;
            if (client.pathIndex >= client.movePath.size()) {
                ClearPlayerMove(client, player);
            }
            continue;
        }

        dx /= dist;
        dy /= dist;
        UpdateFacingFromDirection(player, dx, dy);

        if (dist <= remainingStep) {
            player.x = targetX;
            player.y = targetY;
            remainingStep -= dist;
            ++client.pathIndex;
            moved = true;
            if (client.pathIndex >= client.movePath.size()) {
                ClearPlayerMove(client, player);
            }
        } else {
            player.x += dx * remainingStep;
            player.y += dy * remainingStep;
            remainingStep = 0.0f;
            moved = true;
        }
    }

    if (moved && player.state == EntityState::Moving) {
        SetEntityAnim(player.anim, player.animStartTick, PlayerAnim::Run, tick);
    }

    return moved;
}

void UpdatePlayerEntity(PlayerState& player, ConnectedClient& client,
                        std::vector<EnemyState>& enemies, uint32_t tick) {
    if (player.state == EntityState::Dead) {
        ClearPlayerMove(client, player);
        return;
    }

    if (player.state == EntityState::Hit) {
        ResetPlayerCombo(client);
        ClearPlayerMove(client, player);
        if (tick - player.stateStartTick >= static_cast<uint32_t>(kHitStunTicks)) {
            if (player.hp <= 0) {
                TransitionEntity(player.state, player.stateStartTick, player.anim,
                                 player.animStartTick, EntityState::Dead, tick);
            } else if (player.targetId >= 0) {
                EnemyState* enemy = FindEnemy(enemies, player.targetId);
                if (enemy != nullptr && IsAlive(enemy->state) &&
                    IsInMeleeRange(player.x, player.y, enemy->x, enemy->y)) {
                    TransitionEntity(player.state, player.stateStartTick, player.anim,
                                     player.animStartTick, EntityState::Combat, tick);
                    StartPlayerComboPhase(player, client, PlayerComboPhase::Attack1,
                                          PlayerAnim::Attack1, tick);
                } else {
                    EndPlayerCombat(player, enemies, tick);
                    TransitionEntity(player.state, player.stateStartTick, player.anim,
                                     player.animStartTick, EntityState::Idle, tick);
                }
            } else {
                TransitionEntity(player.state, player.stateStartTick, player.anim,
                                 player.animStartTick, EntityState::Idle, tick);
            }
        }
        return;
    }

    if (player.state == EntityState::Moving || client.hasMoveTarget) {
        StepPlayerMovement(player, client, tick);
        if (!client.hasMoveTarget) {
            TryCompletePendingEngage(client, player, enemies, tick);
            if (player.state == EntityState::Moving) {
                TransitionEntity(player.state, player.stateStartTick, player.anim,
                                 player.animStartTick, EntityState::Idle, tick);
            }
        }
        return;
    }

    if (player.state == EntityState::Combat) {
        UpdatePlayerCombo(player, client, enemies, tick);
        return;
    }
}

void UpdateEnemyEntity(EnemyState& enemy, std::vector<PlayerState>& players, uint32_t tick) {
    if (enemy.state == EntityState::Dead) {
        return;
    }

    if (enemy.state == EntityState::Hit) {
        if (tick - enemy.stateStartTick >= static_cast<uint32_t>(kHitStunTicks)) {
            if (enemy.hp <= 0) {
                TransitionEntity(enemy.state, enemy.stateStartTick, enemy.anim,
                                 enemy.animStartTick, EntityState::Dead, tick);
            } else if (enemy.targetId >= 0) {
                const PlayerState* player = FindPlayerConst(players, enemy.targetId);
                if (player != nullptr && IsAlive(player->state) &&
                    IsInMeleeRange(enemy.x, enemy.y, player->x, player->y)) {
                    TransitionEntity(enemy.state, enemy.stateStartTick, enemy.anim,
                                     enemy.animStartTick, EntityState::Combat, tick);
                } else {
                    enemy.targetId = -1;
                    TransitionEntity(enemy.state, enemy.stateStartTick, enemy.anim,
                                     enemy.animStartTick, EntityState::Idle, tick);
                }
            } else {
                TransitionEntity(enemy.state, enemy.stateStartTick, enemy.anim,
                                 enemy.animStartTick, EntityState::Idle, tick);
            }
        }
        return;
    }

    if (enemy.state == EntityState::Combat) {
        const PlayerState* player = FindPlayerConst(players, enemy.targetId);
        if (player == nullptr || !IsAlive(player->state) ||
            !IsInMeleeRange(enemy.x, enemy.y, player->x, player->y)) {
            enemy.targetId = -1;
            TransitionEntity(enemy.state, enemy.stateStartTick, enemy.anim, enemy.animStartTick,
                             EntityState::Idle, tick);
            return;
        }

        TryPerformEnemyAttack(enemy, players, tick);
    }
}

void HandlePlayerEngageRequest(PlayerState& player, ConnectedClient& client,
                               std::vector<EnemyState>& enemies, int enemyId,
                               const GridMap& map, uint32_t tick) {
    if (!CanAcceptAttackIntent(player.state)) {
        return;
    }

    EnemyState* enemy = FindEnemy(enemies, enemyId);
    if (enemy == nullptr || !IsAlive(enemy->state)) {
        return;
    }

    EndPlayerCombat(player, enemies, tick);
    ClearPlayerMove(client, player);
    client.pendingAttackEnemyId = enemyId;
    player.targetId = enemyId;

    if (IsInMeleeRange(player.x, player.y, enemy->x, enemy->y)) {
        client.pendingAttackEnemyId = -1;
        BeginCombat(player, *enemy, client, tick);
        return;
    }

    const int playerCol = WorldToCellCol(player.x);
    const int playerRow = WorldToCellRow(player.y);
    const int enemyCol = WorldToCellCol(enemy->x);
    const int enemyRow = WorldToCellRow(enemy->y);
    const std::optional<GridPoint> approach =
        FindBestAdjacentApproachTile(map, playerCol, playerRow, enemyCol, enemyRow);
    if (!approach.has_value()) {
        client.pendingAttackEnemyId = -1;
        player.targetId = -1;
        return;
    }

    if (!StartPlayerPath(client, player, map, approach->first, approach->second, tick)) {
        client.pendingAttackEnemyId = -1;
        player.targetId = -1;
    }
}

}  // namespace

bool GameServer::Start(uint16_t tcpPort, uint16_t wsPort) {
    auto onMessage = [this](int clientId, const Message& message) {
        EnqueueMessage(clientId, TransportForClientId(clientId), message);
    };

    auto onDisconnect = [this](int clientId) {
        EnqueueDisconnect(clientId, TransportForClientId(clientId));
    };

    if (!tcpListener_.Start(tcpPort, onMessage, onDisconnect)) {
        return false;
    }

    if (!wsListener_.Start(wsPort, onMessage, onDisconnect)) {
        tcpListener_.Stop();
        return false;
    }

    running_ = true;
    enemies_ = CreateDefaultEnemies();
    for (EnemyState& enemy : enemies_) {
        enemy.stateStartTick = tick_;
        enemy.animStartTick = tick_;
    }
    return true;
}

void GameServer::Stop() {
    running_ = false;
    tcpListener_.Stop();
    wsListener_.Stop();
    clients_.clear();
    transportByClientId_.clear();
    players_.clear();
    enemies_.clear();
    chatHistory_.clear();
}

void GameServer::Run() {
    using Clock = std::chrono::steady_clock;
    const auto tickDuration = std::chrono::duration<double>(kTickDuration);
    auto nextTick = Clock::now();

    while (running_) {
        ProcessMessages();
        ProcessDisconnects();

        const auto now = Clock::now();
        if (now >= nextTick) {
            SimulateTick();
            BroadcastWorldState();
            ++tick_;
            nextTick += std::chrono::duration_cast<Clock::duration>(tickDuration);
            if (now - nextTick > std::chrono::seconds(1)) {
                nextTick = now;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void GameServer::EnqueueMessage(int clientId, TransportKind transport, const Message& message) {
    std::lock_guard<std::mutex> lock(incomingMutex_);
    incoming_.push_back(IncomingMessage{clientId, transport, message});
}

void GameServer::EnqueueDisconnect(int clientId, TransportKind transport) {
    std::lock_guard<std::mutex> lock(incomingMutex_);
    pendingDisconnects_.push_back(PendingDisconnect{clientId, transport});
}

void GameServer::ProcessMessages() {
    std::deque<IncomingMessage> batch;
    {
        std::lock_guard<std::mutex> lock(incomingMutex_);
        batch.swap(incoming_);
    }

    for (const IncomingMessage& incoming : batch) {
        HandleMessage(incoming);
    }
}

void GameServer::ProcessDisconnects() {
    std::deque<PendingDisconnect> batch;
    {
        std::lock_guard<std::mutex> lock(incomingMutex_);
        batch.swap(pendingDisconnects_);
    }

    for (const PendingDisconnect& pending : batch) {
        HandleDisconnect(pending.clientId, pending.transport);
    }
}

void GameServer::HandleMessage(const IncomingMessage& incoming) {
    switch (incoming.message.type) {
        case MessageType::JoinRequest: {
            if (static_cast<int>(players_.size()) >= kMaxPlayers) {
                SendToClient(incoming.clientId, incoming.transport,
                             MakeJoinRejected("Server is full"));
                if (incoming.transport == TransportKind::Tcp) {
                    tcpListener_.DisconnectClient(incoming.clientId);
                }
                return;
            }

            ConnectedClient client;
            client.id = incoming.clientId;
            client.transport = incoming.transport;
            client.name = incoming.message.joinRequest.name;
            if (client.name.empty()) {
                client.name = "Player" + std::to_string(incoming.clientId);
            }
            client.hasJoined = true;
            clients_[incoming.clientId] = client;
            transportByClientId_[incoming.clientId] = incoming.transport;

            const GridMap& map = DefaultGridMap();
            int spawnCol = kGridCols / 2;
            int spawnRow = kGridRows / 2;
            if (!map.IsWalkable(spawnCol, spawnRow)) {
                bool foundSpawn = false;
                for (int row = 1; row < kGridRows - 1 && !foundSpawn; ++row) {
                    for (int col = 1; col < kGridCols - 1; ++col) {
                        if (map.IsWalkable(col, row)) {
                            spawnCol = col;
                            spawnRow = row;
                            foundSpawn = true;
                            break;
                        }
                    }
                }
            }

            PlayerState player;
            player.id = incoming.clientId;
            player.name = client.name;
            player.x = CellCenterX(spawnCol);
            player.y = CellCenterY(spawnRow);
            player.state = EntityState::Idle;
            player.anim = PlayerAnim::Idle;
            player.stateStartTick =
                tick_ + static_cast<uint32_t>(incoming.clientId % kIdleFrameCount) *
                            static_cast<uint32_t>(kIdleAnimTicksPerFrame);
            player.animStartTick = player.stateStartTick;
            player.hp = kPlayerMaxHp;
            player.moveTargetCol = -1;
            player.moveTargetRow = -1;
            players_.push_back(player);

            SendToClient(incoming.clientId, incoming.transport,
                         MakeJoinAccepted(incoming.clientId, BuildPlayerSnapshot(), enemies_));
            if (!chatHistory_.empty()) {
                SendToClient(incoming.clientId, incoming.transport,
                             MakeChatHistory(chatHistory_));
            }
            std::cout << "[game] " << client.name << " joined as client "
                      << incoming.clientId << "\n";
            break;
        }
        case MessageType::MoveRequest: {
            auto it = clients_.find(incoming.clientId);
            if (it == clients_.end() || !it->second.hasJoined) {
                return;
            }

            PlayerState* player = FindPlayer(players_, incoming.clientId);
            if (player == nullptr || !CanAcceptMoveIntent(player->state)) {
                return;
            }

            const int col = incoming.message.moveRequest.col;
            const int row = incoming.message.moveRequest.row;
            const GridMap& map = DefaultGridMap();
            if (!IsValidCell(col, row) || !map.IsWalkable(col, row)) {
                return;
            }

            ConnectedClient& client = it->second;
            EndPlayerCombat(*player, enemies_, tick_);
            client.pendingAttackEnemyId = -1;
            player->targetId = -1;
            ClearPlayerMove(client, *player);

            if (!StartPlayerPath(client, *player, map, col, row, tick_)) {
                return;
            }
            break;
        }
        case MessageType::AttackRequest: {
            auto it = clients_.find(incoming.clientId);
            if (it == clients_.end() || !it->second.hasJoined) {
                return;
            }

            PlayerState* player = FindPlayer(players_, incoming.clientId);
            if (player == nullptr) {
                return;
            }

            HandlePlayerEngageRequest(*player, it->second, enemies_,
                                      incoming.message.attackRequest.enemyId, DefaultGridMap(),
                                      tick_);
            break;
        }
        case MessageType::CancelCombatRequest: {
            auto it = clients_.find(incoming.clientId);
            if (it == clients_.end() || !it->second.hasJoined) {
                return;
            }

            PlayerState* player = FindPlayer(players_, incoming.clientId);
            if (player == nullptr) {
                return;
            }

            CancelPlayerCombat(*player, it->second, enemies_, tick_);
            break;
        }
        case MessageType::Ping: {
            SendToClient(incoming.clientId, incoming.transport,
                         MakePong(incoming.message.ping.clientTimeMs, NowMs()));
            break;
        }
        case MessageType::Chat: {
            auto it = clients_.find(incoming.clientId);
            if (it == clients_.end() || !it->second.hasJoined) {
                return;
            }

            const std::string text = SanitizeChatText(incoming.message.chat.text);
            if (text.empty()) {
                return;
            }

            const ChatMessage entry{
                incoming.clientId,
                it->second.name,
                text,
            };
            RecordAndBroadcastChat(entry);
            std::cout << "[chat] " << it->second.name << ": " << text << "\n";
            break;
        }
        default:
            break;
    }
}

void GameServer::HandleDisconnect(int clientId, TransportKind transport) {
    (void)transport;
    if (clients_.find(clientId) == clients_.end() &&
        transportByClientId_.find(clientId) == transportByClientId_.end()) {
        return;
    }

    if (PlayerState* player = FindPlayer(players_, clientId)) {
        auto clientIt = clients_.find(clientId);
        if (clientIt != clients_.end()) {
            CancelPlayerCombat(*player, clientIt->second, enemies_, tick_);
        }
        (void)player;
    }

    clients_.erase(clientId);
    transportByClientId_.erase(clientId);
    players_.erase(
        std::remove_if(players_.begin(), players_.end(),
                       [&](const PlayerState& player) { return player.id == clientId; }),
        players_.end());
    std::cout << "[game] client " << clientId << " removed\n";
}

void GameServer::SimulateTick() {
    for (PlayerState& player : players_) {
        auto clientIt = clients_.find(player.id);
        if (clientIt == clients_.end()) {
            continue;
        }

        UpdatePlayerEntity(player, clientIt->second, enemies_, tick_);

        if (player.state != EntityState::Dead) {
            const float margin = kPlayerRadius;
            player.x = std::clamp(player.x, margin, kWorldWidth - margin);
            player.y = std::clamp(player.y, margin, kWorldHeight - margin);
        }
    }

    for (EnemyState& enemy : enemies_) {
        UpdateEnemyEntity(enemy, players_, tick_);
    }
}

void GameServer::BroadcastWorldState() {
    BroadcastToAll(MakeWorldState(tick_, players_, enemies_));
}

void GameServer::RecordAndBroadcastChat(const ChatMessage& entry) {
    chatHistory_.push_back(entry);
    if (static_cast<int>(chatHistory_.size()) > kMaxChatHistory) {
        chatHistory_.erase(chatHistory_.begin());
    }
    BroadcastToAll(MakeChatBroadcast(entry.playerId, entry.name, entry.text));
}

void GameServer::BroadcastToAll(const Message& message) {
    std::vector<std::pair<int, TransportKind>> recipients;
    recipients.reserve(transportByClientId_.size());
    for (const auto& [clientId, transport] : transportByClientId_) {
        recipients.emplace_back(clientId, transport);
    }

    for (const auto& [clientId, transport] : recipients) {
        if (transportByClientId_.find(clientId) == transportByClientId_.end()) {
            continue;
        }
        if (!SendToClient(clientId, transport, message)) {
            EnqueueDisconnect(clientId, transport);
        }
    }
}

bool GameServer::SendToClient(int clientId, TransportKind transport, const Message& message) {
    if (transport == TransportKind::Tcp) {
        return tcpListener_.SendTo(clientId, message);
    }
    return wsListener_.SendTo(clientId, message);
}

std::vector<PlayerState> GameServer::BuildPlayerSnapshot() const {
    return players_;
}

uint32_t GameServer::NowMs() const {
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now().time_since_epoch();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

}  // namespace net
