#include "game_server.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <thread>

#include "common/combat.hpp"
#include "common/config.hpp"
#include "common/enemies.hpp"
#include "common/entity_defs.hpp"
#include "common/entity_registry.hpp"
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

void SetEnemyMoveFacingToward(EnemyState& enemy, float targetX, float targetY) {
    UpdateEnemyFacing(enemy, targetX - enemy.x, targetY - enemy.y);
}

void ClearEnemyMove(EnemyMovementState& move) {
    move.hasMoveTarget = false;
    move.movePath.clear();
    move.pathIndex = 0;
}

void ClearEnemyChase(EnemyMovementState& move) {
    move.chasingPlayer = false;
    move.chaseTargetId = -1;
}

bool InPatrolIdle(const EnemyMovementState& move, uint32_t tick) {
    return tick < move.patrolIdleUntilTick;
}

void BeginPatrolIdle(EnemyState& enemy, EnemyMovementState& move, uint32_t tick) {
    SnapEntityToCellCenter(enemy.x, enemy.y);
    move.patrolIdleUntilTick = tick + static_cast<uint32_t>(kGoblinPatrolIdleTicks);
    TransitionEntity(enemy.state, enemy.stateStartTick, enemy.anim, enemy.animStartTick,
                     EntityState::Idle, tick);
}

bool IsPatrolGoalBlocked(int col, int row, int enemyId, const std::vector<EnemyState>& enemies) {
    for (const EnemyState& other : enemies) {
        if (other.id == enemyId || !IsAlive(other.state)) {
            continue;
        }
        if (WorldToCellCol(other.x) == col && WorldToCellRow(other.y) == row) {
            return true;
        }
    }
    return false;
}

void InitializeGoblinMovement(EnemyMovementState& move, const GridMap& map,
                               const EnemyState& enemy, uint32_t tick) {
    const int anchorCol = WorldToCellCol(enemy.x);
    const int anchorRow = WorldToCellRow(enemy.y);
    move.patrolWaypoints = BuildGoblinPatrolWaypoints(map, anchorCol, anchorRow);
    if (!move.patrolWaypoints.empty()) {
        move.patrolWaypointIndex = static_cast<size_t>(enemy.id) % move.patrolWaypoints.size();
    } else {
        move.patrolWaypointIndex = 0;
    }
    move.patrolIdleUntilTick =
        tick + static_cast<uint32_t>(kGoblinPatrolIdleTicks) +
        static_cast<uint32_t>((enemy.id % 4) * 15);
}

bool StartEnemyPath(EnemyState& enemy, EnemyMovementState& move, const GridMap& map, int goalCol,
                    int goalRow, uint32_t tick) {
    const int startCol = WorldToCellCol(enemy.x);
    const int startRow = WorldToCellRow(enemy.y);
    std::vector<GridPoint> path = FindPath(map, startCol, startRow, goalCol, goalRow);
    if (path.empty()) {
        return false;
    }

    move.movePath = std::move(path);
    move.pathIndex = 0;
    if (move.movePath.size() > 1 && move.movePath[0].first == startCol &&
        move.movePath[0].second == startRow) {
        move.pathIndex = 1;
    }
    if (move.pathIndex >= move.movePath.size()) {
        ClearEnemyMove(move);
        return false;
    }

    move.hasMoveTarget = true;
    const auto& firstWaypoint = move.movePath[move.pathIndex];
    SetEnemyMoveFacingToward(enemy, CellCenterX(firstWaypoint.first),
                             CellCenterY(firstWaypoint.second));
    TransitionEntity(enemy.state, enemy.stateStartTick, enemy.anim, enemy.animStartTick,
                     EntityState::Moving, tick);
    return true;
}

