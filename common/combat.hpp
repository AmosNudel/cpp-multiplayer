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

std::optional<GridPoint> FindBestAdjacentApproachTile(const GridMap& map, int startCol,
                                                      int startRow, int targetCol,
                                                      int targetRow);

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

bool ApplyDamageToPlayer(PlayerState& player, int damage, uint32_t tick, bool critical);
bool ApplyDamageToEnemy(EnemyState& enemy, int damage, uint32_t tick, bool critical);

}  // namespace net
