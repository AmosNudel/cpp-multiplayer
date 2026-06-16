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
        if (pathLength < bestPathLength) {
            bestPathLength = pathLength;
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

void SetEntityAnim(PlayerAnim& anim, uint32_t& animStartTick, PlayerAnim newAnim,
                   uint32_t tick) {
    if (anim == newAnim) {
        return;
    }
    anim = newAnim;
    animStartTick = tick;
}

}  // namespace net
