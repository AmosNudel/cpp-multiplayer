#pragma once

#include <cstdint>
#include <string>

#include "common/config.hpp"

namespace net {

enum class EntityState {
    Idle,
    Moving,
    Combat,
    Hit,
    Dead,
};

inline constexpr int kPlayerMaxHp = 100;
inline constexpr int kPlayerMaxShield = 50;
inline constexpr int kGoblinMaxHp = 150;
inline constexpr int kPlayerAttackDamage = 10;
inline constexpr int kPlayerCritChancePercent = 25;
inline constexpr int kPlayerCritDamageMultiplier = 2;
inline constexpr int kGoblinAttackDamage = 8;
inline constexpr int kHitStunTicks = 8;
inline constexpr int kGoblinAttackCooldownTicks = 12;
inline constexpr float kEntityPickRadius = kPlayerRadius * 1.4f;

inline const char* EntityStateName(EntityState state) {
    switch (state) {
        case EntityState::Idle: return "idle";
        case EntityState::Moving: return "moving";
        case EntityState::Combat: return "combat";
        case EntityState::Hit: return "hit";
        case EntityState::Dead: return "dead";
    }
    return "idle";
}

inline EntityState ParseEntityState(const std::string& value) {
    if (value == "moving") return EntityState::Moving;
    if (value == "combat") return EntityState::Combat;
    if (value == "hit") return EntityState::Hit;
    if (value == "dead") return EntityState::Dead;
    return EntityState::Idle;
}

inline PlayerAnim AnimForEntityState(EntityState state) {
    switch (state) {
        case EntityState::Moving: return PlayerAnim::Run;
        case EntityState::Hit: return PlayerAnim::Hit;
        case EntityState::Dead: return PlayerAnim::Dead;
        case EntityState::Combat: return PlayerAnim::Attack1;
        case EntityState::Idle:
        default: return PlayerAnim::Idle;
    }
}

inline int PlayerAttackDamage(bool critical) {
    return critical ? kPlayerAttackDamage * kPlayerCritDamageMultiplier : kPlayerAttackDamage;
}

inline bool IsAlive(EntityState state) { return state != EntityState::Dead; }

inline bool CanAcceptMoveIntent(EntityState state) {
    return state != EntityState::Dead && state != EntityState::Hit;
}

inline bool CanAcceptAttackIntent(EntityState state) {
    return state != EntityState::Dead && state != EntityState::Hit;
}

}  // namespace net
