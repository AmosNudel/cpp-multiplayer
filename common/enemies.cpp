#include "common/enemies.hpp"

#include "common/grid.hpp"

namespace net {

EnemyState CreateDefaultGoblin(int id) {
    EnemyState goblin;
    goblin.id = id;
    goblin.kind = "goblin";
    goblin.x = CellCenterX(kDefaultGoblinCol);
    goblin.y = CellCenterY(kDefaultGoblinRow);
    goblin.state = EntityState::Idle;
    goblin.anim = PlayerAnim::Idle;
    goblin.hp = kGoblinMaxHp;
    goblin.facingRight = false;
    return goblin;
}

std::vector<EnemyState> CreateDefaultEnemies() {
    return {CreateDefaultGoblin(kDefaultGoblinId)};
}

}  // namespace net
