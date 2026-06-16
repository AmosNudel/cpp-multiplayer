#include "common/pathfinding.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <vector>

namespace net {
namespace {

int CellIndex(int col, int row) {
    return row * kGridCols + col;
}

int ManhattanDistance(int colA, int rowA, int colB, int rowB) {
    return std::abs(colA - colB) + std::abs(rowA - rowB);
}

struct Node {
    int col = 0;
    int row = 0;
    int g = 0;
    int f = 0;

    bool operator>(const Node& other) const { return f > other.f; }
};

std::vector<GridPoint> ReconstructPath(const std::vector<int>& parent, int goalIndex) {
    std::vector<GridPoint> path;
    int current = goalIndex;
    while (current >= 0) {
        path.emplace_back(current % kGridCols, current / kGridCols);
        current = parent[static_cast<size_t>(current)];
    }
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<GridPoint> SimplifyPathToCorners(const std::vector<GridPoint>& path) {
    if (path.size() <= 2) {
        return path;
    }

    std::vector<GridPoint> simplified;
    simplified.push_back(path.front());

    for (size_t i = 2; i < path.size(); ++i) {
        const GridPoint& beforePrev = path[i - 2];
        const GridPoint& prev = path[i - 1];
        const GridPoint& curr = path[i];

        const int prevDx = prev.first - beforePrev.first;
        const int prevDy = prev.second - beforePrev.second;
        const int currDx = curr.first - prev.first;
        const int currDy = curr.second - prev.second;

        if (prevDx != currDx || prevDy != currDy) {
            simplified.push_back(prev);
        }
    }

    simplified.push_back(path.back());
    return simplified;
}

}  // namespace

std::vector<GridPoint> FindPath(const GridMap& map, int startCol, int startRow, int goalCol,
                                int goalRow) {
    if (!IsValidCell(startCol, startRow) || !IsValidCell(goalCol, goalRow)) {
        return {};
    }
    if (!map.IsWalkable(goalCol, goalRow)) {
        return {};
    }
    if (startCol == goalCol && startRow == goalRow) {
        return {{goalCol, goalRow}};
    }

    const int cellCount = kGridCols * kGridRows;
    std::vector<int> gScore(static_cast<size_t>(cellCount), std::numeric_limits<int>::max());
    std::vector<int> parent(static_cast<size_t>(cellCount), -1);
    std::vector<bool> closed(static_cast<size_t>(cellCount), false);

    const int startIndex = CellIndex(startCol, startRow);
    const int goalIndex = CellIndex(goalCol, goalRow);

    gScore[static_cast<size_t>(startIndex)] = 0;

    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    open.push(Node{startCol, startRow, 0,
                   ManhattanDistance(startCol, startRow, goalCol, goalRow)});

    static constexpr int kDirections[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1},
    };

    while (!open.empty()) {
        const Node current = open.top();
        open.pop();

        const int currentIndex = CellIndex(current.col, current.row);
        if (closed[static_cast<size_t>(currentIndex)]) {
            continue;
        }
        closed[static_cast<size_t>(currentIndex)] = true;

        if (currentIndex == goalIndex) {
            return SimplifyPathToCorners(ReconstructPath(parent, goalIndex));
        }

        for (const auto& direction : kDirections) {
            const int nextCol = current.col + direction[0];
            const int nextRow = current.row + direction[1];
            if (!IsValidCell(nextCol, nextRow) || !map.IsWalkable(nextCol, nextRow)) {
                continue;
            }

            const int nextIndex = CellIndex(nextCol, nextRow);
            if (closed[static_cast<size_t>(nextIndex)]) {
                continue;
            }

            const int tentativeG = current.g + 1;
            if (tentativeG >= gScore[static_cast<size_t>(nextIndex)]) {
                continue;
            }

            parent[static_cast<size_t>(nextIndex)] = currentIndex;
            gScore[static_cast<size_t>(nextIndex)] = tentativeG;
            open.push(Node{nextCol, nextRow, tentativeG,
                           tentativeG + ManhattanDistance(nextCol, nextRow, goalCol, goalRow)});
        }
    }

    return {};
}

}  // namespace net
