#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/config.hpp"

namespace net {

inline constexpr int kGoblinIdleFrameCount = 4;
inline constexpr int kGoblinIdleAnimTicksPerFrame = 3;
inline constexpr float kGoblinSpriteHeight = 96.0f;

struct EnemyState {
    int id = 0;
    std::string kind = "goblin";
    float x = 0.0f;
    float y = 0.0f;
    PlayerAnim anim = PlayerAnim::Idle;
    uint32_t animStartTick = 0;
    bool facingRight = true;
};

std::vector<EnemyState> CreateDefaultEnemies();

inline int GoblinAnimFrameIndex(PlayerAnim anim, uint32_t serverTick, uint32_t animStartTick) {
    (void)anim;
    if (serverTick < animStartTick) {
        return 0;
    }

    const uint32_t elapsed = serverTick - animStartTick;
    return static_cast<int>((elapsed / static_cast<uint32_t>(kGoblinIdleAnimTicksPerFrame)) %
                              static_cast<uint32_t>(kGoblinIdleFrameCount));
}

}  // namespace net
