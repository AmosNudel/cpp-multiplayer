#pragma once

#include <cstdint>
#include <vector>

#include "raylib.h"

#include "common/entity_state.hpp"
#include "common/protocol.hpp"

class AudioManager {
public:
    void Init();
    void Shutdown();

    void SetMuted(bool muted);
    bool IsMuted() const { return muted_; }
    void ToggleMute();

    void Update();

    void OnJoined();
    void OnDisconnected();
    void SeedSkillEffects(const std::vector<net::SkillEffectState>& effects);

    void UpdateLocalPlayer(const net::PlayerState& player, uint32_t serverTick);
    void UpdateSkillEffects(const std::vector<net::SkillEffectState>& effects);

private:
    void ApplyMuteVolumes();
    void PlayLocal(Sound sound);
    void PlayShared(Sound sound);
    void StartBackgroundMusic();
    void StopBackgroundMusic();

    static std::string ResolveSfxPath(const char* relativePath);

    bool muted_ = false;
    bool joined_ = false;

    Music bgMusic_{};
    Sound step_{};
    Sound attack_{};
    Sound hit_{};
    Sound thunder_{};
    Sound heal_{};
    Sound charge_{};

    bool bgMusicLoaded_ = false;
    bool stepLoaded_ = false;
    bool attackLoaded_ = false;
    bool hitLoaded_ = false;
    bool thunderLoaded_ = false;
    bool healLoaded_ = false;
    bool chargeLoaded_ = false;

    net::EntityState lastLocalState_ = net::EntityState::Idle;
    net::PlayerAnim lastLocalAnim_ = net::PlayerAnim::Idle;
    int lastLocalHp_ = -1;
    uint32_t lastAttackAnimStart_ = 0;
    double lastStepTime_ = 0.0;

    struct SkillEffectKey {
        int skillId = 0;
        int casterId = 0;
        uint32_t startTick = 0;

        bool operator==(const SkillEffectKey& other) const {
            return skillId == other.skillId && casterId == other.casterId &&
                   startTick == other.startTick;
        }
    };

    std::vector<SkillEffectKey> seenSkillEffects_;
};