bool StepEnemyMovement(EnemyState& enemy, EnemyMovementState& move) {
    if (!move.hasMoveTarget || move.pathIndex >= move.movePath.size()) {
        return false;
    }

    float remainingStep = kPlayerSpeed * kTickDuration;
    static constexpr float kArriveEpsilon = 0.5f;
    bool moved = false;

    while (remainingStep > 0.0f && move.pathIndex < move.movePath.size()) {
        const auto& waypoint = move.movePath[move.pathIndex];
        const float targetX = CellCenterX(waypoint.first);
        const float targetY = CellCenterY(waypoint.second);
        float dx = targetX - enemy.x;
        float dy = targetY - enemy.y;
        const float dist = std::sqrt(dx * dx + dy * dy);

        if (dist <= kArriveEpsilon) {
            enemy.x = targetX;
            enemy.y = targetY;
            ++move.pathIndex;
            moved = true;
            if (move.pathIndex >= move.movePath.size()) {
                ClearEnemyMove(move);
            }
            continue;
        }

        dx /= dist;
        dy /= dist;
        UpdateEnemyFacing(enemy, dx, dy);

        if (dist <= remainingStep) {
            enemy.x = targetX;
            enemy.y = targetY;
            remainingStep -= dist;
            ++move.pathIndex;
            moved = true;
            if (move.pathIndex >= move.movePath.size()) {
                ClearEnemyMove(move);
            }
        } else {
            enemy.x += dx * remainingStep;
            enemy.y += dy * remainingStep;
            remainingStep = 0.0f;
            moved = true;
        }
    }

    return moved;
}

ConnectedClient* FindClient(std::unordered_map<int, ConnectedClient>& clients, int playerId) {
    const auto it = clients.find(playerId);
    return it != clients.end() ? &it->second : nullptr;
}

bool IsWithinGoblinAggroRange(float ax, float ay, float bx, float by) {
    const int colA = WorldToCellCol(ax);
    const int rowA = WorldToCellRow(ay);
    const int colB = WorldToCellCol(bx);
    const int rowB = WorldToCellRow(by);
    return ManhattanCellDistance(colA, rowA, colB, rowB) <= kGoblinAggroCellDistance;
}

PlayerState* FindGoblinAggroTarget(const EnemyState& enemy, std::vector<PlayerState>& players) {
    PlayerState* bestTarget = nullptr;
    int bestDistance = kGoblinAggroCellDistance + 1;

    for (PlayerState& player : players) {
        if (!IsAlive(player.state) || player.state == EntityState::Hit ||
            player.state == EntityState::Dead) {
            continue;
        }

        if (player.state == EntityState::Combat && player.targetId != enemy.id) {
            continue;
        }

        const int distance = ManhattanCellDistance(WorldToCellCol(enemy.x), WorldToCellRow(enemy.y),
                                                   WorldToCellCol(player.x),
                                                   WorldToCellRow(player.y));
        if (distance <= kGoblinAggroCellDistance && distance < bestDistance) {
            bestDistance = distance;
            bestTarget = &player;
        }
    }

    return bestTarget;
}

void ClearPlayerMove(ConnectedClient& client, PlayerState& player) {
    client.hasMoveTarget = false;
    client.movePath.clear();
    client.pathIndex = 0;
    player.moveTargetCol = -1;
    player.moveTargetRow = -1;
}

void ClearPlayerDisengage(ConnectedClient& client) {
    client.hasDisengageTarget = false;
}

