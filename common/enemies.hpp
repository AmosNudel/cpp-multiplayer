#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/config.hpp"
#include "common/entity_state.hpp"

namespace net {

inline constexpr int kGoblinIdleFrameCount = 4;
inline constexpr int kGoblinIdleAnimTicksPerFrame = 3;
inline constexpr int kGoblinAttackFrameCount = 8;
inline constexpr int kGoblinAttackDamageFrame = 6;
inline constexpr int kGoblinAttackAnimTicksPerFrame = 2;
inline constexpr float kGoblinSpriteHeight = 128.0f;
inline constexpr int kDefaultGoblinCol = 7;
inline constexpr int kDefaultGoblinRow = 7;
inline constexpr int kDefaultGoblinId = 1;
inline constexpr int kGoblinPatrolMinCol = 5;
inline constexpr int kGoblinPatrolMaxCol = 9;
inline constexpr int kGoblinPatrolMinRow = 5;
inline constexpr int kGoblinPatrolMaxRow = 9;
inline constexpr int kGoblinAggroCellDistance = 1;

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
    bool attackDamageDealt = false;
};

std::vector<EnemyState> CreateDefaultEnemies();
EnemyState CreateDefaultGoblin(int id = kDefaultGoblinId);

inline int GoblinAnimFrameCount(PlayerAnim anim) {
    switch (anim) {
        case PlayerAnim::Attack1: return kGoblinAttackFrameCount;
        case PlayerAnim::Run: return kRunFrameCount;
        case PlayerAnim::Idle:
        default: return kGoblinIdleFrameCount;
    }
}

inline int GoblinAnimTicksPerFrame(PlayerAnim anim) {
    switch (anim) {
        case PlayerAnim::Attack1: return kGoblinAttackAnimTicksPerFrame;
        case PlayerAnim::Run: return kRunAnimTicksPerFrame;
        case PlayerAnim::Idle:
        default: return kGoblinIdleAnimTicksPerFrame;
    }
}

inline int GoblinAnimFrameIndex(PlayerAnim anim, uint32_t serverTick, uint32_t animStartTick) {
    if (serverTick < animStartTick) {
        return 0;
    }

    const uint32_t elapsed = serverTick - animStartTick;
    const int frameCount = GoblinAnimFrameCount(anim);
    const int ticksPerFrame = GoblinAnimTicksPerFrame(anim);
    if (frameCount <= 0 || ticksPerFrame <= 0) {
        return 0;
    }

    const int frame = static_cast<int>(elapsed / static_cast<uint32_t>(ticksPerFrame));
    if (anim == PlayerAnim::Attack1) {
        return frame >= frameCount ? frameCount - 1 : frame;
    }

    return static_cast<int>(frame % static_cast<uint32_t>(frameCount));
}

inline bool GoblinAnimFinished(PlayerAnim anim, uint32_t tick, uint32_t animStartTick) {
    if (tick < animStartTick) {
        return false;
    }

    const uint32_t elapsed = tick - animStartTick;
    const int frameCount = GoblinAnimFrameCount(anim);
    const int ticksPerFrame = GoblinAnimTicksPerFrame(anim);
    if (frameCount <= 0 || ticksPerFrame <= 0) {
        return true;
    }

    return elapsed >= static_cast<uint32_t>(frameCount * ticksPerFrame);
}

}  // namespace net
