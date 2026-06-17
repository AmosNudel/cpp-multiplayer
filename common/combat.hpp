#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include "common/entity_state.hpp"
#include "common/enemies.hpp"
#include "common/grid_map.hpp"
#include "common/pathfinding.hpp"
#include "common/protocol.hpp"

namespace net {

int ManhattanCellDistance(int colA, int rowA, int colB, int rowB);
bool IsInMeleeRange(float ax, float ay, float bx, float by);
bool IsInMeleeRangeWithEnemy(float ax, float ay, const EnemyState& enemy);
bool EnemyOccupiesCell(const EnemyState& enemy, int col, int row);
std::vector<std::pair<int, int>> CollectAdjacentCellsAroundEnemy(const EnemyState& enemy);

void SnapEntityToCellCenter(float& x, float& y);

bool IsCellOccupied(int col, int row, const std::vector<PlayerState>& players,
                    const std::vector<EnemyState>& enemies, int ignorePlayerId = -1,
                    int ignoreEnemyId = -1);

std::optional<GridPoint> FindRetreatTile(const GridMap& map, int playerCol, int playerRow,
                                         int enemyCol, int enemyRow);

std::optional<GridPoint> FindBestAdjacentApproachTile(const GridMap& map, int startCol,
                                                      int startRow, int targetCol,
                                                      int targetRow,
                                                      const std::vector<PlayerState>* players =
                                                          nullptr,
                                                      const std::vector<EnemyState>* enemies =
                                                          nullptr,
                                                      int ignorePlayerId = -1,
                                                      int ignoreEnemyId = -1);

std::optional<GridPoint> FindBestAdjacentApproachTileForEnemy(
    const GridMap& map, int startCol, int startRow, const EnemyState& target,
    const std::vector<PlayerState>* players = nullptr,
    const std::vector<EnemyState>* enemies = nullptr, int ignorePlayerId = -1,
    int ignoreEnemyId = -1);

void TransitionEntity(EntityState& state, uint32_t& stateStartTick, PlayerAnim& anim,
                      uint32_t& animStartTick, EntityState newState, uint32_t tick);

void TransitionEntityState(EntityState& state, uint32_t& stateStartTick, EntityState newState,
                           uint32_t tick);

void SetEntityAnim(PlayerAnim& anim, uint32_t& animStartTick, PlayerAnim newAnim,
                   uint32_t tick);

void RestartEntityAnim(PlayerAnim& anim, uint32_t& animStartTick, PlayerAnim newAnim,
                      uint32_t tick);

int CurrentAnimFrame(PlayerAnim anim, uint32_t tick, uint32_t animStartTick);
bool IsAnimFinished(PlayerAnim anim, uint32_t tick, uint32_t animStartTick);

bool RollCriticalHit(int attackerId, int targetId, uint32_t tick, int chancePercent);

inline bool ShouldInterruptWithHitStun(EntityState state) {
    return state != EntityState::Combat && state != EntityState::Disengaging;
}

bool ApplyDamageToPlayer(PlayerState& player, int damage, uint32_t tick, bool critical);
bool ApplyDamageToEnemy(EnemyState& enemy, int damage, uint32_t tick, bool critical);

}  // namespace net
