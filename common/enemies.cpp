#include "common/enemies.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <utility>

#include "common/combat.hpp"
#include "common/entity_defs.hpp"
#include "common/entity_registry.hpp"
#include "common/grid.hpp"
#include "common/pathfinding.hpp"

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
        for (const std::pair<int, int>& cell : EnemyOccupiedCells(enemy)) {
            if (cell.first == col && cell.second == row) {
                return true;
            }
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
        const std::vector<std::pair<int, int>> cellsA = EnemyOccupiedCells(enemies[i]);
        for (size_t j = i + 1; j < enemies.size(); ++j) {
            if (!IsAlive(enemies[j].state)) {
                continue;
            }
            const std::vector<std::pair<int, int>> cellsB = EnemyOccupiedCells(enemies[j]);
            for (const std::pair<int, int>& cellA : cellsA) {
                for (const std::pair<int, int>& cellB : cellsB) {
                    if (cellA.first == cellB.first && cellA.second == cellB.second) {
                        return true;
                    }
                }
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

std::vector<std::pair<int, int>> AllocateSpawnCells(const GridMap& map, int count) {
    const auto [centerCol, centerRow] = ResolvePlayerSpawnCell(map);
    std::vector<std::pair<int, int>> result;
    if (count <= 0) {
        return result;
    }

    std::vector<std::pair<int, int>> queue = {{centerCol, centerRow}};
    std::vector<std::vector<bool>> visited(static_cast<size_t>(kGridRows),
                                           std::vector<bool>(static_cast<size_t>(kGridCols), false));
    visited[static_cast<size_t>(centerRow)][static_cast<size_t>(centerCol)] = true;

    size_t queueIndex = 0;
    while (queueIndex < queue.size() && static_cast<int>(result.size()) < count) {
        const auto [col, row] = queue[queueIndex++];
        if (map.IsWalkable(col, row)) {
            result.emplace_back(col, row);
        }

        constexpr int kDirections[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& direction : kDirections) {
            const int nextCol = col + direction[0];
            const int nextRow = row + direction[1];
            if (!IsValidCell(nextCol, nextRow) ||
                visited[static_cast<size_t>(nextRow)][static_cast<size_t>(nextCol)]) {
                continue;
            }
            visited[static_cast<size_t>(nextRow)][static_cast<size_t>(nextCol)] = true;
            queue.emplace_back(nextCol, nextRow);
        }
    }

    while (static_cast<int>(result.size()) < count) {
        result.emplace_back(centerCol, centerRow);
    }

    return result;
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
    const GridMap& map = ArenaGridMap();
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
            CreateGoblinAt(kDefaultGoblinId + static_cast<int>(enemies.size()), cell.first,
                           cell.second));
    }

    if (static_cast<int>(enemies.size()) < kDefaultGoblinCount) {
        for (const std::pair<int, int>& cell : spawnPoints) {
            if (static_cast<int>(enemies.size()) >= kDefaultGoblinCount) {
                break;
            }
            if (IsCellOccupiedByEnemy(cell.first, cell.second, enemies, -1)) {
                continue;
            }
            if (ManhattanCellDistance(cell.first, cell.second, playerCol, playerRow) <
                kGoblinMinSpawnDistanceFromPlayer) {
                continue;
            }
            enemies.push_back(
                CreateGoblinAt(kDefaultGoblinId + static_cast<int>(enemies.size()), cell.first,
                               cell.second));
        }
    }

    return enemies;
}

std::pair<int, int> GoblinBossTopLeftFromCenter(int centerCol, int centerRow) {
    return {centerCol - kGoblinBossTileCols / 2, centerRow - kGoblinBossTileRows / 2};
}

float GoblinBossCenterXFromTopLeft(int topLeftCol) {
    return CellCenterX(topLeftCol) +
           (static_cast<float>(kGoblinBossTileCols) - 1.0f) * kGridCellSize * 0.5f;
}

float GoblinBossCenterYFromTopLeft(int topLeftRow) {
    return CellCenterY(topLeftRow) +
           (static_cast<float>(kGoblinBossTileRows) - 1.0f) * kGridCellSize * 0.5f;
}

std::pair<float, float> GoblinBossWorldCenterFromCenterCell(int centerCol, int centerRow) {
    const auto [topLeftCol, topLeftRow] = GoblinBossTopLeftFromCenter(centerCol, centerRow);
    return {GoblinBossCenterXFromTopLeft(topLeftCol), GoblinBossCenterYFromTopLeft(topLeftRow)};
}

bool GoblinBossFitsAtTopLeft(int topLeftCol, int topLeftRow, const GridMap& map,
                             const std::vector<PlayerState>& players,
                             const std::vector<EnemyState>& enemies, int ignoreEnemyId) {
    if (topLeftCol < 0 || topLeftRow < 0 ||
        topLeftCol + kGoblinBossTileCols > kGridCols ||
        topLeftRow + kGoblinBossTileRows > kGridRows) {
        return false;
    }

    for (int row = topLeftRow; row < topLeftRow + kGoblinBossTileRows; ++row) {
        for (int col = topLeftCol; col < topLeftCol + kGoblinBossTileCols; ++col) {
            if (!map.IsWalkable(col, row)) {
                return false;
            }
            for (const PlayerState& player : players) {
                if (!IsAlive(player.state)) {
                    continue;
                }
                if (WorldToCellCol(player.x) == col && WorldToCellRow(player.y) == row) {
                    return false;
                }
            }
            for (const EnemyState& enemy : enemies) {
                if (enemy.id == ignoreEnemyId || !IsAlive(enemy.state)) {
                    continue;
                }
                if (IsGoblinBoss(enemy)) {
                    const auto [otherTopLeftCol, otherTopLeftRow] =
                        GoblinBossTopLeftFromCenter(WorldToCellCol(enemy.x),
                                                    WorldToCellRow(enemy.y));
                    if (col >= otherTopLeftCol && col < otherTopLeftCol + kGoblinBossTileCols &&
                        row >= otherTopLeftRow && row < otherTopLeftRow + kGoblinBossTileRows) {
                        return false;
                    }
                } else if (WorldToCellCol(enemy.x) == col && WorldToCellRow(enemy.y) == row) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool GoblinBossFitsAtCenter(int centerCol, int centerRow, const GridMap& map,
                            const std::vector<PlayerState>& players,
                            const std::vector<EnemyState>& enemies, int ignoreEnemyId) {
    const auto [topLeftCol, topLeftRow] = GoblinBossTopLeftFromCenter(centerCol, centerRow);
    return GoblinBossFitsAtTopLeft(topLeftCol, topLeftRow, map, players, enemies, ignoreEnemyId);
}

std::optional<GridPoint> FindBestBossChaseCenter(const GridMap& map, const EnemyState& boss,
                                                 const PlayerState& player,
                                                 const std::vector<PlayerState>& players,
                                                 const std::vector<EnemyState>& enemies) {
    const int startCol = WorldToCellCol(boss.x);
    const int startRow = WorldToCellRow(boss.y);
    const int playerCol = WorldToCellCol(player.x);
    const int playerRow = WorldToCellRow(player.y);

    static constexpr int kDirections[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    std::optional<GridPoint> best;
    int bestPathLength = std::numeric_limits<int>::max();

    for (const auto& direction : kDirections) {
        const int touchCol = playerCol + direction[0];
        const int touchRow = playerRow + direction[1];
        if (!IsValidCell(touchCol, touchRow)) {
            continue;
        }

        for (int localCol = 0; localCol < kGoblinBossTileCols; ++localCol) {
            for (int localRow = 0; localRow < kGoblinBossTileRows; ++localRow) {
                const int topLeftCol = touchCol - localCol;
                const int topLeftRow = touchRow - localRow;
                if (!GoblinBossFitsAtTopLeft(topLeftCol, topLeftRow, map, players, enemies,
                                            boss.id)) {
                    continue;
                }

                const int centerCol = topLeftCol + kGoblinBossTileCols / 2;
                const int centerRow = topLeftRow + kGoblinBossTileRows / 2;
                const std::vector<GridPoint> path =
                    FindPath(map, startCol, startRow, centerCol, centerRow);
                if (path.empty()) {
                    continue;
                }

                const int pathLength = static_cast<int>(path.size());
                if (pathLength < bestPathLength) {
                    bestPathLength = pathLength;
                    best = GridPoint{centerCol, centerRow};
                }
            }
        }
    }

    return best;
}

bool IsGoblinBoss(const EnemyState& enemy) {
    return enemy.kind == kGoblinBossEntityId;
}

bool IsRegularGoblin(const EnemyState& enemy) {
    return enemy.kind == kGoblinEntityId;
}

std::vector<std::pair<int, int>> EnemyOccupiedCells(const EnemyState& enemy) {
    std::vector<std::pair<int, int>> cells;
    if (IsGoblinBoss(enemy)) {
        const auto [topLeftCol, topLeftRow] =
            GoblinBossTopLeftFromCenter(WorldToCellCol(enemy.x), WorldToCellRow(enemy.y));
        for (int row = topLeftRow; row < topLeftRow + kGoblinBossTileRows; ++row) {
            for (int col = topLeftCol; col < topLeftCol + kGoblinBossTileCols; ++col) {
                cells.emplace_back(col, row);
            }
        }
        return cells;
    }

    const int anchorCol = WorldToCellCol(enemy.x);
    const int anchorRow = WorldToCellRow(enemy.y);
    cells.emplace_back(anchorCol, anchorRow);
    return cells;
}

bool AllRegularGoblinsDefeated(const std::vector<EnemyState>& enemies) {
    bool foundRegularGoblin = false;
    for (const EnemyState& enemy : enemies) {
        if (!IsRegularGoblin(enemy)) {
            continue;
        }
        foundRegularGoblin = true;
        if (enemy.state != EntityState::Dead) {
            return false;
        }
    }
    return foundRegularGoblin;
}

bool HasGoblinBoss(const std::vector<EnemyState>& enemies) {
    for (const EnemyState& enemy : enemies) {
        if (IsGoblinBoss(enemy)) {
            return true;
        }
    }
    return false;
}

bool IsValidGoblinBossSpawnCell(int topLeftCol, int topLeftRow, const GridMap& map,
                                const std::vector<EnemyState>& enemies) {
    return GoblinBossFitsAtTopLeft(topLeftCol, topLeftRow, map, {}, enemies, -1);
}

std::pair<int, int> PickGoblinBossSpawnCell(const GridMap& map,
                                            const std::vector<EnemyState>& enemies) {
    const auto [centerCol, centerRow] = ResolvePlayerSpawnCell(map);
    std::vector<std::pair<int, int>> candidates;
    for (int row = 1; row <= kGridRows - 1 - kGoblinBossTileRows; ++row) {
        for (int col = 1; col <= kGridCols - 1 - kGoblinBossTileCols; ++col) {
            if (IsValidGoblinBossSpawnCell(col, row, map, enemies)) {
                candidates.emplace_back(col, row);
            }
        }
    }

    if (candidates.empty()) {
        return {centerCol, centerRow};
    }

    std::sort(candidates.begin(), candidates.end(),
              [centerCol, centerRow](const std::pair<int, int>& a,
                                     const std::pair<int, int>& b) {
                  const int distA = ManhattanCellDistance(a.first, a.second, centerCol, centerRow);
                  const int distB = ManhattanCellDistance(b.first, b.second, centerCol, centerRow);
                  return distA < distB;
              });
    return candidates.front();
}

EnemyState CreateGoblinBossAt(int id, int topLeftCol, int topLeftRow) {
    EnemyState boss;
    boss.id = id;
    boss.kind = kGoblinBossEntityId;
    boss.x = GoblinBossCenterXFromTopLeft(topLeftCol);
    boss.y = GoblinBossCenterYFromTopLeft(topLeftRow);
    boss.state = EntityState::Idle;
    boss.anim = PlayerAnim::Idle;
    boss.hp = DefaultEntityRegistry().StatsFor(kGoblinBossEntityId).maxHp;
    boss.facingRight = false;
    return boss;
}

}  // namespace net
