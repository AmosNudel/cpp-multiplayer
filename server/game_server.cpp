#include "game_server.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <random>
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
#include "common/session.hpp"

namespace net {
namespace {

const GridMap& MapForScene(SceneId scene) {
    return scene == SceneId::Arena ? ArenaGridMap() : HubGridMap();
}

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
        if (EnemyOccupiesCell(other, col, row)) {
            return true;
        }
    }
    return false;
}

bool IsPlayerInMeleeWithEnemy(float playerX, float playerY, const EnemyState& enemy) {
    return IsInMeleeRangeWithEnemy(playerX, playerY, enemy);
}

void PickRandomBossComboPattern(int pattern[3]) {
    static constexpr int kPatterns[5][3] = {
        {1, 1, 2},
        {1, 2, 1},
        {2, 1, 1},
        {1, 1, 1},
        {2, 1, 2},
    };
    static std::mt19937 rng(std::random_device{}());
    const int index = static_cast<int>(rng() % 5);
    for (int i = 0; i < 3; ++i) {
        pattern[i] = kPatterns[index][i];
    }
}

void ResetBossCombo(EnemyMovementState& move) {
    move.bossComboPhase = BossComboPhase::None;
    move.bossComboPhaseStartTick = 0;
    move.bossComboSwingDamageDealt = false;
}

void StartBossComboSwing(EnemyState& enemy, EnemyMovementState& move, BossComboPhase phase,
                         int swingIndex, uint32_t tick) {
    move.bossComboPhase = phase;
    move.bossComboPhaseStartTick = tick;
    move.bossComboSwingDamageDealt = false;
    const PlayerAnim anim = GoblinAttackAnimForVariant(move.bossComboPattern[swingIndex]);
    RestartEntityAnim(enemy.anim, enemy.animStartTick, anim, tick);
}

void StartBossComboCycle(EnemyState& enemy, EnemyMovementState& move, uint32_t tick) {
    PickRandomBossComboPattern(move.bossComboPattern);
    StartBossComboSwing(enemy, move, BossComboPhase::Swing1, 0, tick);
}

int BossSwingDamageForVariant(const CombatStats& stats, int variant) {
    if (variant == 2) {
        return (stats.attackDamage * kGoblinBossVariantDamageNumerator) /
               kGoblinBossVariantDamageDenominator;
    }
    return stats.attackDamage;
}

void ApplyBossAoeDamage(const EnemyState& enemy, int damage, std::vector<PlayerState>& players,
                        uint32_t tick) {
    if (damage <= 0) {
        return;
    }

    const std::vector<std::pair<int, int>> adjacent = CollectAdjacentCellsAroundEnemy(enemy);
    for (PlayerState& player : players) {
        if (player.sceneId != SceneId::Arena || !IsAlive(player.state)) {
            continue;
        }

        const int playerCol = WorldToCellCol(player.x);
        const int playerRow = WorldToCellRow(player.y);
        for (const std::pair<int, int>& cell : adjacent) {
            if (cell.first == playerCol && cell.second == playerRow) {
                ApplyDamageToPlayer(player, damage, tick, false);
                break;
            }
        }
    }
}

