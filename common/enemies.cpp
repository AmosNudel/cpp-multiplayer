#include "common/enemies.hpp"

#include "common/grid.hpp"

namespace net {

std::vector<EnemyState> CreateDefaultEnemies() {
    EnemyState goblin;
    goblin.id = 1;
    goblin.kind = "goblin";
    goblin.x = CellCenterX(kDefaultGoblinCol);
    goblin.y = CellCenterY(kDefaultGoblinRow);
    goblin.state = EntityState::Idle;
    goblin.anim = PlayerAnim::Idle;
    goblin.hp = kGoblinMaxHp;
    goblin.facingRight = false;
    return {goblin};
}

}  // namespace net
