#include "common/enemies.hpp"

#include <algorithm>
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

std::pair<int, int> PickRandomGoblinSpawnCell(const GridMap& map,
                                              const std::vector<EnemyState>& enemies,
                                              int excludeEnemyId) {
    const auto [playerCol, playerRow] = ResolvePlayerSpawnCell(map);
    std::vector<std::pair<int, int>> candidates =
        CollectGoblinSpawnPoints(map, playerCol, playerRow);

    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                    [&](const std::pair<int, int>& cell) {
                                        return IsCellOccupiedByEnemy(cell.first, cell.second,
                                                                     enemies, excludeEnemyId);
                                    }),
                     candidates.end());

    if (candidates.empty()) {
        return {kDefaultGoblinCol, kDefaultGoblinRow};
    }

    std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
    return candidates[dist(GoblinSpawnRng())];
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

    const int count =
        std::min(kDefaultGoblinCount, static_cast<int>(spawnPoints.size()));
    std::vector<EnemyState> enemies;
    enemies.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        enemies.push_back(CreateGoblinAt(kDefaultGoblinId + i, spawnPoints[static_cast<size_t>(i)].first,
                                         spawnPoints[static_cast<size_t>(i)].second));
    }
    return enemies;
}

}  // namespace net