bool IsVoluntaryMove(const ConnectedClient& client, const PlayerState& player) {
    return client.hasMoveTarget && client.pendingAttackEnemyId < 0 && player.targetId < 0;
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

const EnemyState* FindEnemyConst(const std::vector<EnemyState>& enemies, int enemyId) {
    for (const EnemyState& enemy : enemies) {
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
    RestartEntityAnim(player.anim, player.animStartTick, anim, tick);
}

PlayerAnim AnimForComboPhase(PlayerComboPhase phase) {
    switch (phase) {
        case PlayerComboPhase::Attack1: return PlayerAnim::Attack1;
        case PlayerComboPhase::Attack2: return PlayerAnim::Attack2;
        case PlayerComboPhase::Attack3: return PlayerAnim::Attack3;
        case PlayerComboPhase::PauseAfter1:
        case PlayerComboPhase::PauseAfter2: return PlayerAnim::Idle;
        case PlayerComboPhase::None:
        default: return PlayerAnim::Idle;
    }
}

bool IsComboAttackPhase(PlayerComboPhase phase) {
    return phase == PlayerComboPhase::Attack1 || phase == PlayerComboPhase::Attack2 ||
           phase == PlayerComboPhase::Attack3;
}

void BeginCombat(PlayerState& player, EnemyState& enemy, ConnectedClient& client, uint32_t tick,
                 bool preserveCombo = false) {
    SnapEntityToCellCenter(player.x, player.y);
    SnapEntityToCellCenter(enemy.x, enemy.y);

    player.targetId = enemy.id;
    enemy.targetId = player.id;
    TransitionEntityState(player.state, player.stateStartTick, EntityState::Combat, tick);
    TransitionEntityState(enemy.state, enemy.stateStartTick, EntityState::Combat, tick);
    UpdateFacingFromDirection(player, enemy.x - player.x, enemy.y - player.y);
    UpdateEnemyFacing(enemy, player.x - enemy.x, player.y - enemy.y);

    if (preserveCombo && client.comboPhase != PlayerComboPhase::None) {
        client.comboPhaseStartTick = tick;
        client.comboSwingDamageDealt = false;
        RestartEntityAnim(player.anim, player.animStartTick,
                          AnimForComboPhase(client.comboPhase), tick);
    } else {
        StartPlayerComboPhase(player, client, PlayerComboPhase::Attack1, PlayerAnim::Attack1, tick);
    }
}

bool TryBeginGoblinCombat(EnemyState& enemy, PlayerState& player,
                          std::unordered_map<int, ConnectedClient>& clients,
                          std::vector<EnemyState>& enemies, uint32_t tick) {
    SnapEntityToCellCenter(enemy.x, enemy.y);
    SnapEntityToCellCenter(player.x, player.y);
    if (!IsInMeleeRange(enemy.x, enemy.y, player.x, player.y)) {
        return false;
    }

    ConnectedClient* client = FindClient(clients, player.id);
    if (client == nullptr) {
        return false;
    }

    if (IsVoluntaryMove(*client, player)) {
        return false;
    }

    EndPlayerCombat(player, enemies, tick);
    ClearPlayerMove(*client, player);
    client->pendingAttackEnemyId = -1;
    ResetPlayerCombo(*client);
    BeginCombat(player, enemy, *client, tick);
    return true;
}

bool StartGoblinChase(EnemyState& enemy, EnemyMovementState& move, PlayerState& player,
                      std::unordered_map<int, ConnectedClient>& clients,
                      std::vector<EnemyState>& enemies, const GridMap& map, uint32_t tick) {
    ClearEnemyMove(move);
    move.chasingPlayer = true;
    move.chaseTargetId = player.id;

    if (IsInMeleeRange(enemy.x, enemy.y, player.x, player.y)) {
        ClearEnemyChase(move);
        return TryBeginGoblinCombat(enemy, player, clients, enemies, tick);
    }

    const int enemyCol = WorldToCellCol(enemy.x);
    const int enemyRow = WorldToCellRow(enemy.y);
    const int playerCol = WorldToCellCol(player.x);
    const int playerRow = WorldToCellRow(player.y);
    const std::optional<GridPoint> approach =
        FindBestAdjacentApproachTile(map, enemyCol, enemyRow, playerCol, playerRow);
    if (!approach.has_value()) {
        ClearEnemyChase(move);
        return false;
    }

    if (!StartEnemyPath(enemy, move, map, approach->first, approach->second, tick)) {
        ClearEnemyChase(move);
        return false;
    }

    return true;
}

bool StartNextPatrolLeg(EnemyState& enemy, EnemyMovementState& move, const GridMap& map,
                        const std::vector<EnemyState>& enemies, uint32_t tick) {
    if (move.patrolWaypoints.empty()) {
        return false;
    }

    const size_t startIndex = move.patrolWaypointIndex;
    for (size_t attempt = 0; attempt < move.patrolWaypoints.size(); ++attempt) {
        move.patrolWaypointIndex = (startIndex + attempt) % move.patrolWaypoints.size();
        const GridPoint& goal = move.patrolWaypoints[move.patrolWaypointIndex];
        if (IsPatrolGoalBlocked(goal.first, goal.second, enemy.id, enemies)) {
            continue;
        }
        if (StartEnemyPath(enemy, move, map, goal.first, goal.second, tick)) {
            move.patrolWaypointIndex = (move.patrolWaypointIndex + 1) % move.patrolWaypoints.size();
            return true;
        }
    }

    move.patrolWaypointIndex = startIndex;
    return false;
}

void TryCompleteGoblinChase(EnemyState& enemy, EnemyMovementState& move, PlayerState& player,
                            std::unordered_map<int, ConnectedClient>& clients,
                            std::vector<EnemyState>& enemies, const GridMap& map, uint32_t tick) {
    if (!IsWithinGoblinAggroRange(enemy.x, enemy.y, player.x, player.y)) {
        ClearEnemyChase(move);
        TransitionEntity(enemy.state, enemy.stateStartTick, enemy.anim, enemy.animStartTick,
                         EntityState::Idle, tick);
        return;
    }

    if (TryBeginGoblinCombat(enemy, player, clients, enemies, tick)) {
        ClearEnemyChase(move);
        return;
    }

    StartGoblinChase(enemy, move, player, clients, enemies, map, tick);
}

bool TryPathToCombatTarget(PlayerState& player, ConnectedClient& client, const EnemyState& enemy,
                           const GridMap& map, uint32_t tick) {
    const int playerCol = WorldToCellCol(player.x);
    const int playerRow = WorldToCellRow(player.y);
    const int enemyCol = WorldToCellCol(enemy.x);
    const int enemyRow = WorldToCellRow(enemy.y);
    const std::optional<GridPoint> approach =
        FindBestAdjacentApproachTile(map, playerCol, playerRow, enemyCol, enemyRow);
    if (!approach.has_value()) {
        return false;
    }

    client.pendingAttackEnemyId = enemy.id;
    return StartPlayerPath(client, player, map, approach->first, approach->second, tick);
}

void RecoverPlayerAfterHit(PlayerState& player, ConnectedClient& client,
                           std::vector<EnemyState>& enemies, const GridMap& map, uint32_t tick) {
    if (IsVoluntaryMove(client, player)) {
        if (player.hp <= 0) {
            TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                             EntityState::Dead, tick);
            ClearPlayerMove(client, player);
            return;
        }

        TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                         EntityState::Moving, tick);
        return;
    }

    if (player.targetId < 0) {
        TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                         EntityState::Idle, tick);
        return;
    }

    EnemyState* enemy = FindEnemy(enemies, player.targetId);
    if (enemy == nullptr || !IsAlive(enemy->state)) {
        EndPlayerCombat(player, enemies, tick);
        TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                         EntityState::Idle, tick);
        return;
    }

    if (IsInMeleeRange(player.x, player.y, enemy->x, enemy->y)) {
        BeginCombat(player, *enemy, client, tick, true);
        return;
    }

    if (TryPathToCombatTarget(player, client, *enemy, map, tick)) {
        return;
    }

    EndPlayerCombat(player, enemies, tick);
    TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                     EntityState::Idle, tick);
}

