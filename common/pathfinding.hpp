#pragma once

#include <utility>
#include <vector>

#include "common/grid_map.hpp"

namespace net {

using GridPoint = std::pair<int, int>;

std::vector<GridPoint> FindPath(const GridMap& map, int startCol, int startRow, int goalCol,
                                int goalRow);

}  // namespace net
