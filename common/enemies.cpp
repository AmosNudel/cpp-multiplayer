#include "common/enemies.hpp"

#include "common/grid.hpp"

namespace net {

std::vector<EnemyState> CreateDefaultEnemies() {
    EnemyState goblin;
    goblin.id = 1;
    goblin.kind = "goblin";
    goblin.x = CellCenterX(7);
    goblin.y = CellCenterY(7);
    goblin.anim = PlayerAnim::Idle;
    goblin.facingRight = false;
    return {goblin};
}

}  // namespace net