void CancelPlayerCombat(PlayerState& player, ConnectedClient& client,
                        std::vector<EnemyState>& enemies, uint32_t tick) {
    client.pendingAttackEnemyId = -1;
    ResetPlayerCombo(client);
    ClearPlayerMove(client, player);
    ClearPlayerDisengage(client);
    EndPlayerCombat(player, enemies, tick);
}

void StartPlayerDisengage(PlayerState& player, ConnectedClient& client, float targetX,
                          float targetY, uint32_t tick) {
    client.disengageStartX = player.x;
    client.disengageStartY = player.y;
    client.disengageTargetX = targetX;
    client.disengageTargetY = targetY;
    client.hasDisengageTarget = true;
    ClearPlayerMove(client, player);
    TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                     EntityState::Disengaging, tick);
    SetEntityAnim(player.anim, player.animStartTick, PlayerAnim::Jump, tick);
}

void UpdatePlayerDisengage(PlayerState& player, ConnectedClient& client, uint32_t tick) {
    if (!client.hasDisengageTarget) {
        TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                         EntityState::Idle, tick);
        return;
    }

    const int totalTicks = kJumpFrameCount * kJumpAnimTicksPerFrame;
    const uint32_t elapsed = tick >= player.animStartTick ? tick - player.animStartTick : 0;
    const float progress =
        totalTicks > 0
            ? std::min(1.0f, static_cast<float>(elapsed) / static_cast<float>(totalTicks))
            : 1.0f;
    player.x = client.disengageStartX +
               (client.disengageTargetX - client.disengageStartX) * progress;
    player.y = client.disengageStartY +
               (client.disengageTargetY - client.disengageStartY) * progress;

    if (!IsAnimFinished(PlayerAnim::Jump, tick, player.animStartTick)) {
        return;
    }

    player.x = client.disengageTargetX;
    player.y = client.disengageTargetY;
    SnapEntityToCellCenter(player.x, player.y);
    ClearPlayerDisengage(client);
    TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                     EntityState::Idle, tick);
}

