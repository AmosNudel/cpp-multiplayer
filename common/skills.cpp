#include "common/skills.hpp"

#include <algorithm>

namespace net {

namespace {

const SkillDef kSkillDefs[] = {
    {SkillId::Thunderstrike, SkillBranch::Dps, 1, "Thunderstrike", 15, 6, 0, 30, 0, 0},
    {SkillId::ChainLightning, SkillBranch::Dps, 2, "Chain Lightning", 25, 6, 1, 18, 0, 0},
    {SkillId::StormBurst, SkillBranch::Dps, 3, "Storm Burst", 40, 8, 2, 35, 0, 0},
    {SkillId::BarrierPulse, SkillBranch::Shield, 1, "Barrier Pulse", 18, 5, 0, 0, 0, 25},
    {SkillId::WardField, SkillBranch::Shield, 2, "Ward Field", 28, 6, 1, 0, 0, 20},
    {SkillId::AegisStorm, SkillBranch::Shield, 3, "Aegis Storm", 40, 8, 2, 0, 0, 35},
    {SkillId::Mend, SkillBranch::Support, 1, "Mend", 15, 5, 0, 0, 25, 0},
    {SkillId::RenewalField, SkillBranch::Support, 2, "Renewal Field", 25, 6, 1, 0, 18, 0},
    {SkillId::VitalSurge, SkillBranch::Support, 3, "Vital Surge", 40, 8, 2, 0, 30, 0},
};

}  // namespace

const SkillDef& SkillDefFor(SkillId id) {
    for (const SkillDef& def : kSkillDefs) {
        if (def.id == id) {
            return def;
        }
    }
    return kSkillDefs[0];
}

SkillId SkillForBranchTier(SkillBranch branch, int tier) {
    for (const SkillDef& def : kSkillDefs) {
        if (def.branch == branch && def.tier == tier) {
            return def.id;
        }
    }
    return SkillId::None;
}

bool SkillRequiresGridTarget(SkillId id) {
    const SkillDef& def = SkillDefFor(id);
    return def.id != SkillId::None && def.rangeCells > 0;
}

bool SkillIsUnlocked(const std::vector<int>& unlockedSkills, SkillId id) {
    const int skillId = SkillIdToInt(id);
    return std::find(unlockedSkills.begin(), unlockedSkills.end(), skillId) !=
           unlockedSkills.end();
}

void CollectSkillTargetCells(int centerCol, int centerRow, int aoeRadius,
                             std::vector<std::pair<int, int>>& out) {
    out.clear();
    if (aoeRadius <= 0) {
        out.emplace_back(centerCol, centerRow);
        return;
    }

    for (int row = centerRow - aoeRadius; row <= centerRow + aoeRadius; ++row) {
        for (int col = centerCol - aoeRadius; col <= centerCol + aoeRadius; ++col) {
            out.emplace_back(col, row);
        }
    }
}

bool IsCellInSkillRange(int casterCol, int casterRow, int targetCol, int targetRow,
                        int rangeCells) {
    const int distance =
        std::abs(targetCol - casterCol) + std::abs(targetRow - casterRow);
    return distance <= rangeCells;
}

}  // namespace net
