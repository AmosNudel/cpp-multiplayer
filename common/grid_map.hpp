#pragma once

#include <array>
#include <cstdint>

#include "common/grid.hpp"

namespace net {

enum class TileType : std::uint8_t {
    Empty = 0,
    Wall = 1,
    Prop = 2,
    Enemy = 3,
};

class GridMap {
public:
    static GridMap CreateHub();
    static GridMap CreateArena();
    static GridMap CreateDefault();

    TileType Get(int col, int row) const;
    bool IsWalkable(int col, int row) const;
    void Set(int col, int row, TileType type);
    void FillRect(int col, int row, int width, int height, TileType type);

private:
    static int Index(int col, int row);

    std::array<TileType, kGridCols * kGridRows> tiles_{};
};

const GridMap& DefaultGridMap();
const GridMap& HubGridMap();
const GridMap& ArenaGridMap();

}  // namespace net
