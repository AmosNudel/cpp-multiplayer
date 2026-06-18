#pragma once

#include <cstdint>

#include "common/config.hpp"

namespace net {

inline constexpr int kArenaWaveCount = 4;
inline constexpr int kArenaWaveGoblinCounts[kArenaWaveCount] = {3, 3, 4, 4};
inline constexpr int kArenaWaveIntermissionSeconds = 10;
inline constexpr uint32_t kArenaWaveIntermissionTicks =
    static_cast<uint32_t>(kArenaWaveIntermissionSeconds * kTickRate);

inline constexpr int kBossScalePlayerThreshold = 3;
inline constexpr int kBossBonusStatPercentPerExtraPlayer = 25;

inline int ArenaWaveGoblinCount(int waveIndex) {
    if (waveIndex < 1 || waveIndex > kArenaWaveCount) {
        return 0;
    }
    return kArenaWaveGoblinCounts[waveIndex - 1];
}

inline int BossExtraPlayerCount(int arenaPlayerCount) {
    return arenaPlayerCount > kBossScalePlayerThreshold
               ? arenaPlayerCount - kBossScalePlayerThreshold
               : 0;
}

inline int ScaleStatByExtraPlayers(int baseStat, int arenaPlayerCount) {
    const int extra = BossExtraPlayerCount(arenaPlayerCount);
    return baseStat + (baseStat * extra * kBossBonusStatPercentPerExtraPlayer) / 100;
}

}  // namespace net