void TryApplyBossComboSwingDamage(EnemyState& enemy, EnemyMovementState& move,
                                  std::vector<PlayerState>& players, uint32_t tick) {
    if (move.bossComboSwingDamageDealt) {
        return;
    }

    int swingIndex = -1;
    switch (move.bossComboPhase) {
        case BossComboPhase::Swing1: swingIndex = 0; break;
        case BossComboPhase::Swing2: swingIndex = 1; break;
        case BossComboPhase::Swing3: swingIndex = 2; break;
        default: return;
    }

    const PlayerAnim attackAnim = GoblinAttackAnimForVariant(move.bossComboPattern[swingIndex]);
    if (enemy.anim != attackAnim) {
        return;
    }

    const int frame = GoblinAnimFrameIndex(attackAnim, tick, enemy.animStartTick);
    if (frame != kGoblinAttackDamageFrame) {
        return;
    }

    move.bossComboSwingDamageDealt = true;
    const CombatStats& stats = DefaultEntityRegistry().StatsFor(enemy.kind);
    const int damage = BossSwingDamageForVariant(stats, move.bossComboPattern[swingIndex]);
    ApplyBossAoeDamage(enemy, damage, players, tick);
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
                    int goalRow, uint32_t tick, const std::vector<PlayerState>& players,
                    const std::vector<EnemyState>& enemies) {
    if (IsCellOccupied(goalCol, goalRow, players, enemies, -1, enemy.id)) {
        return false;
    }

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

bool StepEnemyMovement(EnemyState& enemy, EnemyMovementState& move, const GridMap& map,
                       const std::vector<PlayerState>& players,
                       const std::vector<EnemyState>& enemies) {
    if (!move.hasMoveTarget || move.pathIndex >= move.movePath.size()) {
        return false;
    }

    float remainingStep = kPlayerSpeed * kTickDuration;
    static constexpr float kArriveEpsilon = 0.5f;
    bool moved = false;

    while (remainingStep > 0.0f && move.pathIndex < move.movePath.size()) {
        const auto& waypoint = move.movePath[move.pathIndex];
        const int waypointCol = waypoint.first;
        const int waypointRow = waypoint.second;
        if (IsCellOccupied(waypointCol, waypointRow, players, enemies, -1, enemy.id)) {
            ClearEnemyMove(move);
            return moved;
        }

        const float targetX = CellCenterX(waypointCol);
        const float targetY = CellCenterY(waypointRow);
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

bool IsWithinGoblinLeashRange(float ax, float ay, float bx, float by) {
    const int colA = WorldToCellCol(ax);
    const int rowA = WorldToCellRow(ay);
    const int colB = WorldToCellCol(bx);
    const int rowB = WorldToCellRow(by);
    return ManhattanCellDistance(colA, rowA, colB, rowB) <= kGoblinLeashCellDistance;
}

int FindReplacementEnemyTarget(const EnemyState& enemy, const std::vector<PlayerState>& players) {
    for (const PlayerState& player : players) {
        if (player.sceneId != SceneId::Arena || !IsAlive(player.state)) {
            continue;
        }
        if (player.state != EntityState::Combat || player.targetId != enemy.id) {
            continue;
        }
        if (!IsPlayerInMeleeWithEnemy(player.x, player.y, enemy)) {
            continue;
        }
        return player.id;
    }
    return -1;
}

PlayerState* FindGoblinAggroTarget(const EnemyState& enemy, std::vector<PlayerState>& players) {
    PlayerState* bestTarget = nullptr;
    int bestDistance = kGoblinAggroCellDistance + 1;

    for (PlayerState& player : players) {
        if (player.sceneId != SceneId::Arena) {
            continue;
        }
        if (!IsAlive(player.state) || player.state == EntityState::Hit ||
            player.state == EntityState::Dead || IsLeavingCombat(player.state)) {
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

void ClearPendingMoveAfterDisengage(ConnectedClient& client) {
    client.pendingMoveCol = -1;
    client.pendingMoveRow = -1;
}

bool IsVoluntaryMove(const ConnectedClient& client, const PlayerState& player) {
    return client.hasMoveTarget && client.pendingAttackEnemyId < 0 && player.targetId < 0;
}

bool StartPlayerPath(ConnectedClient& client, PlayerState& player, const GridMap& map,
                     int goalCol, int goalRow, uint32_t tick,
                     const std::vector<PlayerState>& players,
                     const std::vector<EnemyState>& enemies) {
    if (IsCellOccupied(goalCol, goalRow, players, enemies, player.id, -1)) {
        return false;
    }

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

void BreakPlayerCombatLink(PlayerState& player, const std::vector<PlayerState>& players,
                           std::vector<EnemyState>& enemies, uint32_t tick) {
    if (player.targetId >= 0) {
        if (EnemyState* enemy = FindEnemy(enemies, player.targetId)) {
            if (enemy->targetId == player.id) {
                enemy->targetId = FindReplacementEnemyTarget(*enemy, players);
                if (enemy->targetId < 0 && IsAlive(enemy->state) &&
                    enemy->state == EntityState::Combat) {
                    TransitionEntity(enemy->state, enemy->stateStartTick, enemy->anim,
                                     enemy->animStartTick, EntityState::Idle, tick);
                }
            }
        }
    }
    player.targetId = -1;
}

void EndPlayerCombat(PlayerState& player, const std::vector<PlayerState>& players,
                     std::vector<EnemyState>& enemies, uint32_t tick) {
    BreakPlayerCombatLink(player, players, enemies, tick);
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
    const bool enemyAlreadyEngaged =
        enemy.state == EntityState::Combat && enemy.targetId >= 0;
    if (!enemyAlreadyEngaged) {
        enemy.targetId = player.id;
    }

    TransitionEntityState(player.state, player.stateStartTick, EntityState::Combat, tick);
    if (enemy.state != EntityState::Combat) {
        TransitionEntityState(enemy.state, enemy.stateStartTick, EntityState::Combat, tick);
    }
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
                          std::vector<PlayerState>& players,
                          std::unordered_map<int, ConnectedClient>& clients,
                          std::vector<EnemyState>& enemies, EnemyMovementState& move,
                          uint32_t tick) {
    SnapEntityToCellCenter(enemy.x, enemy.y);
    SnapEntityToCellCenter(player.x, player.y);
    if (!IsPlayerInMeleeWithEnemy(player.x, player.y, enemy)) {
        return false;
    }

    if (IsLeavingCombat(player.state)) {
        return false;
    }

    ConnectedClient* client = FindClient(clients, player.id);
    if (client == nullptr) {
        return false;
    }

    if (IsVoluntaryMove(*client, player)) {
        return false;
    }

    if (player.targetId == enemy.id && player.state == EntityState::Combat) {
        return true;
    }

    if (player.targetId >= 0 && player.targetId != enemy.id) {
        EndPlayerCombat(player, players, enemies, tick);
    }

    ClearPlayerMove(*client, player);
    client->pendingAttackEnemyId = -1;
    ResetPlayerCombo(*client);
    if (IsGoblinBoss(enemy)) {
        ResetBossCombo(move);
    }
    BeginCombat(player, enemy, *client, tick);
    return true;
}

bool StartGoblinChase(EnemyState& enemy, EnemyMovementState& move, PlayerState& player,
                      std::vector<PlayerState>& players,
                      std::unordered_map<int, ConnectedClient>& clients,
                      std::vector<EnemyState>& enemies, const GridMap& map, uint32_t tick) {
    ClearEnemyMove(move);
    move.chasingPlayer = true;
    move.chaseTargetId = player.id;

    if (IsPlayerInMeleeWithEnemy(player.x, player.y, enemy)) {
        ClearEnemyChase(move);
        return TryBeginGoblinCombat(enemy, player, players, clients, enemies, move, tick);
    }

    std::optional<GridPoint> approach;
    const int enemyCol = WorldToCellCol(enemy.x);
    const int enemyRow = WorldToCellRow(enemy.y);
    const int playerCol = WorldToCellCol(player.x);
    const int playerRow = WorldToCellRow(player.y);
    approach = FindBestAdjacentApproachTile(map, enemyCol, enemyRow, playerCol, playerRow, &players,
                                            &enemies, -1, enemy.id);
    if (!approach.has_value()) {
        ClearEnemyChase(move);
        return false;
    }

    if (!StartEnemyPath(enemy, move, map, approach->first, approach->second, tick, players,
                        enemies)) {
        ClearEnemyChase(move);
        return false;
    }

    return true;
}

bool StartNextPatrolLeg(EnemyState& enemy, EnemyMovementState& move, const GridMap& map,
                        const std::vector<PlayerState>& players,
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
        if (StartEnemyPath(enemy, move, map, goal.first, goal.second, tick, players, enemies)) {
            move.patrolWaypointIndex = (move.patrolWaypointIndex + 1) % move.patrolWaypoints.size();
            return true;
        }
    }

    move.patrolWaypointIndex = startIndex;
    return false;
}

void TryCompleteGoblinChase(EnemyState& enemy, EnemyMovementState& move, PlayerState& player,
                            std::vector<PlayerState>& players,
                            std::unordered_map<int, ConnectedClient>& clients,
                            std::vector<EnemyState>& enemies, const GridMap& map, uint32_t tick) {
    if (!IsWithinGoblinLeashRange(enemy.x, enemy.y, player.x, player.y)) {
        ClearEnemyChase(move);
        TransitionEntity(enemy.state, enemy.stateStartTick, enemy.anim, enemy.animStartTick,
                         EntityState::Idle, tick);
        return;
    }

    if (TryBeginGoblinCombat(enemy, player, players, clients, enemies, move, tick)) {
        ClearEnemyChase(move);
        return;
    }

    StartGoblinChase(enemy, move, player, players, clients, enemies, map, tick);
}

bool TryPathToCombatTarget(PlayerState& player, ConnectedClient& client, const EnemyState& enemy,
                           const GridMap& map, uint32_t tick,
                           const std::vector<PlayerState>& players,
                           const std::vector<EnemyState>& enemies) {
    const int playerCol = WorldToCellCol(player.x);
    const int playerRow = WorldToCellRow(player.y);
    const int enemyCol = WorldToCellCol(enemy.x);
    const int enemyRow = WorldToCellRow(enemy.y);
    const std::optional<GridPoint> approach =
        FindBestAdjacentApproachTile(map, playerCol, playerRow, enemyCol, enemyRow, &players,
                                     &enemies, player.id, enemy.id);
    if (!approach.has_value()) {
        return false;
    }

    client.pendingAttackEnemyId = enemy.id;
    return StartPlayerPath(client, player, map, approach->first, approach->second, tick, players,
                           enemies);
}

void RecoverPlayerAfterHit(PlayerState& player, ConnectedClient& client,
                           std::vector<PlayerState>& players,
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
        EndPlayerCombat(player, players, enemies, tick);
        TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                         EntityState::Idle, tick);
        return;
    }

    if (IsPlayerInMeleeWithEnemy(player.x, player.y, *enemy)) {
        BeginCombat(player, *enemy, client, tick, true);
        return;
    }

    if (TryPathToCombatTarget(player, client, *enemy, map, tick, players, enemies)) {
        return;
    }

    EndPlayerCombat(player, players, enemies, tick);
    TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                     EntityState::Idle, tick);
}

void CancelPlayerCombat(PlayerState& player, ConnectedClient& client,
                        const std::vector<PlayerState>& players,
                        std::vector<EnemyState>& enemies, uint32_t tick) {
    client.pendingAttackEnemyId = -1;
    ResetPlayerCombo(client);
    ClearPlayerMove(client, player);
    ClearPlayerDisengage(client);
    ClearPendingMoveAfterDisengage(client);
    EndPlayerCombat(player, players, enemies, tick);
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

void UpdatePlayerDisengage(PlayerState& player, ConnectedClient& client, const GridMap& map,
                           uint32_t tick, const std::vector<PlayerState>& players,
                           const std::vector<EnemyState>& enemies) {
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

    const int pendingCol = client.pendingMoveCol;
    const int pendingRow = client.pendingMoveRow;
    ClearPendingMoveAfterDisengage(client);

    if (pendingCol >= 0 && pendingRow >= 0 &&
        StartPlayerPath(client, player, map, pendingCol, pendingRow, tick, players, enemies)) {
        return;
    }

    TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                     EntityState::Idle, tick);
}

void DisengagePlayerCombat(PlayerState& player, ConnectedClient& client,
                           std::vector<EnemyState>& enemies,
                           const std::vector<PlayerState>& players, const GridMap& map,
                           uint32_t tick) {
    if (!CanAcceptDisengageIntent(player.state) || player.targetId < 0) {
        return;
    }

    const EnemyState* enemy = FindEnemyConst(enemies, player.targetId);
    if (enemy == nullptr) {
        CancelPlayerCombat(player, client, players, enemies, tick);
        return;
    }

    SnapEntityToCellCenter(player.x, player.y);
    const int playerCol = WorldToCellCol(player.x);
    const int playerRow = WorldToCellRow(player.y);
    const int enemyCol = WorldToCellCol(enemy->x);
    const int enemyRow = WorldToCellRow(enemy->y);
    const bool inMelee = IsPlayerInMeleeWithEnemy(player.x, player.y, *enemy);

    client.pendingAttackEnemyId = -1;
    ResetPlayerCombo(client);
    BreakPlayerCombatLink(player, players, enemies, tick);
    ClearPlayerMove(client, player);

    if (!inMelee) {
        TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                         EntityState::Idle, tick);
        return;
    }

    const std::optional<GridPoint> retreat =
        FindRetreatTile(map, playerCol, playerRow, enemyCol, enemyRow);
    if (!retreat.has_value()) {
        TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                         EntityState::Idle, tick);
        return;
    }

    if (IsCellOccupied(retreat->first, retreat->second, players, enemies, player.id, -1)) {
        TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                         EntityState::Idle, tick);
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
                       std::vector<PlayerState>& players, std::vector<EnemyState>& enemies,
                       const GridMap& map, uint32_t tick) {
    if (IsVoluntaryMove(client, player)) {
        return;
    }

    ClearPlayerMove(client, player);

    EnemyState* enemy = FindEnemy(enemies, player.targetId);
    if (enemy == nullptr || !IsAlive(enemy->state)) {
        CancelPlayerCombat(player, client, players, enemies, tick);
        return;
    }

    if (!IsPlayerInMeleeWithEnemy(player.x, player.y, *enemy)) {
        if (!TryPathToCombatTarget(player, client, *enemy, map, tick, players, enemies)) {
            CancelPlayerCombat(player, client, players, enemies, tick);
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

    if (!IsPlayerInMeleeWithEnemy(player->x, player->y, enemy)) {
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

void UpdateGoblinBossCombat(EnemyState& enemy, EnemyMovementState& move,
                            std::vector<PlayerState>& players, uint32_t tick) {
    if (enemy.state != EntityState::Combat || enemy.targetId < 0) {
        return;
    }

    const CombatStats& stats = DefaultEntityRegistry().StatsFor(enemy.kind);
    const PlayerState* player = FindPlayerConst(players, enemy.targetId);
    if (player == nullptr || !IsAlive(player->state) || player->state == EntityState::Hit) {
        return;
    }

    if (!IsInMeleeRangeWithEnemy(player->x, player->y, enemy)) {
        return;
    }

    if (PlayerState* mutablePlayer = FindPlayer(players, enemy.targetId)) {
        SnapEntityToCellCenter(mutablePlayer->x, mutablePlayer->y);
    }

    UpdateEnemyFacing(enemy, player->x - enemy.x, player->y - enemy.y);

    if (enemy.anim == PlayerAnim::Attack1 || enemy.anim == PlayerAnim::Attack2) {
        TryApplyBossComboSwingDamage(enemy, move, players, tick);

        if (GoblinAnimFinished(enemy.anim, tick, enemy.animStartTick)) {
            enemy.lastAttackTick = tick;
            switch (move.bossComboPhase) {
                case BossComboPhase::Swing1:
                    move.bossComboPhase = BossComboPhase::PauseAfter1;
                    move.bossComboPhaseStartTick = tick;
                    SetEntityAnim(enemy.anim, enemy.animStartTick, PlayerAnim::Idle, tick);
                    break;
                case BossComboPhase::Swing2:
                    move.bossComboPhase = BossComboPhase::PauseAfter2;
                    move.bossComboPhaseStartTick = tick;
                    SetEntityAnim(enemy.anim, enemy.animStartTick, PlayerAnim::Idle, tick);
                    break;
                case BossComboPhase::Swing3:
                    StartBossComboCycle(enemy, move, tick);
                    break;
                default:
                    SetEntityAnim(enemy.anim, enemy.animStartTick, PlayerAnim::Idle, tick);
                    break;
            }
        }
        return;
    }

    if (move.bossComboPhase == BossComboPhase::PauseAfter1) {
        if (tick - move.bossComboPhaseStartTick >= static_cast<uint32_t>(kComboPauseTicks)) {
            StartBossComboSwing(enemy, move, BossComboPhase::Swing2, 1, tick);
        }
        return;
    }

    if (move.bossComboPhase == BossComboPhase::PauseAfter2) {
        if (tick - move.bossComboPhaseStartTick >= static_cast<uint32_t>(kComboPauseTicks)) {
            StartBossComboSwing(enemy, move, BossComboPhase::Swing3, 2, tick);
        }
        return;
    }

    if (tick - enemy.lastAttackTick < static_cast<uint32_t>(stats.attackCooldownTicks)) {
        return;
    }

    StartBossComboCycle(enemy, move, tick);
}

void TrySpawnGoblinBoss(std::vector<EnemyState>& enemies, std::vector<PlayerState>& players,
                        std::unordered_map<int, ConnectedClient>& clients,
                        std::unordered_map<int, EnemyMovementState>& enemyMovement,
                        const GridMap& map, uint32_t tick) {
    if (HasGoblinBoss(enemies) || !AllRegularGoblinsDefeated(enemies)) {
        return;
    }

    const auto [spawnCol, spawnRow] = PickGoblinBossSpawnCell(map, enemies);
    EnemyState boss = CreateGoblinBossAt(kGoblinBossId, spawnCol, spawnRow);
    boss.stateStartTick = tick;
    boss.animStartTick = tick;
    enemies.push_back(boss);
    EnemyState& spawnedBoss = enemies.back();
    EnemyMovementState& move = enemyMovement[spawnedBoss.id];

    std::vector<PlayerState*> targets;
    targets.reserve(players.size());
    for (PlayerState& player : players) {
        if (player.sceneId == SceneId::Arena && IsAlive(player.state)) {
            targets.push_back(&player);
        }
    }

    if (!targets.empty()) {
        static std::mt19937 bossTargetRng(std::random_device{}());
        PlayerState& target = *targets[bossTargetRng() % targets.size()];
        StartGoblinChase(spawnedBoss, move, target, players, clients, enemies, map, tick);
    }

    std::cout << "[game] goblin boss spawned at (" << spawnCol << ", " << spawnRow << ")\n";
}

void RespawnEnemy(int enemyId, std::vector<EnemyState>& enemies,
                  std::vector<PlayerState>& players,
                  std::unordered_map<int, ConnectedClient>& clients,
                  std::unordered_map<int, EnemyMovementState>& enemyMovement, uint32_t tick) {
    (void)enemyId;
    (void)enemies;
    (void)players;
    (void)clients;
    (void)enemyMovement;
    (void)tick;
}

void RespawnAllDeadEnemies(std::vector<EnemyState>& enemies, std::vector<PlayerState>& players,
                           std::unordered_map<int, ConnectedClient>& clients,
                           std::unordered_map<int, EnemyMovementState>& enemyMovement,
                           uint32_t tick) {
    std::vector<int> deadIds;
    deadIds.reserve(enemies.size());
    for (const EnemyState& enemy : enemies) {
        if (enemy.state == EntityState::Dead) {
            deadIds.push_back(enemy.id);
        }
    }

    for (int enemyId : deadIds) {
        RespawnEnemy(enemyId, enemies, players, clients, enemyMovement, tick);
    }
}

void TryCompletePendingEngage(ConnectedClient& client, PlayerState& player,
                              std::vector<PlayerState>& players,
                              std::vector<EnemyState>& enemies, const GridMap& map,
                              uint32_t tick) {
    if (client.pendingAttackEnemyId < 0) {
        return;
    }

    const int enemyId = client.pendingAttackEnemyId;
    client.pendingAttackEnemyId = -1;

    EnemyState* enemy = FindEnemy(enemies, enemyId);
    if (enemy == nullptr || !IsAlive(enemy->state)) {
        EndPlayerCombat(player, players, enemies, tick);
        TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                         EntityState::Idle, tick);
        return;
    }

    if (!IsPlayerInMeleeWithEnemy(player.x, player.y, *enemy)) {
        client.pendingAttackEnemyId = enemyId;
        if (TryPathToCombatTarget(player, client, *enemy, map, tick, players, enemies)) {
            return;
        }
        EndPlayerCombat(player, players, enemies, tick);
        TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                         EntityState::Idle, tick);
        return;
    }

    BeginCombat(player, *enemy, client, tick, client.comboPhase != PlayerComboPhase::None);
}

bool StepPlayerMovement(PlayerState& player, ConnectedClient& client, uint32_t tick,
                        const std::vector<PlayerState>& players,
                        const std::vector<EnemyState>& enemies) {
    if (!client.hasMoveTarget || client.pathIndex >= client.movePath.size()) {
        return false;
    }

    float remainingStep = kPlayerSpeed * kTickDuration;
    static constexpr float kArriveEpsilon = 0.5f;
    bool moved = false;

    while (remainingStep > 0.0f && client.pathIndex < client.movePath.size()) {
        const auto& waypoint = client.movePath[client.pathIndex];
        const int waypointCol = waypoint.first;
        const int waypointRow = waypoint.second;
        if (IsCellOccupied(waypointCol, waypointRow, players, enemies, player.id, -1)) {
            ClearPlayerMove(client, player);
            return moved;
        }

        const float targetX = CellCenterX(waypointCol);
        const float targetY = CellCenterY(waypointRow);
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
                        std::vector<PlayerState>& players, std::vector<EnemyState>& enemies,
                        const GridMap& map, uint32_t tick) {
    if (player.state == EntityState::Dead) {
        ClearPlayerMove(client, player);
        return;
    }

    if (player.state == EntityState::Disengaging) {
        ClearPlayerMove(client, player);
        UpdatePlayerDisengage(player, client, map, tick, players, enemies);
        return;
    }

    if (IsVoluntaryMove(client, player)) {
        if (player.state == EntityState::Combat) {
            BreakPlayerCombatLink(player, players, enemies, tick);
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

        StepPlayerMovement(player, client, tick, players, enemies);
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
                RecoverPlayerAfterHit(player, client, players, enemies, map, tick);
            }
        }
        return;
    }

    if (player.state == EntityState::Moving || client.hasMoveTarget) {
        StepPlayerMovement(player, client, tick, players, enemies);
        if (!client.hasMoveTarget) {
            TryCompletePendingEngage(client, player, players, enemies, map, tick);
            if (player.state == EntityState::Moving) {
                TransitionEntity(player.state, player.stateStartTick, player.anim,
                                 player.animStartTick, EntityState::Idle, tick);
            }
        }
    }

    if (player.state == EntityState::Combat) {
        UpdatePlayerCombo(player, client, players, enemies, map, tick);
    }
}

void ProcessDeadEnemyRespawns(std::vector<EnemyState>& enemies, std::vector<PlayerState>& players,
                              std::unordered_map<int, ConnectedClient>& clients,
                              std::unordered_map<int, EnemyMovementState>& enemyMovement,
                              uint32_t tick) {
    (void)tick;
    const GridMap& map = ArenaGridMap();
    TrySpawnGoblinBoss(enemies, players, clients, enemyMovement, map, tick);
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
                    IsPlayerInMeleeWithEnemy(player->x, player->y, enemy)) {
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
            !IsPlayerInMeleeWithEnemy(player->x, player->y, enemy)) {
            enemy.targetId = FindReplacementEnemyTarget(enemy, players);
            if (enemy.targetId < 0) {
                TransitionEntity(enemy.state, enemy.stateStartTick, enemy.anim, enemy.animStartTick,
                                 EntityState::Idle, tick);
            }
            return;
        }

        if (IsGoblinBoss(enemy)) {
            UpdateGoblinBossCombat(enemy, move, players, tick);
        } else {
            UpdateGoblinCombat(enemy, players, tick);
        }
        return;
    }

    if (IsGoblinBoss(enemy) || !InPatrolIdle(move, tick)) {
        if (PlayerState* aggroTarget = FindGoblinAggroTarget(enemy, players)) {
            if (IsPlayerInMeleeWithEnemy(aggroTarget->x, aggroTarget->y, enemy)) {
                ClearEnemyMove(move);
                ClearEnemyChase(move);
                TryBeginGoblinCombat(enemy, *aggroTarget, players, clients, enemies, move, tick);
                return;
            }

            if (!move.chasingPlayer || move.chaseTargetId != aggroTarget->id ||
                !move.hasMoveTarget) {
                StartGoblinChase(enemy, move, *aggroTarget, players, clients, enemies, map, tick);
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
        StepEnemyMovement(enemy, move, map, players, enemies);
        if (!move.hasMoveTarget) {
            if (move.chasingPlayer && move.chaseTargetId >= 0) {
                PlayerState* chaseTarget = FindPlayer(players, move.chaseTargetId);
                if (chaseTarget != nullptr) {
                    SnapEntityToCellCenter(enemy.x, enemy.y);
                    TryCompleteGoblinChase(enemy, move, *chaseTarget, players, clients, enemies,
                                           map, tick);
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

    if (enemy.state == EntityState::Idle && !IsGoblinBoss(enemy) && !move.chasingPlayer &&
        !move.hasMoveTarget && !InPatrolIdle(move, tick)) {
        StartNextPatrolLeg(enemy, move, map, players, enemies, tick);
    }
}

void HandlePlayerEngageRequest(PlayerState& player, ConnectedClient& client,
                               std::vector<PlayerState>& players,
                               std::vector<EnemyState>& enemies, int enemyId,
                               const GridMap& map, uint32_t tick) {
    if (player.sceneId != SceneId::Arena || !CanAcceptAttackIntent(player.state)) {
        return;
    }

    EnemyState* enemy = FindEnemy(enemies, enemyId);
    if (enemy == nullptr || !IsAlive(enemy->state)) {
        return;
    }

    if (player.targetId >= 0 && player.targetId != enemyId) {
        EndPlayerCombat(player, players, enemies, tick);
    }

    ClearPlayerMove(client, player);
    client.pendingAttackEnemyId = enemyId;
    player.targetId = enemyId;

    if (IsPlayerInMeleeWithEnemy(player.x, player.y, *enemy)) {
        client.pendingAttackEnemyId = -1;
        BeginCombat(player, *enemy, client, tick);
        return;
    }

    const int playerCol = WorldToCellCol(player.x);
    const int playerRow = WorldToCellRow(player.y);
    const int enemyCol = WorldToCellCol(enemy->x);
    const int enemyRow = WorldToCellRow(enemy->y);
    const std::optional<GridPoint> approach =
        FindBestAdjacentApproachTile(map, playerCol, playerRow, enemyCol, enemyRow, &players,
                                     &enemies, player.id, enemyId);
    if (!approach.has_value()) {
        client.pendingAttackEnemyId = -1;
        player.targetId = -1;
        return;
    }

    if (!StartPlayerPath(client, player, map, approach->first, approach->second, tick, players,
                         enemies)) {
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
    sessionPhase_ = SessionPhase::HubIdle;
    sessionPhaseEndsAtTick_ = 0;
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

            const GridMap& map = HubGridMap();
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
            player.sceneId = SceneId::Hub;
            player.isReady = false;
            players_.push_back(player);

            const WorldState worldState = BuildWorldStateForClient(incoming.clientId);
            SendToClient(incoming.clientId, incoming.transport,
                         MakeJoinAccepted(incoming.clientId, worldState.players, worldState.enemies,
                                          worldState.session));
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
            if (player == nullptr || !CanAcceptMoveIntent(player->state) ||
                player->sceneId == SceneId::Arena && player->state == EntityState::Dead) {
                return;
            }

            const int col = incoming.message.moveRequest.col;
            const int row = incoming.message.moveRequest.row;
            const GridMap& map = MapForScene(player->sceneId);
            if (!IsValidCell(col, row) || !map.IsWalkable(col, row)) {
                return;
            }

            ConnectedClient& client = it->second;
            if (player->state == EntityState::Combat && player->targetId >= 0) {
                const EnemyState* enemy = FindEnemyConst(enemies_, player->targetId);
                if (enemy != nullptr &&
                    IsPlayerInMeleeWithEnemy(player->x, player->y, *enemy)) {
                    client.pendingMoveCol = col;
                    client.pendingMoveRow = row;
                    DisengagePlayerCombat(*player, client, enemies_, players_, map, tick_);
                    break;
                }
            }

            ResetPlayerCombo(client);
            client.pendingAttackEnemyId = -1;
            BreakPlayerCombatLink(*player, players_, enemies_, tick_);
            ClearPlayerMove(client, *player);

            if (!StartPlayerPath(client, *player, map, col, row, tick_, players_, enemies_)) {
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

            HandlePlayerEngageRequest(*player, it->second, players_, enemies_,
                                      incoming.message.attackRequest.enemyId,
                                      ArenaGridMap(), tick_);
            break;
        }
        case MessageType::SetReadyRequest: {
            auto it = clients_.find(incoming.clientId);
            if (it == clients_.end() || !it->second.hasJoined) {
                return;
            }

            HandleSetReady(incoming.clientId, incoming.message.setReadyRequest.ready);
            break;
        }
        case MessageType::SetArenaResetRequest: {
            auto it = clients_.find(incoming.clientId);
            if (it == clients_.end() || !it->second.hasJoined) {
                return;
            }

            HandleSetArenaReset(incoming.clientId,
                                incoming.message.setArenaResetRequest.selected);
            break;
        }
        case MessageType::ReturnToHubRequest: {
            auto it = clients_.find(incoming.clientId);
            if (it == clients_.end() || !it->second.hasJoined) {
                return;
            }

            HandleReturnToHub(incoming.clientId);
            break;
        }
        case MessageType::RejoinArenaRequest: {
            auto it = clients_.find(incoming.clientId);
            if (it == clients_.end() || !it->second.hasJoined) {
                return;
            }

            HandleRejoinArena(incoming.clientId);
            break;
        }
        case MessageType::RespawnInArenaRequest: {
            auto it = clients_.find(incoming.clientId);
            if (it == clients_.end() || !it->second.hasJoined) {
                return;
            }

            HandleRespawnInArena(incoming.clientId);
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

            CancelPlayerCombat(*player, it->second, players_, enemies_, tick_);
            break;
        }
        case MessageType::DisengageRequest: {
            auto it = clients_.find(incoming.clientId);
            if (it == clients_.end() || !it->second.hasJoined) {
                return;
            }

            PlayerState* player = FindPlayer(players_, incoming.clientId);
            if (player == nullptr || !CanAcceptDisengageIntent(player->state)) {
                return;
            }

            DisengagePlayerCombat(*player, it->second, enemies_, players_,
                                  MapForScene(player->sceneId), tick_);
            break;
        }
        case MessageType::RespawnEnemyRequest: {
            auto it = clients_.find(incoming.clientId);
            if (it == clients_.end() || !it->second.hasJoined) {
                return;
            }

            PlayerState* player = FindPlayer(players_, incoming.clientId);
            if (player == nullptr || player->sceneId != SceneId::Arena ||
                sessionPhase_ != SessionPhase::ArenaActive) {
                return;
            }

            const int enemyId = incoming.message.respawnEnemyRequest.enemyId;
            if (enemyId <= kRespawnAllDeadEnemiesId) {
                RespawnAllDeadEnemies(enemies_, players_, clients_, enemyMovement_, tick_);
            } else {
                RespawnEnemy(enemyId, enemies_, players_, clients_, enemyMovement_, tick_);
            }
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
            CancelPlayerCombat(*player, clientIt->second, players_, enemies_, tick_);
        }
        (void)player;
    }

    clients_.erase(clientId);
    transportByClientId_.erase(clientId);
    players_.erase(
        std::remove_if(players_.begin(), players_.end(),
                       [&](const PlayerState& player) { return player.id == clientId; }),
        players_.end());
    BroadcastToAll(MakePlayerLeft(clientId));
    std::cout << "[game] client " << clientId << " removed\n";
}

void GameServer::SimulateTick() {
    UpdateSession();

    for (PlayerState& player : players_) {
        auto clientIt = clients_.find(player.id);
        if (clientIt == clients_.end()) {
            continue;
        }

        const GridMap& map = MapForScene(player.sceneId);
        UpdatePlayerEntity(player, clientIt->second, players_, enemies_, map, tick_);

        if (player.sceneId == SceneId::Arena && player.state != EntityState::Combat &&
            player.state != EntityState::Hit && player.state != EntityState::Dead) {
            const CombatStats& stats = DefaultEntityRegistry().StatsFor(kPlayerEntityId);
            if (stats.shieldRegenPerTick > 0 && player.shield < stats.maxShield &&
                tick_ % 4 == 0) {
                player.shield =
                    std::min(stats.maxShield, player.shield + stats.shieldRegenPerTick);
            }
        }

        if (player.state != EntityState::Dead) {
            const float margin = kPlayerRadius;
            player.x = std::clamp(player.x, margin, kWorldWidth - margin);
            player.y = std::clamp(player.y, margin, kWorldHeight - margin);
        }
    }

    if (sessionPhase_ == SessionPhase::ArenaActive) {
        const GridMap& arenaMap = ArenaGridMap();
        if (CountPlayersInScene(SceneId::Arena) > 0) {
            for (EnemyState& enemy : enemies_) {
                UpdateEnemyEntity(enemy, enemyMovement_[enemy.id], players_, clients_, enemies_,
                                  arenaMap, tick_);
            }

            ProcessDeadEnemyRespawns(enemies_, players_, clients_, enemyMovement_, tick_);

            if (arenaVictoryEndsAtTick_ == 0 && IsGoblinBossDefeated(enemies_)) {
                arenaVictoryEndsAtTick_ = tick_ + kArenaVictoryDelayTicks;
                allDeadReturnAtTick_ = 0;
                std::cout << "[game] goblin boss defeated, returning to hub in "
                          << kArenaVictoryDelaySeconds << "s\n";
            }
        }
    }
}

void GameServer::BroadcastWorldState() {
    std::vector<std::pair<int, TransportKind>> recipients;
    recipients.reserve(transportByClientId_.size());
    for (const auto& [clientId, transport] : transportByClientId_) {
        recipients.emplace_back(clientId, transport);
    }

    for (const auto& [clientId, transport] : recipients) {
        if (transportByClientId_.find(clientId) == transportByClientId_.end()) {
            continue;
        }

        const WorldState worldState = BuildWorldStateForClient(clientId);
        if (!SendToClient(clientId, transport,
                          MakeWorldState(worldState.tick, worldState.players, worldState.enemies,
                                         worldState.session))) {
            EnqueueDisconnect(clientId, transport);
        }
    }
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

std::vector<PlayerState> GameServer::BuildPlayerSnapshot(SceneId scene) const {
    std::vector<PlayerState> snapshot;
    snapshot.reserve(players_.size());
    for (const PlayerState& player : players_) {
        if (player.sceneId == scene) {
            snapshot.push_back(player);
        }
    }
    return snapshot;
}

SessionSnapshot GameServer::BuildSessionSnapshot() const {
    SessionSnapshot session;
    session.phase = sessionPhase_;
    session.phaseEndsAtTick = sessionPhaseEndsAtTick_;
    session.allDeadReturnAtTick = allDeadReturnAtTick_;
    session.arenaJoinOpensAtTick = arenaJoinOpensAtTick_;
    session.arenaSessionEndsAtTick = arenaSessionEndsAtTick_;
    session.arenaVictoryEndsAtTick = arenaVictoryEndsAtTick_;
    session.hubPlayerCount = CountPlayersInScene(SceneId::Hub);
    session.arenaPlayerCount = CountPlayersInScene(SceneId::Arena);
    for (const PlayerState& player : players_) {
        if (player.sceneId == SceneId::Hub && player.isReady) {
            session.readyPlayerIds.push_back(player.id);
        }
        if (player.sceneId == SceneId::Hub && player.wantsArenaReset) {
            session.arenaResetPlayerIds.push_back(player.id);
        }
    }
    return session;
}

WorldState GameServer::BuildWorldStateForClient(int clientId) const {
    WorldState worldState;
    worldState.tick = tick_;
    worldState.session = BuildSessionSnapshot();

    const PlayerState* viewer = nullptr;
    for (const PlayerState& player : players_) {
        if (player.id == clientId) {
            viewer = &player;
            break;
        }
    }

    const SceneId scene = viewer != nullptr ? viewer->sceneId : SceneId::Hub;
    worldState.players = BuildPlayerSnapshot(scene);
    if (scene == SceneId::Arena) {
        worldState.enemies = enemies_;
    }
    return worldState;
}

int GameServer::CountPlayersInScene(SceneId scene) const {
    int count = 0;
    for (const PlayerState& player : players_) {
        if (player.sceneId == scene) {
            ++count;
        }
    }
    return count;
}

int GameServer::CountReadyHubPlayers() const {
    int count = 0;
    for (const PlayerState& player : players_) {
        if (player.sceneId == SceneId::Hub && player.isReady) {
            ++count;
        }
    }
    return count;
}

int GameServer::CountArenaResetHubPlayers() const {
    int count = 0;
    for (const PlayerState& player : players_) {
        if (player.sceneId == SceneId::Hub && player.wantsArenaReset) {
            ++count;
        }
    }
    return count;
}

void GameServer::ClearAllReady() {
    for (PlayerState& player : players_) {
        player.isReady = false;
    }
}

void GameServer::ClearAllArenaReset() {
    for (PlayerState& player : players_) {
        player.wantsArenaReset = false;
    }
}

bool GameServer::HasActiveArenaSession() const {
    return arenaSessionEndsAtTick_ > 0 || !enemies_.empty();
}

void GameServer::ResetPlayerForScene(PlayerState& player, ConnectedClient* client, SceneId scene,
                                     int spawnCol, int spawnRow) {
    if (client != nullptr) {
        CancelPlayerCombat(player, *client, players_, enemies_, tick_);
        ResetPlayerCombo(*client);
        ClearPlayerMove(*client, player);
        ClearPlayerDisengage(*client);
        ClearPendingMoveAfterDisengage(*client);
        client->pendingAttackEnemyId = -1;
    }

    const CombatStats& stats = DefaultEntityRegistry().StatsFor(kPlayerEntityId);
    player.sceneId = scene;
    player.isReady = false;
    player.wantsArenaReset = false;
    player.x = CellCenterX(spawnCol);
    player.y = CellCenterY(spawnRow);
    player.hp = stats.maxHp;
    player.shield = stats.maxShield;
    player.targetId = -1;
    player.moveTargetCol = -1;
    player.moveTargetRow = -1;
    player.state = EntityState::Idle;
    player.anim = PlayerAnim::Idle;
    player.stateStartTick = tick_;
    player.animStartTick = tick_;
    if (scene == SceneId::Arena) {
        player.arenaRejoinAtTick = 0;
    }
}

bool GameServer::CanPlayerRejoinArena(const PlayerState& player) const {
    if (sessionPhase_ != SessionPhase::ArenaActive) {
        return false;
    }
    if (player.sceneId != SceneId::Hub) {
        return false;
    }

    const uint32_t opensAt =
        std::max(arenaJoinOpensAtTick_, player.arenaRejoinAtTick);
    return tick_ >= opensAt;
}

bool GameServer::CanPlayerRespawnInArena(const PlayerState& player) const {
    if (sessionPhase_ != SessionPhase::ArenaActive) {
        return false;
    }
    if (player.sceneId != SceneId::Arena || player.state != EntityState::Dead) {
        return false;
    }

    return tick_ >= player.stateStartTick + kArenaDeathRespawnDelayTicks;
}

void GameServer::ReturnAllArenaPlayersToHub() {
    const GridMap& hubMap = HubGridMap();
    const int returningCount = CountPlayersInScene(SceneId::Arena);
    if (returningCount <= 0) {
        return;
    }

    const std::vector<std::pair<int, int>> spawnCells =
        AllocateSpawnCells(hubMap, returningCount);
    int spawnIndex = 0;

    for (PlayerState& player : players_) {
        if (player.sceneId != SceneId::Arena) {
            continue;
        }

        ConnectedClient* client = FindClient(clients_, player.id);
        const auto [spawnCol, spawnRow] = spawnCells[static_cast<size_t>(spawnIndex++)];
        ResetPlayerForScene(player, client, SceneId::Hub, spawnCol, spawnRow);
        player.arenaRejoinAtTick = tick_ + kArenaRejoinDelayTicks;
    }
}

void GameServer::HandleRejoinArena(int clientId) {
    if (sessionPhase_ != SessionPhase::ArenaActive) {
        return;
    }

    PlayerState* player = FindPlayer(players_, clientId);
    if (player == nullptr || player->sceneId != SceneId::Hub) {
        return;
    }

    if (!CanPlayerRejoinArena(*player)) {
        return;
    }

    const GridMap& arenaMap = ArenaGridMap();
    const std::vector<std::pair<int, int>> spawnCells = AllocateSpawnCells(arenaMap, 1);
    ConnectedClient* client = FindClient(clients_, player->id);
    const auto [spawnCol, spawnRow] = spawnCells.front();
    ResetPlayerForScene(*player, client, SceneId::Arena, spawnCol, spawnRow);
    player->arenaRejoinAtTick = 0;
    allDeadReturnAtTick_ = 0;
    std::cout << "[game] player " << clientId << " rejoined arena\n";
}

void GameServer::HandleRespawnInArena(int clientId) {
    if (sessionPhase_ != SessionPhase::ArenaActive) {
        return;
    }

    PlayerState* player = FindPlayer(players_, clientId);
    if (player == nullptr || !CanPlayerRespawnInArena(*player)) {
        return;
    }

    const GridMap& arenaMap = ArenaGridMap();
    const std::vector<std::pair<int, int>> spawnCells = AllocateSpawnCells(arenaMap, 1);
    ConnectedClient* client = FindClient(clients_, player->id);
    const auto [spawnCol, spawnRow] = spawnCells.front();
    ResetPlayerForScene(*player, client, SceneId::Arena, spawnCol, spawnRow);
    allDeadReturnAtTick_ = 0;
    std::cout << "[game] player " << clientId << " respawned in arena\n";
}

void GameServer::StartArena() {
    std::vector<int> readyPlayerIds;
    for (const PlayerState& player : players_) {
        if (player.sceneId == SceneId::Hub && player.isReady) {
            readyPlayerIds.push_back(player.id);
        }
    }
    if (readyPlayerIds.empty()) {
        return;
    }

    const GridMap& arenaMap = ArenaGridMap();
    const std::vector<std::pair<int, int>> spawnCells =
        AllocateSpawnCells(arenaMap, static_cast<int>(readyPlayerIds.size()));

    for (size_t i = 0; i < readyPlayerIds.size(); ++i) {
        PlayerState* player = FindPlayer(players_, readyPlayerIds[i]);
        if (player == nullptr) {
            continue;
        }

        ConnectedClient* client = FindClient(clients_, player->id);
        const auto [spawnCol, spawnRow] = spawnCells[i];
        ResetPlayerForScene(*player, client, SceneId::Arena, spawnCol, spawnRow);
    }

    const bool resumeExistingArena =
        !enemies_.empty() && arenaSessionEndsAtTick_ > tick_;

    if (!resumeExistingArena) {
        enemies_.clear();
        enemyMovement_.clear();
        enemies_ = CreateDefaultEnemies();
        if (EnemiesShareTiles(enemies_)) {
            std::cerr << "[game] warning: goblins spawned on shared tiles\n";
        }
        for (EnemyState& enemy : enemies_) {
            enemy.stateStartTick = tick_;
            enemy.animStartTick = tick_;
            InitializeGoblinMovement(enemyMovement_[enemy.id], arenaMap, enemy, tick_);
        }
        arenaSessionEndsAtTick_ = tick_ + kArenaDurationTicks;
    }

    sessionPhase_ = SessionPhase::ArenaActive;
    sessionPhaseEndsAtTick_ = arenaSessionEndsAtTick_;
    arenaJoinOpensAtTick_ = tick_ + kArenaRejoinDelayTicks;
    allDeadReturnAtTick_ = 0;
    arenaVictoryEndsAtTick_ = 0;
    ClearAllArenaReset();
    std::cout << "[game] arena " << (resumeExistingArena ? "resumed" : "started")
              << " with " << readyPlayerIds.size() << " players\n";
}

void GameServer::ResetArena() {
    const GridMap& hubMap = HubGridMap();
    const int returningCount = CountPlayersInScene(SceneId::Arena);
    if (returningCount > 0) {
        const std::vector<std::pair<int, int>> spawnCells =
            AllocateSpawnCells(hubMap, returningCount);
        int spawnIndex = 0;
        for (PlayerState& player : players_) {
            if (player.sceneId != SceneId::Arena) {
                continue;
            }

            ConnectedClient* client = FindClient(clients_, player.id);
            const auto [spawnCol, spawnRow] = spawnCells[static_cast<size_t>(spawnIndex++)];
            ResetPlayerForScene(player, client, SceneId::Hub, spawnCol, spawnRow);
        }
    }

    enemies_.clear();
    enemyMovement_.clear();
    sessionPhase_ = SessionPhase::HubIdle;
    sessionPhaseEndsAtTick_ = 0;
    allDeadReturnAtTick_ = 0;
    arenaJoinOpensAtTick_ = 0;
    arenaSessionEndsAtTick_ = 0;
    arenaVictoryEndsAtTick_ = 0;
    ClearAllReady();
    ClearAllArenaReset();
    for (PlayerState& player : players_) {
        player.arenaRejoinAtTick = 0;
    }
    std::cout << "[game] arena reset by unanimous vote\n";
}

void GameServer::EndArena() {
    const GridMap& hubMap = HubGridMap();
    const int returningCount = CountPlayersInScene(SceneId::Arena);
    const std::vector<std::pair<int, int>> spawnCells =
        AllocateSpawnCells(hubMap, returningCount);
    int spawnIndex = 0;

    for (PlayerState& player : players_) {
        if (player.sceneId != SceneId::Arena) {
            continue;
        }

        ConnectedClient* client = FindClient(clients_, player.id);
        const auto [spawnCol, spawnRow] = spawnCells[static_cast<size_t>(spawnIndex++)];
        ResetPlayerForScene(player, client, SceneId::Hub, spawnCol, spawnRow);
    }

    enemies_.clear();
    enemyMovement_.clear();
    sessionPhase_ = SessionPhase::HubIdle;
    sessionPhaseEndsAtTick_ = 0;
    allDeadReturnAtTick_ = 0;
    arenaJoinOpensAtTick_ = 0;
    arenaSessionEndsAtTick_ = 0;
    arenaVictoryEndsAtTick_ = 0;
    for (PlayerState& player : players_) {
        player.arenaRejoinAtTick = 0;
    }
    ClearAllReady();
    ClearAllArenaReset();
    std::cout << "[game] arena ended, players returned to hub\n";
}

void GameServer::HandleReturnToHub(int clientId) {
    if (sessionPhase_ != SessionPhase::ArenaActive) {
        return;
    }

    PlayerState* player = FindPlayer(players_, clientId);
    if (player == nullptr || player->sceneId != SceneId::Arena) {
        return;
    }

    const GridMap& hubMap = HubGridMap();
    const std::vector<std::pair<int, int>> spawnCells = AllocateSpawnCells(hubMap, 1);
    ConnectedClient* client = FindClient(clients_, player->id);
    const auto [spawnCol, spawnRow] = spawnCells.front();
    ResetPlayerForScene(*player, client, SceneId::Hub, spawnCol, spawnRow);
    player->arenaRejoinAtTick = tick_ + kArenaRejoinDelayTicks;
}

void GameServer::HandleSetReady(int clientId, bool ready) {
    if (sessionPhase_ == SessionPhase::ArenaActive &&
        CountPlayersInScene(SceneId::Arena) > 0) {
        return;
    }

    PlayerState* player = FindPlayer(players_, clientId);
    if (player == nullptr || player->sceneId != SceneId::Hub) {
        return;
    }

    player->isReady = ready;
    if (!ready) {
        return;
    }

    const int hubCount = CountPlayersInScene(SceneId::Hub);
    const int readyCount = CountReadyHubPlayers();

    if (hubCount <= 1) {
        StartArena();
        return;
    }

    if (sessionPhase_ == SessionPhase::HubIdle) {
        sessionPhase_ = SessionPhase::Lobby;
        sessionPhaseEndsAtTick_ = tick_ + kLobbyDurationTicks;
    }

    if (readyCount >= hubCount) {
        StartArena();
    }
}

void GameServer::HandleSetArenaReset(int clientId, bool selected) {
    if (!HasActiveArenaSession()) {
        return;
    }

    PlayerState* player = FindPlayer(players_, clientId);
    if (player == nullptr || player->sceneId != SceneId::Hub) {
        return;
    }

    player->wantsArenaReset = selected;
    if (!selected) {
        return;
    }

    const int hubCount = CountPlayersInScene(SceneId::Hub);
    const int resetCount = CountArenaResetHubPlayers();
    if (hubCount > 0 && resetCount >= hubCount) {
        ResetArena();
    }
}

void GameServer::UpdateSession() {
    if (arenaVictoryEndsAtTick_ > 0 && tick_ >= arenaVictoryEndsAtTick_) {
        EndArena();
        return;
    }

    if (arenaSessionEndsAtTick_ > 0 && tick_ >= arenaSessionEndsAtTick_) {
        EndArena();
        return;
    }

    if (sessionPhase_ == SessionPhase::ArenaActive &&
        CountPlayersInScene(SceneId::Arena) == 0 && allDeadReturnAtTick_ == 0) {
        sessionPhase_ = SessionPhase::Lobby;
        sessionPhaseEndsAtTick_ = tick_ + kLobbyDurationTicks;
        ClearAllReady();
    }

    if (sessionPhase_ == SessionPhase::Lobby) {
        const int hubCount = CountPlayersInScene(SceneId::Hub);
        const int readyCount = CountReadyHubPlayers();

        if (readyCount == 0) {
            sessionPhase_ = SessionPhase::HubIdle;
            sessionPhaseEndsAtTick_ = 0;
            return;
        }

        if (hubCount <= 1 && readyCount == 1) {
            StartArena();
            return;
        }

        if (hubCount >= 2 && readyCount >= hubCount) {
            StartArena();
            return;
        }

        if (sessionPhaseEndsAtTick_ > 0 && tick_ >= sessionPhaseEndsAtTick_) {
            if (readyCount > 0) {
                StartArena();
            } else {
                sessionPhase_ = SessionPhase::HubIdle;
                sessionPhaseEndsAtTick_ = 0;
            }
        }
        return;
    }

    if (sessionPhase_ == SessionPhase::ArenaActive) {
        if (arenaVictoryEndsAtTick_ > 0) {
            return;
        }

        int aliveArenaCount = 0;
        for (const PlayerState& player : players_) {
            if (player.sceneId != SceneId::Arena) {
                continue;
            }
            if (IsAlive(player.state)) {
                ++aliveArenaCount;
            }
        }

        if (aliveArenaCount == 0 && CountPlayersInScene(SceneId::Arena) > 0) {
            if (allDeadReturnAtTick_ == 0) {
                allDeadReturnAtTick_ =
                    tick_ + kArenaDeathRespawnDelayTicks + kAllDeadReturnTicks;
            }
            if (tick_ >= allDeadReturnAtTick_) {
                ReturnAllArenaPlayersToHub();
                allDeadReturnAtTick_ = 0;
            }
        } else {
            allDeadReturnAtTick_ = 0;
        }
    }
}

uint32_t GameServer::NowMs() const {
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now().time_since_epoch();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

}  // namespace net