void DisengagePlayerCombat(PlayerState& player, ConnectedClient& client,
                           std::vector<EnemyState>& enemies,
                           const std::vector<PlayerState>& players, const GridMap& map,
                           uint32_t tick) {
    if (player.state != EntityState::Combat || player.targetId < 0) {
        return;
    }

    const EnemyState* enemy = FindEnemyConst(enemies, player.targetId);
    if (enemy == nullptr) {
        CancelPlayerCombat(player, client, enemies, tick);
        return;
    }

    SnapEntityToCellCenter(player.x, player.y);
    const int playerCol = WorldToCellCol(player.x);
    const int playerRow = WorldToCellRow(player.y);
    const int enemyCol = WorldToCellCol(enemy->x);
    const int enemyRow = WorldToCellRow(enemy->y);
    const bool inMelee = IsInMeleeRange(player.x, player.y, enemy->x, enemy->y);

    CancelPlayerCombat(player, client, enemies, tick);

    if (!inMelee) {
        return;
    }

    const std::optional<GridPoint> retreat =
        FindRetreatTile(map, playerCol, playerRow, enemyCol, enemyRow);
    if (!retreat.has_value()) {
        return;
    }

    if (IsCellOccupied(retreat->first, retreat->second, players, enemies, player.id, -1)) {
        return;
    }

    UpdateFacingFromDirection(player, enemyCol - retreat->first, enemyRow - retreat->second);
    StartPlayerDisengage(player, client, CellCenterX(retreat->first), CellCenterY(retreat->second),
                         tick);
}

void TryApplyComboSwingDamage(PlayerState& player, ConnectedClient& client, EnemyState& enemy,
                              uint32_t tick) {
    if (!IsComboAttackPhase(client.comboPhase) || client.comboSwingDamageDealt) {
        return;
    }

    const PlayerAnim attackAnim = AnimForComboPhase(client.comboPhase);
    const int damageFrame = AttackDamageFrame(attackAnim);
    if (damageFrame < 0) {
        return;
    }

    const int currentFrame =
        CurrentAnimFrame(attackAnim, tick, client.comboPhaseStartTick);
    if (currentFrame != damageFrame) {
        return;
    }

    client.comboSwingDamageDealt = true;
    const CombatStats& playerStats = DefaultEntityRegistry().StatsFor(kPlayerEntityId);
    const bool critical =
        RollCriticalHit(player.id, enemy.id, tick, playerStats.critChancePercent);
    const int damage = ComputeAttackDamage(playerStats, critical);
    ApplyDamageToEnemy(enemy, damage, tick, critical);
    if (!IsAlive(enemy.state)) {
        player.targetId = -1;
        ResetPlayerCombo(client);
        TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                         EntityState::Idle, tick);
    }
}

