#pragma once

#include <cstdint>
#include <string>

namespace net {

inline constexpr const char* kPlayerEntityId = "player";
inline constexpr const char* kGoblinEntityId = "goblin";
inline constexpr const char* kGoblinBossEntityId = "goblin_boss";

struct CombatStats {
    int maxHp = 100;
    int maxShield = 0;
    int attackDamage = 10;
    int critChancePercent = 0;
    int critDamageMultiplier = 2;
    int attackCooldownTicks = 12;
    int hitStunTicks = 8;
    int shieldRegenPerTick = 0;
};

struct EntityDef {
    std::string id;
    std::string displayName;
    CombatStats stats;
    float spriteHeight = 96.0f;
};

inline int ComputeAttackDamage(const CombatStats& stats, bool critical) {
    return critical ? stats.attackDamage * stats.critDamageMultiplier : stats.attackDamage;
}

}  // namespace net
