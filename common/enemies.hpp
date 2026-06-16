#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "common/config.hpp"
#include "common/entity_state.hpp"
#include "common/grid_map.hpp"

namespace net {

inline constexpr int kGoblinIdleFrameCount = 4;
inline constexpr int kGoblinIdleAnimTicksPerFrame = 3;
inline constexpr int kGoblinAttackFrameCount = 8;
inline constexpr int kGoblinAttackDamageFrame = 6;
inline constexpr int kGoblinAttackAnimTicksPerFrame = 2;
inline constexpr int kGoblinDeathFrameCount = 4;
inline constexpr int kGoblinDeathAnimTicksPerFrame = 3;
inline constexpr int kGoblinCorpseLifetimeTicks = 60;
inline constexpr float kGoblinSpriteHeight = 128.0f;
inline constexpr int kDefaultGoblinCol = 7;
inline constexpr int kDefaultGoblinRow = 7;
inline constexpr int kDefaultGoblinId = 1;
inline constexpr int kDefaultGoblinCount = 5;
inline constexpr int kGoblinMinSpawnDistanceFromPlayer = 4;
inline constexpr int kGoblinMinSpawnSeparation = 3;
inline constexpr int kGoblinPatrolRadius = 2;
inline constexpr int kGoblinAggroCellDistance = 1;
inline constexpr int kGoblinPatrolIdleTicks = 60;

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
    int hp = 0;
    int targetId = -1;
    uint32_t lastAttackTick = 0;
    bool attackDamageDealt = false;
};

std::pair<int, int> ResolvePlayerSpawnCell(const GridMap& map);
std::vector<std::pair<int, int>> CollectGoblinSpawnPoints(
    const GridMap& map, int playerSpawnCol, int playerSpawnRow,
    int minDistance = kGoblinMinSpawnDistanceFromPlayer);
std::pair<int, int> PickRandomGoblinSpawnCell(const GridMap& map,
                                               const std::vector<EnemyState>& enemies,
                                               int excludeEnemyId = -1);
bool EnemiesShareTiles(const std::vector<EnemyState>& enemies);
std::vector<std::pair<int, int>> BuildGoblinPatrolWaypoints(const GridMap& map, int anchorCol,
                                                             int anchorRow);
std::vector<std::pair<int, int>> AllocateSpawnCells(const GridMap& map, int count);
std::vector<EnemyState> CreateDefaultEnemies();
EnemyState CreateGoblinAt(int id, int col, int row);

inline int GoblinAnimFrameCount(PlayerAnim anim) {
    switch (anim) {
        case PlayerAnim::Attack1: return kGoblinAttackFrameCount;
        case PlayerAnim::Run: return kRunFrameCount;
        case PlayerAnim::Dead: return kGoblinDeathFrameCount;
        case PlayerAnim::Idle:
        default: return kGoblinIdleFrameCount;
    }
}

inline int GoblinAnimTicksPerFrame(PlayerAnim anim) {
    switch (anim) {
        case PlayerAnim::Attack1: return kGoblinAttackAnimTicksPerFrame;
        case PlayerAnim::Run: return kRunAnimTicksPerFrame;
        case PlayerAnim::Dead: return kGoblinDeathAnimTicksPerFrame;
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
    if (anim == PlayerAnim::Attack1 || anim == PlayerAnim::Dead) {
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
