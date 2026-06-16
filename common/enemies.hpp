#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/config.hpp"
#include "common/entity_state.hpp"

namespace net {

inline constexpr int kGoblinIdleFrameCount = 4;
inline constexpr int kGoblinIdleAnimTicksPerFrame = 3;
inline constexpr float kGoblinSpriteHeight = 128.0f;
inline constexpr int kDefaultGoblinCol = 7;
inline constexpr int kDefaultGoblinRow = 7;

struct EnemyState {
    int id = 0;
    std::string kind = "goblin";
    float x = 0.0f;
    float y = 0.0f;
    EntityState state = EntityState::Idle;
    uint32_t stateStartTick = 0;
    PlayerAnim anim = PlayerAnim::Idle;
    uint32_t animStartTick = 0;
    bool facingRight = true;
    int hp = kGoblinMaxHp;
    int targetId = -1;
    uint32_t lastAttackTick = 0;
};

std::vector<EnemyState> CreateDefaultEnemies();

inline int GoblinAnimFrameIndex(PlayerAnim anim, uint32_t serverTick, uint32_t animStartTick) {
    return AnimFrameIndex(anim, serverTick, animStartTick);
}

}  // namespace net
