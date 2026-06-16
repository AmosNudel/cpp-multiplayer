#include "common/enemies.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <utility>

#include "common/combat.hpp"
#include "common/entity_defs.hpp"
#include "common/entity_registry.hpp"
#include "common/grid.hpp"

namespace net {
namespace {

std::mt19937& GoblinSpawnRng() {
    static std::mt19937 rng(std::random_device{}());
    return rng;
}

bool IsCellOccupiedByEnemy(int col, int row, const std::vector<EnemyState>& enemies,
                           int excludeEnemyId) {
    for (const EnemyState& enemy : enemies) {
        if (enemy.id == excludeEnemyId || !IsAlive(enemy.state)) {
            continue;
        }
        if (WorldToCellCol(enemy.x) == col && WorldToCellRow(enemy.y) == row) {
            return true;
        }
    }
    return false;
}

bool IsFarEnoughFromEnemies(int col, int row, const std::vector<EnemyState>& enemies,
                            int minSeparation, int excludeEnemyId = -1) {
    for (const EnemyState& enemy : enemies) {
        if (enemy.id == excludeEnemyId || !IsAlive(enemy.state)) {
            continue;
        }
        if (ManhattanCellDistance(col, row, WorldToCellCol(enemy.x), WorldToCellRow(enemy.y)) <
            minSeparation) {
            return false;
        }
    }
    return true;
}

bool IsValidGoblinSpawnCell(int col, int row, const GridMap& map, int playerSpawnCol,
                            int playerSpawnRow, const std::vector<EnemyState>& enemies,
                            int excludeEnemyId) {
    if (!map.IsWalkable(col, row)) {
        return false;
    }
    if (ManhattanCellDistance(col, row, playerSpawnCol, playerSpawnRow) <
        kGoblinMinSpawnDistanceFromPlayer) {
        return false;
    }
    if (IsCellOccupiedByEnemy(col, row, enemies, excludeEnemyId)) {
        return false;
    }
    if (!IsFarEnoughFromEnemies(col, row, enemies, kGoblinMinSpawnSeparation, excludeEnemyId)) {
        return false;
    }
    return true;
}

}  // namespace

std::pair<int, int> ResolvePlayerSpawnCell(const GridMap& map) {
    int spawnCol = kGridCols / 2;
    int spawnRow = kGridRows / 2;
    if (map.IsWalkable(spawnCol, spawnRow)) {
        return {spawnCol, spawnRow};
    }

    for (int row = 1; row < kGridRows - 1; ++row) {
        for (int col = 1; col < kGridCols - 1; ++col) {
            if (map.IsWalkable(col, row)) {
                return {col, row};
            }
        }
    }

    return {spawnCol, spawnRow};
}

std::vector<std::pair<int, int>> CollectGoblinSpawnPoints(const GridMap& map, int playerSpawnCol,
                                                          int playerSpawnRow, int minDistance) {
    std::vector<std::pair<int, int>> points;
    for (int row = 1; row < kGridRows - 1; ++row) {
        for (int col = 1; col < kGridCols - 1; ++col) {
            if (!map.IsWalkable(col, row)) {
                continue;
            }
            if (ManhattanCellDistance(col, row, playerSpawnCol, playerSpawnRow) < minDistance) {
                continue;
            }
            points.emplace_back(col, row);
        }
    }
    return points;
}

bool EnemiesShareTiles(const std::vector<EnemyState>& enemies) {
    for (size_t i = 0; i < enemies.size(); ++i) {
        if (!IsAlive(enemies[i].state)) {
            continue;
        }
        const int colA = WorldToCellCol(enemies[i].x);
        const int rowA = WorldToCellRow(enemies[i].y);
        for (size_t j = i + 1; j < enemies.size(); ++j) {
            if (!IsAlive(enemies[j].state)) {
                continue;
            }
            if (colA == WorldToCellCol(enemies[j].x) && rowA == WorldToCellRow(enemies[j].y)) {
                return true;
            }
        }
    }
    return false;
}

std::pair<int, int> PickRandomGoblinSpawnCell(const GridMap& map,
                                              const std::vector<EnemyState>& enemies,
                                              int excludeEnemyId) {
    const auto [playerCol, playerRow] = ResolvePlayerSpawnCell(map);
    std::vector<std::pair<int, int>> candidates =
        CollectGoblinSpawnPoints(map, playerCol, playerRow);

    std::shuffle(candidates.begin(), candidates.end(), GoblinSpawnRng());
    for (const std::pair<int, int>& cell : candidates) {
        if (IsValidGoblinSpawnCell(cell.first, cell.second, map, playerCol, playerRow, enemies,
                                   excludeEnemyId)) {
            return cell;
        }
    }

    for (const std::pair<int, int>& cell : candidates) {
        if (IsCellOccupiedByEnemy(cell.first, cell.second, enemies, excludeEnemyId)) {
            continue;
        }
        if (ManhattanCellDistance(cell.first, cell.second, playerCol, playerRow) <
            kGoblinMinSpawnDistanceFromPlayer) {
            continue;
        }
        return cell;
    }

    return {kDefaultGoblinCol, kDefaultGoblinRow};
}

std::vector<std::pair<int, int>> BuildGoblinPatrolWaypoints(const GridMap& map, int anchorCol,
                                                            int anchorRow) {
    const int minCol = std::max(1, anchorCol - kGoblinPatrolRadius);
    const int maxCol = std::min(kGridCols - 2, anchorCol + kGoblinPatrolRadius);
    const int minRow = std::max(1, anchorRow - kGoblinPatrolRadius);
    const int maxRow = std::min(kGridRows - 2, anchorRow + kGoblinPatrolRadius);

    std::vector<std::pair<int, int>> corners = {
        {minCol, minRow},
        {maxCol, minRow},
        {maxCol, maxRow},
        {minCol, maxRow},
    };

    std::vector<std::pair<int, int>> waypoints;
    for (const std::pair<int, int>& corner : corners) {
        if (map.IsWalkable(corner.first, corner.second)) {
            waypoints.push_back(corner);
        }
    }

    if (waypoints.size() >= 2) {
        return waypoints;
    }

    waypoints.clear();
    for (int row = minRow; row <= maxRow; ++row) {
        for (int col = minCol; col <= maxCol; ++col) {
            if (!map.IsWalkable(col, row)) {
                continue;
            }
            if (col == anchorCol && row == anchorRow) {
                continue;
            }
            waypoints.emplace_back(col, row);
        }
    }

    std::sort(waypoints.begin(), waypoints.end(),
              [anchorCol, anchorRow](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                  const float angleA = std::atan2(static_cast<float>(a.second - anchorRow),
                                                  static_cast<float>(a.first - anchorCol));
                  const float angleB = std::atan2(static_cast<float>(b.second - anchorRow),
                                                  static_cast<float>(b.first - anchorCol));
                  return angleA < angleB;
              });

    return waypoints;
}

EnemyState CreateGoblinAt(int id, int col, int row) {
    EnemyState goblin;
    goblin.id = id;
    goblin.kind = kGoblinEntityId;
    goblin.x = CellCenterX(col);
    goblin.y = CellCenterY(row);
    goblin.state = EntityState::Idle;
    goblin.anim = PlayerAnim::Idle;
    goblin.hp = DefaultEntityRegistry().StatsFor(kGoblinEntityId).maxHp;
    goblin.facingRight = false;
    return goblin;
}

std::vector<EnemyState> CreateDefaultEnemies() {
    const GridMap& map = DefaultGridMap();
    const auto [playerCol, playerRow] = ResolvePlayerSpawnCell(map);
    std::vector<std::pair<int, int>> spawnPoints =
        CollectGoblinSpawnPoints(map, playerCol, playerRow);

    std::shuffle(spawnPoints.begin(), spawnPoints.end(), GoblinSpawnRng());

    std::vector<EnemyState> enemies;
    enemies.reserve(static_cast<size_t>(kDefaultGoblinCount));
    for (const std::pair<int, int>& cell : spawnPoints) {
        if (static_cast<int>(enemies.size()) >= kDefaultGoblinCount) {
            break;
        }
        if (!IsValidGoblinSpawnCell(cell.first, cell.second, map, playerCol, playerRow, enemies,
                                    -1)) {
            continue;
        }
        enemies.push_back(
            CreateGoblinAt(kDefaultGoblinId + static_cast<int>(enemies.size()), cell.first, cell.second));
    }
    return enemies;
}

}  // namespace net
