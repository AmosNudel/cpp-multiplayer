#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/config.hpp"

namespace net {

struct EnemyState;

enum class SkillBranch : uint8_t {
    Dps,
    Shield,
    Support,
};

enum class SkillId : uint8_t {
    None = 0,
    Thunderstrike = 1,
    ChainLightning = 2,
    StormBurst = 3,
    BarrierPulse = 4,
    WardField = 5,
    AegisStorm = 6,
    Mend = 7,
    RenewalField = 8,
    VitalSurge = 9,
};

inline constexpr int kMaxTeamLevel = 3;
inline constexpr int kArenaStartTeamLevel = 1;
inline constexpr int kGoblinKillXp = 25;
inline constexpr int kBossKillXp = 75;
inline constexpr int kTeamXpLevel1 = 40;
inline constexpr int kTeamXpLevel2 = 100;
inline constexpr int kTeamXpLevel3 = 180;

inline constexpr int kThunderVfxFrameCount = 13;
inline constexpr int kThunderVfxTicksPerFrame = 2;
inline constexpr int kThunderVfxDurationTicks =
    kThunderVfxFrameCount * kThunderVfxTicksPerFrame;

inline const char* SkillBranchName(SkillBranch branch) {
    switch (branch) {
        case SkillBranch::Dps: return "dps";
        case SkillBranch::Shield: return "shield";
        case SkillBranch::Support: return "support";
    }
    return "dps";
}

inline SkillBranch ParseSkillBranch(const std::string& value) {
    if (value == "shield") return SkillBranch::Shield;
    if (value == "support") return SkillBranch::Support;
    return SkillBranch::Dps;
}

inline const char* SkillIdName(SkillId id) {
    switch (id) {
        case SkillId::Thunderstrike: return "thunderstrike";
        case SkillId::ChainLightning: return "chain_lightning";
        case SkillId::StormBurst: return "storm_burst";
        case SkillId::BarrierPulse: return "barrier_pulse";
        case SkillId::WardField: return "ward_field";
        case SkillId::AegisStorm: return "aegis_storm";
        case SkillId::Mend: return "mend";
        case SkillId::RenewalField: return "renewal_field";
        case SkillId::VitalSurge: return "vital_surge";
        case SkillId::None:
        default: return "none";
    }
}

inline SkillId ParseSkillId(const std::string& value) {
    if (value == "thunderstrike") return SkillId::Thunderstrike;
    if (value == "chain_lightning") return SkillId::ChainLightning;
    if (value == "storm_burst") return SkillId::StormBurst;
    if (value == "barrier_pulse") return SkillId::BarrierPulse;
    if (value == "ward_field") return SkillId::WardField;
    if (value == "aegis_storm") return SkillId::AegisStorm;
    if (value == "mend") return SkillId::Mend;
    if (value == "renewal_field") return SkillId::RenewalField;
    if (value == "vital_surge") return SkillId::VitalSurge;
    return SkillId::None;
}

inline int SkillIdToInt(SkillId id) { return static_cast<int>(id); }
inline SkillId SkillIdFromInt(int value) { return static_cast<SkillId>(value); }

struct SkillDef {
    SkillId id = SkillId::None;
    SkillBranch branch = SkillBranch::Dps;
    int tier = 0;
    const char* displayName = "";
    int cooldownSeconds = 0;
    int rangeCells = 0;
    int aoeRadius = 0;
    int damage = 0;
    int heal = 0;
    int shieldGrant = 0;
};

inline int TeamXpThresholdForLevel(int level) {
    switch (level) {
        case 1: return kTeamXpLevel1;
        case 2: return kTeamXpLevel2;
        case 3: return kTeamXpLevel3;
        default: return 0;
    }
}

inline int TeamXpToNextLevel(int teamLevel, int teamXp) {
    if (teamLevel >= kMaxTeamLevel) {
        return 0;
    }
    return TeamXpThresholdForLevel(teamLevel + 1) - teamXp;
}

const SkillDef& SkillDefFor(SkillId id);
SkillId SkillForBranchTier(SkillBranch branch, int tier);
bool SkillRequiresGridTarget(SkillId id);
bool SkillIsUnlocked(const std::vector<int>& unlockedSkills, SkillId id);

void CollectSkillTargetCells(int centerCol, int centerRow, int aoeRadius,
                             std::vector<std::pair<int, int>>& out);
bool IsCellInSkillRange(int casterCol, int casterRow, int targetCol, int targetRow,
                        int rangeCells);
bool IsEnemyInSkillArea(const EnemyState& enemy, int centerCol, int centerRow, int aoeRadius);

}  // namespace net
