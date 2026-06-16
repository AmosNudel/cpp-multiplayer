#include "common/grid_map.hpp"

namespace net {

int GridMap::Index(int col, int row) {
    return row * kGridCols + col;
}

GridMap GridMap::CreateDefault() {
    GridMap map;

    for (int col = 0; col < kGridCols; ++col) {
        map.Set(col, 0, TileType::Wall);
        map.Set(col, kGridRows - 1, TileType::Wall);
    }
    for (int row = 0; row < kGridRows; ++row) {
        map.Set(0, row, TileType::Wall);
        map.Set(kGridCols - 1, row, TileType::Wall);
    }

    map.FillRect(3, 3, 3, 2, TileType::Prop);
    map.FillRect(8, 2, 1, 4, TileType::Prop);
    map.Set(14, 4, TileType::Prop);
    map.Set(14, 5, TileType::Prop);
    map.Set(15, 5, TileType::Prop);
    map.Set(16, 5, TileType::Prop);
    map.FillRect(5, 10, 2, 2, TileType::Prop);
    map.Set(12, 11, TileType::Prop);
    map.FillRect(2, 12, 5, 1, TileType::Prop);
    map.FillRect(15, 9, 2, 3, TileType::Prop);

    return map;
}

TileType GridMap::Get(int col, int row) const {
    if (!IsValidCell(col, row)) {
        return TileType::Wall;
    }
    return tiles_[Index(col, row)];
}

bool GridMap::IsWalkable(int col, int row) const {
    return Get(col, row) == TileType::Empty;
}

void GridMap::Set(int col, int row, TileType type) {
    if (!IsValidCell(col, row)) {
        return;
    }
    tiles_[Index(col, row)] = type;
}

void GridMap::FillRect(int col, int row, int width, int height, TileType type) {
    for (int y = row; y < row + height; ++y) {
        for (int x = col; x < col + width; ++x) {
            Set(x, y, type);
        }
    }
}

const GridMap& DefaultGridMap() {
    static const GridMap map = GridMap::CreateDefault();
    return map;
}

}  // namespace net
