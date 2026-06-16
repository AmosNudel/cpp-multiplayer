#pragma once

#include "common/config.hpp"

namespace net {

inline constexpr float kGridCellSize = 40.0f;
inline constexpr int kGridCols = static_cast<int>(kWorldWidth / kGridCellSize);
inline constexpr int kGridRows = static_cast<int>(kWorldHeight / kGridCellSize);

inline bool IsValidCell(int col, int row) {
    return col >= 0 && col < kGridCols && row >= 0 && row < kGridRows;
}

inline int WorldToCellCol(float worldX) {
    if (worldX < 0.0f) {
        return 0;
    }

    const int col = static_cast<int>(worldX / kGridCellSize);
    return col >= kGridCols ? kGridCols - 1 : col;
}

inline int WorldToCellRow(float worldY) {
    if (worldY < 0.0f) {
        return 0;
    }

    const int row = static_cast<int>(worldY / kGridCellSize);
    return row >= kGridRows ? kGridRows - 1 : row;
}

inline float CellCenterX(int col) {
    return (static_cast<float>(col) + 0.5f) * kGridCellSize;
}

inline float CellCenterY(int row) {
    return (static_cast<float>(row) + 0.5f) * kGridCellSize;
}

}  // namespace net
