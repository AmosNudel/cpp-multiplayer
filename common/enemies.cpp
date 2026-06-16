#include "common/enemies.hpp"

#include "common/entity_defs.hpp"
#include "common/entity_registry.hpp"
#include "common/grid.hpp"

namespace net {

EnemyState CreateDefaultGoblin(int id) {
    EnemyState goblin;
    goblin.id = id;
    goblin.kind = kGoblinEntityId;
    goblin.x = CellCenterX(kDefaultGoblinCol);
    goblin.y = CellCenterY(kDefaultGoblinRow);
    goblin.state = EntityState::Idle;
    goblin.anim = PlayerAnim::Idle;
    goblin.hp = DefaultEntityRegistry().StatsFor(kGoblinEntityId).maxHp;
    goblin.facingRight = false;
    return goblin;
}

std::vector<EnemyState> CreateDefaultEnemies() {
    return {CreateDefaultGoblin(kDefaultGoblinId)};
}

}  // namespace net
