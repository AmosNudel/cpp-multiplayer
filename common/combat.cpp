#include "common/combat.hpp"

#include <cmath>
#include <limits>

#include "common/grid.hpp"

namespace net {

int ManhattanCellDistance(int colA, int rowA, int colB, int rowB) {
    return std::abs(colA - colB) + std::abs(rowA - rowB);
}

bool IsInMeleeRange(float ax, float ay, float bx, float by) {
    const int colA = WorldToCellCol(ax);
    const int rowA = WorldToCellRow(ay);
    const int colB = WorldToCellCol(bx);
    const int rowB = WorldToCellRow(by);
    return ManhattanCellDistance(colA, rowA, colB, rowB) == 1;
}

void SnapEntityToCellCenter(float& x, float& y) {
    x = CellCenterX(WorldToCellCol(x));
    y = CellCenterY(WorldToCellRow(y));
}

bool IsCellOccupied(int col, int row, const std::vector<PlayerState>& players,
                    const std::vector<EnemyState>& enemies, int ignorePlayerId,
                    int ignoreEnemyId) {
    for (const PlayerState& player : players) {
        if (player.id == ignorePlayerId || !IsAlive(player.state)) {
            continue;
        }
        if (WorldToCellCol(player.x) == col && WorldToCellRow(player.y) == row) {
            return true;
        }
    }

    for (const EnemyState& enemy : enemies) {
        if (enemy.id == ignoreEnemyId || !IsAlive(enemy.state)) {
            continue;
        }
        if (WorldToCellCol(enemy.x) == col && WorldToCellRow(enemy.y) == row) {
            return true;
        }
    }

    return false;
}

std::optional<GridPoint> FindRetreatTile(const GridMap& map, int playerCol, int playerRow,
                                         int enemyCol, int enemyRow) {
    const int retreatCol = playerCol + (playerCol - enemyCol);
    const int retreatRow = playerRow + (playerRow - enemyRow);
    if (!IsValidCell(retreatCol, retreatRow) || !map.IsWalkable(retreatCol, retreatRow)) {
        return std::nullopt;
    }
    return GridPoint{retreatCol, retreatRow};
}

std::optional<GridPoint> FindBestAdjacentApproachTile(const GridMap& map, int startCol,
                                                      int startRow, int targetCol,
                                                      int targetRow) {
    static constexpr int kDirections[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1},
    };

    std::optional<GridPoint> best;
    int bestPathLength = std::numeric_limits<int>::max();
    int bestSideAlignment = std::numeric_limits<int>::min();
    const int towardStartCol = startCol - targetCol;
    const int towardStartRow = startRow - targetRow;

    for (const auto& direction : kDirections) {
        const int neighborCol = targetCol + direction[0];
        const int neighborRow = targetRow + direction[1];
        if (!IsValidCell(neighborCol, neighborRow) ||
            !map.IsWalkable(neighborCol, neighborRow)) {
            continue;
        }

        const std::vector<GridPoint> path =
            FindPath(map, startCol, startRow, neighborCol, neighborRow);
        if (path.empty()) {
            continue;
        }

        const int pathLength = static_cast<int>(path.size());
        const int sideAlignment =
            direction[0] * towardStartCol + direction[1] * towardStartRow;
        if (pathLength < bestPathLength ||
            (pathLength == bestPathLength && sideAlignment > bestSideAlignment)) {
            bestPathLength = pathLength;
            bestSideAlignment = sideAlignment;
            best = GridPoint{neighborCol, neighborRow};
        }
    }

    return best;
}

void TransitionEntity(EntityState& state, uint32_t& stateStartTick, PlayerAnim& anim,
                      uint32_t& animStartTick, EntityState newState, uint32_t tick) {
    if (state == newState) {
        return;
    }

    state = newState;
    stateStartTick = tick;
    anim = AnimForEntityState(newState);
    animStartTick = tick;
}

void TransitionEntityState(EntityState& state, uint32_t& stateStartTick, EntityState newState,
                           uint32_t tick) {
    if (state == newState) {
        return;
    }

    state = newState;
    stateStartTick = tick;
}

void SetEntityAnim(PlayerAnim& anim, uint32_t& animStartTick, PlayerAnim newAnim,
                   uint32_t tick) {
    if (anim != newAnim) {
        anim = newAnim;
        animStartTick = tick;
    }
}

void RestartEntityAnim(PlayerAnim& anim, uint32_t& animStartTick, PlayerAnim newAnim,
                       uint32_t tick) {
    anim = newAnim;
    animStartTick = tick;
}

int CurrentAnimFrame(PlayerAnim anim, uint32_t tick, uint32_t animStartTick) {
    if (tick < animStartTick) {
        return 0;
    }

    const uint32_t elapsed = tick - animStartTick;
    const int frameCount = AnimFrameCount(anim);
    const int ticksPerFrame = AnimTicksPerFrame(anim);
    if (frameCount <= 0 || ticksPerFrame <= 0) {
        return 0;
    }

    const int frame = static_cast<int>(elapsed / static_cast<uint32_t>(ticksPerFrame));
    return frame >= frameCount ? frameCount - 1 : frame;
}

bool IsAnimFinished(PlayerAnim anim, uint32_t tick, uint32_t animStartTick) {
    if (tick < animStartTick) {
        return false;
    }

    const uint32_t elapsed = tick - animStartTick;
    const int frameCount = AnimFrameCount(anim);
    const int ticksPerFrame = AnimTicksPerFrame(anim);
    if (frameCount <= 0 || ticksPerFrame <= 0) {
        return true;
    }

    return elapsed >= static_cast<uint32_t>(frameCount * ticksPerFrame);
}

bool RollCriticalHit(int attackerId, int targetId, uint32_t tick, int chancePercent) {
    if (chancePercent <= 0) {
        return false;
    }
    if (chancePercent >= 100) {
        return true;
    }

    const uint32_t roll = static_cast<uint32_t>(attackerId) * 73856093u ^
                          static_cast<uint32_t>(targetId) * 19349663u ^ tick * 83492791u;
    return static_cast<int>(roll % 100) < chancePercent;
}

bool ApplyDamageToPlayer(PlayerState& player, int damage, uint32_t tick, bool critical) {
    if (!IsAlive(player.state) || damage <= 0) {
        return false;
    }

    if (player.shield > 0) {
        const int shieldDamage = damage < player.shield ? damage : player.shield;
        player.shield -= shieldDamage;
        damage -= shieldDamage;
    }

    if (damage <= 0) {
        return false;
    }

    player.hp -= damage;
    if (player.hp <= 0) {
        player.hp = 0;
        TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                         EntityState::Dead, tick);
        return true;
    }

    if (critical) {
        TransitionEntity(player.state, player.stateStartTick, player.anim, player.animStartTick,
                         EntityState::Hit, tick);
        return true;
    }

    return false;
}

bool ApplyDamageToEnemy(EnemyState& enemy, int damage, uint32_t tick, bool critical) {
    if (!IsAlive(enemy.state) || damage <= 0) {
        return false;
    }

    enemy.hp -= damage;
    if (enemy.hp <= 0) {
        enemy.hp = 0;
        TransitionEntity(enemy.state, enemy.stateStartTick, enemy.anim, enemy.animStartTick,
                         EntityState::Dead, tick);
        return true;
    }

    if (critical) {
        TransitionEntity(enemy.state, enemy.stateStartTick, enemy.anim, enemy.animStartTick,
                         EntityState::Hit, tick);
        return true;
    }

    return false;
}

}  // namespace net