void UpdatePlayerCombo(PlayerState& player, ConnectedClient& client,
                       std::vector<EnemyState>& enemies, const GridMap& map, uint32_t tick) {
    if (IsVoluntaryMove(client, player)) {
        return;
    }

    ClearPlayerMove(client, player);

    EnemyState* enemy = FindEnemy(enemies, player.targetId);
    if (enemy == nullptr || !IsAlive(enemy->state)) {
        CancelPlayerCombat(player, client, enemies, tick);
        return;
    }

    if (!IsInMeleeRange(player.x, player.y, enemy->x, enemy->y)) {
        if (!TryPathToCombatTarget(player, client, *enemy, map, tick)) {
            CancelPlayerCombat(player, client, enemies, tick);
        }
        return;
    }

    SnapEntityToCellCenter(player.x, player.y);
    SnapEntityToCellCenter(enemy->x, enemy->y);

    UpdateFacingFromDirection(player, enemy->x - player.x, enemy->y - player.y);
    TryApplyComboSwingDamage(player, client, *enemy, tick);

    if (!IsAlive(enemy->state)) {
        return;
    }

    switch (client.comboPhase) {
        case PlayerComboPhase::Attack1:
            if (IsAnimFinished(PlayerAnim::Attack1, tick, client.comboPhaseStartTick)) {
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
            if (IsAnimFinished(PlayerAnim::Attack2, tick, client.comboPhaseStartTick)) {
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
            if (IsAnimFinished(PlayerAnim::Attack3, tick, client.comboPhaseStartTick)) {
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

void UpdateGoblinCombat(EnemyState& enemy, std::vector<PlayerState>& players, uint32_t tick) {
    if (enemy.state != EntityState::Combat || enemy.targetId < 0) {
        return;
    }

    const CombatStats& stats = DefaultEntityRegistry().StatsFor(enemy.kind);

    const PlayerState* player = FindPlayerConst(players, enemy.targetId);
    if (player == nullptr || !IsAlive(player->state) || player->state == EntityState::Hit) {
        return;
    }

    if (!IsInMeleeRange(enemy.x, enemy.y, player->x, player->y)) {
        return;
    }

    if (PlayerState* mutablePlayer = FindPlayer(players, enemy.targetId)) {
        SnapEntityToCellCenter(enemy.x, enemy.y);
        SnapEntityToCellCenter(mutablePlayer->x, mutablePlayer->y);
    }

    UpdateEnemyFacing(enemy, player->x - enemy.x, player->y - enemy.y);

    if (enemy.anim == PlayerAnim::Attack1) {
        if (!enemy.attackDamageDealt) {
            const int frame =
                GoblinAnimFrameIndex(PlayerAnim::Attack1, tick, enemy.animStartTick);
            if (frame == kGoblinAttackDamageFrame) {
                enemy.attackDamageDealt = true;
                if (PlayerState* mutablePlayer = FindPlayer(players, enemy.targetId)) {
                    ApplyDamageToPlayer(*mutablePlayer, stats.attackDamage, tick, false);
                }
            }
        }

        if (GoblinAnimFinished(PlayerAnim::Attack1, tick, enemy.animStartTick)) {
            enemy.lastAttackTick = tick;
            enemy.attackDamageDealt = false;
            SetEntityAnim(enemy.anim, enemy.animStartTick, PlayerAnim::Idle, tick);
        }
        return;
    }

    if (tick - enemy.lastAttackTick < static_cast<uint32_t>(stats.attackCooldownTicks)) {
        return;
    }

    enemy.attackDamageDealt = false;
    RestartEntityAnim(enemy.anim, enemy.animStartTick, PlayerAnim::Attack1, tick);
}

void RespawnEnemy(int enemyId, std::vector<EnemyState>& enemies,
                  std::vector<PlayerState>& players,
                  std::unordered_map<int, ConnectedClient>& clients,
                  std::unordered_map<int, EnemyMovementState>& enemyMovement, uint32_t tick) {
    for (PlayerState& player : players) {
        if (player.targetId != enemyId) {
            continue;
        }

        if (IsAlive(player.state) && player.state == EntityState::Combat) {
            TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                             EntityState::Idle, tick);
        }
        player.targetId = -1;
    }

    for (auto& [clientId, client] : clients) {
        (void)clientId;
        if (client.pendingAttackEnemyId == enemyId) {
            client.pendingAttackEnemyId = -1;
            ResetPlayerCombo(client);
        }
    }

    enemyMovement.erase(enemyId);

    const GridMap& map = DefaultGridMap();
    const auto [spawnCol, spawnRow] = PickRandomGoblinSpawnCell(map, enemies, enemyId);

    EnemyState* enemy = FindEnemy(enemies, enemyId);
    if (enemy == nullptr) {
        EnemyState spawned = CreateGoblinAt(enemyId, spawnCol, spawnRow);
        spawned.stateStartTick = tick;
        spawned.animStartTick = tick;
        enemies.push_back(spawned);
        InitializeGoblinMovement(enemyMovement[enemyId], map, spawned, tick);
        return;
    }

    *enemy = CreateGoblinAt(enemyId, spawnCol, spawnRow);
    enemy->stateStartTick = tick;
    enemy->animStartTick = tick;
    InitializeGoblinMovement(enemyMovement[enemyId], map, *enemy, tick);
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
        client.pendingAttackEnemyId = enemyId;
        if (TryPathToCombatTarget(player, client, *enemy, DefaultGridMap(), tick)) {
            return;
        }
        EndPlayerCombat(player, enemies, tick);
        TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                         EntityState::Idle, tick);
        return;
    }

    BeginCombat(player, *enemy, client, tick, client.comboPhase != PlayerComboPhase::None);
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

    return moved;
}

void UpdatePlayerEntity(PlayerState& player, ConnectedClient& client,
                        std::vector<EnemyState>& enemies, uint32_t tick) {
    if (player.state == EntityState::Dead) {
        ClearPlayerMove(client, player);
        return;
    }

    if (player.state == EntityState::Disengaging) {
        ClearPlayerMove(client, player);
        UpdatePlayerDisengage(player, client, tick);
        return;
    }

    if (IsVoluntaryMove(client, player)) {
        if (player.state == EntityState::Combat) {
            EndPlayerCombat(player, enemies, tick);
        }

        const CombatStats& stats = DefaultEntityRegistry().StatsFor(kPlayerEntityId);
        const bool hitStunActive =
            player.state == EntityState::Hit &&
            tick - player.stateStartTick < static_cast<uint32_t>(stats.hitStunTicks);

        if (hitStunActive) {
            return;
        }

        if (player.state == EntityState::Hit) {
            if (player.hp <= 0) {
                TransitionEntity(player.state, player.stateStartTick, player.anim,
                                 player.animStartTick, EntityState::Dead, tick);
                ClearPlayerMove(client, player);
                return;
            }

            TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                             EntityState::Moving, tick);
        } else if (player.state != EntityState::Moving) {
            TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                             EntityState::Moving, tick);
        }

        StepPlayerMovement(player, client, tick);
        if (!client.hasMoveTarget && player.state == EntityState::Moving) {
            TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                             EntityState::Idle, tick);
        }
        return;
    }

    if (player.state == EntityState::Hit) {
        ClearPlayerMove(client, player);
        const CombatStats& stats = DefaultEntityRegistry().StatsFor(kPlayerEntityId);
        if (tick - player.stateStartTick >= static_cast<uint32_t>(stats.hitStunTicks)) {
            if (player.hp <= 0) {
                TransitionEntity(player.state, player.stateStartTick, player.anim,
                                 player.animStartTick, EntityState::Dead, tick);
            } else {
                RecoverPlayerAfterHit(player, client, enemies, DefaultGridMap(), tick);
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
    }

    if (player.state == EntityState::Combat) {
        UpdatePlayerCombo(player, client, enemies, DefaultGridMap(), tick);
    }
}

void UpdateEnemyEntity(EnemyState& enemy, EnemyMovementState& move,
                       std::vector<PlayerState>& players,
                       std::unordered_map<int, ConnectedClient>& clients,
                       std::vector<EnemyState>& enemies, const GridMap& map, uint32_t tick) {
    if (enemy.state == EntityState::Dead) {
        ClearEnemyMove(move);
        ClearEnemyChase(move);
        return;
    }

    if (enemy.state == EntityState::Hit) {
        ClearEnemyMove(move);
        ClearEnemyChase(move);
        const CombatStats& stats = DefaultEntityRegistry().StatsFor(enemy.kind);
        if (tick - enemy.stateStartTick >= static_cast<uint32_t>(stats.hitStunTicks)) {
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
        ClearEnemyMove(move);
        ClearEnemyChase(move);

        const PlayerState* player = FindPlayerConst(players, enemy.targetId);
        if (player == nullptr || !IsAlive(player->state) ||
            !IsInMeleeRange(enemy.x, enemy.y, player->x, player->y)) {
            enemy.targetId = -1;
            TransitionEntity(enemy.state, enemy.stateStartTick, enemy.anim, enemy.animStartTick,
                             EntityState::Idle, tick);
            return;
        }

        UpdateGoblinCombat(enemy, players, tick);
        return;
    }

    if (!InPatrolIdle(move, tick)) {
        if (PlayerState* aggroTarget = FindGoblinAggroTarget(enemy, players)) {
            if (IsInMeleeRange(enemy.x, enemy.y, aggroTarget->x, aggroTarget->y)) {
                ClearEnemyMove(move);
                ClearEnemyChase(move);
                TryBeginGoblinCombat(enemy, *aggroTarget, clients, enemies, tick);
                return;
            }

            if (!move.chasingPlayer || move.chaseTargetId != aggroTarget->id ||
                !move.hasMoveTarget) {
                StartGoblinChase(enemy, move, *aggroTarget, clients, enemies, map, tick);
            }
        } else if (move.chasingPlayer) {
            ClearEnemyChase(move);
            ClearEnemyMove(move);
            if (enemy.state == EntityState::Moving) {
                TransitionEntity(enemy.state, enemy.stateStartTick, enemy.anim,
                                 enemy.animStartTick, EntityState::Idle, tick);
            }
        }
    }

    if (move.hasMoveTarget || enemy.state == EntityState::Moving) {
        StepEnemyMovement(enemy, move);
        if (!move.hasMoveTarget) {
            if (move.chasingPlayer && move.chaseTargetId >= 0) {
                PlayerState* chaseTarget = FindPlayer(players, move.chaseTargetId);
                if (chaseTarget != nullptr) {
                    SnapEntityToCellCenter(enemy.x, enemy.y);
                    TryCompleteGoblinChase(enemy, move, *chaseTarget, clients, enemies, map, tick);
                } else {
                    ClearEnemyChase(move);
                    TransitionEntity(enemy.state, enemy.stateStartTick, enemy.anim,
                                     enemy.animStartTick, EntityState::Idle, tick);
                }
            } else if (enemy.state == EntityState::Moving) {
                BeginPatrolIdle(enemy, move, tick);
            }
        }
        return;
    }

    if (enemy.state == EntityState::Idle && !move.chasingPlayer && !move.hasMoveTarget &&
        !InPatrolIdle(move, tick)) {
        StartNextPatrolLeg(enemy, move, map, enemies, tick);
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
    InitializeEntityRegistry();
    enemies_ = CreateDefaultEnemies();
    if (EnemiesShareTiles(enemies_)) {
        std::cerr << "[game] warning: goblins spawned on shared tiles\n";
    }
    for (EnemyState& enemy : enemies_) {
        enemy.stateStartTick = tick_;
        enemy.animStartTick = tick_;
        InitializeGoblinMovement(enemyMovement_[enemy.id], DefaultGridMap(), enemy, tick_);
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
    enemyMovement_.clear();
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
            const auto [spawnCol, spawnRow] = ResolvePlayerSpawnCell(map);

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
            const CombatStats& playerStats = DefaultEntityRegistry().StatsFor(kPlayerEntityId);
            player.hp = playerStats.maxHp;
            player.shield = playerStats.maxShield;
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
            ResetPlayerCombo(client);
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
        case MessageType::DisengageRequest: {
            auto it = clients_.find(incoming.clientId);
            if (it == clients_.end() || !it->second.hasJoined) {
                return;
            }

            PlayerState* player = FindPlayer(players_, incoming.clientId);
            if (player == nullptr) {
                return;
            }

            DisengagePlayerCombat(*player, it->second, enemies_, players_, DefaultGridMap(),
                                  tick_);
            break;
        }
        case MessageType::RespawnEnemyRequest: {
            auto it = clients_.find(incoming.clientId);
            if (it == clients_.end() || !it->second.hasJoined) {
                return;
            }

            RespawnEnemy(incoming.message.respawnEnemyRequest.enemyId, enemies_, players_,
                         clients_, enemyMovement_, tick_);
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
        UpdateEnemyEntity(enemy, enemyMovement_[enemy.id], players_, clients_, enemies_,
                          DefaultGridMap(), tick_);
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
