#include "client/audio_manager.hpp"

#include <algorithm>
#include <string>

#include "common/skills.hpp"

namespace {

constexpr float kBgMusicVolume = 0.35f;
constexpr float kLocalSfxVolume = 0.55f;
constexpr float kSharedSfxVolume = 0.65f;
constexpr double kStepIntervalSeconds = 0.38;

const char* kBgMusicFile = "bg_music.mp3";
const char* kStepFile = "03_Step_grass_03.wav";
const char* kAttackFile = "56_Attack_03.wav";
const char* kHitFile = "61_Hit_03.wav";
const char* kThunderFile = "18_Thunder_02.wav";
const char* kHealFile = "02_Heal_02.wav";
const char* kChargeFile = "45_Charge_05.wav";

}  // namespace

std::string AudioManager::ResolveSfxPath(const char* fileName) {
    const std::string name = fileName;
    const std::string candidates[] = {
        "assets/sfx/" + name,
        "../assets/sfx/" + name,
        "sfx/" + name,
        "../sfx/" + name,
    };
    for (const std::string& path : candidates) {
        if (FileExists(path.c_str())) {
            return path;
        }
    }
    return candidates[0];
}

void AudioManager::Init() {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        TraceLog(LOG_WARNING, "AUDIO: Device failed to initialize");
        return;
    }

    const std::string bgPath = ResolveSfxPath(kBgMusicFile);
    if (FileExists(bgPath.c_str())) {
        bgMusic_ = LoadMusicStream(bgPath.c_str());
        bgMusicLoaded_ = bgMusic_.frameCount > 0;
        if (bgMusicLoaded_) {
            SetMusicVolume(bgMusic_, kBgMusicVolume);
            TraceLog(LOG_INFO, "AUDIO: Loaded music %s", bgPath.c_str());
        } else {
            TraceLog(LOG_WARNING, "AUDIO: Failed to load music %s", bgPath.c_str());
        }
    } else {
        TraceLog(LOG_WARNING, "AUDIO: Missing music at %s", bgPath.c_str());
    }

    auto loadSound = [](const char* fileName, Sound& sound, bool& loaded) {
        const std::string resolved = AudioManager::ResolveSfxPath(fileName);
        if (!FileExists(resolved.c_str())) {
            TraceLog(LOG_WARNING, "AUDIO: Missing sfx %s", resolved.c_str());
            return;
        }
        sound = LoadSound(resolved.c_str());
        loaded = sound.frameCount > 0;
        if (loaded) {
            TraceLog(LOG_INFO, "AUDIO: Loaded sfx %s", resolved.c_str());
        } else {
            TraceLog(LOG_WARNING, "AUDIO: Failed to load sfx %s", resolved.c_str());
        }
    };

    loadSound(kStepFile, step_, stepLoaded_);
    loadSound(kAttackFile, attack_, attackLoaded_);
    loadSound(kHitFile, hit_, hitLoaded_);
    loadSound(kThunderFile, thunder_, thunderLoaded_);
    loadSound(kHealFile, heal_, healLoaded_);
    loadSound(kChargeFile, charge_, chargeLoaded_);

    ApplyMuteVolumes();
    StartBackgroundMusic();
}

void AudioManager::Shutdown() {
    StopBackgroundMusic();

    if (stepLoaded_) {
        UnloadSound(step_);
        stepLoaded_ = false;
    }
    if (attackLoaded_) {
        UnloadSound(attack_);
        attackLoaded_ = false;
    }
    if (hitLoaded_) {
        UnloadSound(hit_);
        hitLoaded_ = false;
    }
    if (thunderLoaded_) {
        UnloadSound(thunder_);
        thunderLoaded_ = false;
    }
    if (healLoaded_) {
        UnloadSound(heal_);
        healLoaded_ = false;
    }
    if (chargeLoaded_) {
        UnloadSound(charge_);
        chargeLoaded_ = false;
    }
    if (bgMusicLoaded_) {
        UnloadMusicStream(bgMusic_);
        bgMusicLoaded_ = false;
    }

    CloseAudioDevice();
}

void AudioManager::SetMuted(bool muted) {
    if (muted_ == muted) {
        return;
    }

    muted_ = muted;
    ApplyMuteVolumes();

    if (muted_) {
        StopBackgroundMusic();
    } else {
        StartBackgroundMusic();
    }
}

void AudioManager::ToggleMute() {
    SetMuted(!muted_);
}

void AudioManager::ApplyMuteVolumes() {
    const float musicVolume = muted_ ? 0.0f : kBgMusicVolume;
    const float localVolume = muted_ ? 0.0f : kLocalSfxVolume;
    const float sharedVolume = muted_ ? 0.0f : kSharedSfxVolume;

    if (bgMusicLoaded_) {
        SetMusicVolume(bgMusic_, musicVolume);
    }
    if (stepLoaded_) {
        SetSoundVolume(step_, localVolume);
    }
    if (attackLoaded_) {
        SetSoundVolume(attack_, localVolume);
    }
    if (hitLoaded_) {
        SetSoundVolume(hit_, localVolume);
    }
    if (thunderLoaded_) {
        SetSoundVolume(thunder_, sharedVolume);
    }
    if (healLoaded_) {
        SetSoundVolume(heal_, sharedVolume);
    }
    if (chargeLoaded_) {
        SetSoundVolume(charge_, sharedVolume);
    }
}

void AudioManager::StartBackgroundMusic() {
    if (muted_ || !bgMusicLoaded_) {
        return;
    }
    if (!IsMusicStreamPlaying(bgMusic_)) {
        PlayMusicStream(bgMusic_);
    }
}

void AudioManager::StopBackgroundMusic() {
    if (bgMusicLoaded_ && IsMusicStreamPlaying(bgMusic_)) {
        StopMusicStream(bgMusic_);
    }
}

void AudioManager::Update() {
    if (bgMusicLoaded_ && !muted_ && IsMusicStreamPlaying(bgMusic_)) {
        UpdateMusicStream(bgMusic_);
    }
}

void AudioManager::OnJoined() {
    joined_ = true;
    lastLocalState_ = net::EntityState::Idle;
    lastLocalAnim_ = net::PlayerAnim::Idle;
    lastLocalHp_ = -1;
    lastAttackAnimStart_ = 0;
    lastStepTime_ = 0.0;
    seenSkillEffects_.clear();
}

void AudioManager::OnDisconnected() {
    joined_ = false;
    seenSkillEffects_.clear();
}

void AudioManager::SeedSkillEffects(const std::vector<net::SkillEffectState>& effects) {
    seenSkillEffects_.clear();
    seenSkillEffects_.reserve(effects.size());
    for (const net::SkillEffectState& effect : effects) {
        seenSkillEffects_.push_back({effect.skillId, effect.casterId, effect.startTick});
    }
}

void AudioManager::PlayLocal(Sound sound) {
    if (muted_) {
        return;
    }
    PlaySound(sound);
}

void AudioManager::PlayShared(Sound sound) {
    if (muted_) {
        return;
    }
    PlaySound(sound);
}

void AudioManager::UpdateLocalPlayer(const net::PlayerState& player, uint32_t /*serverTick*/) {
    if (!joined_) {
        return;
    }

    if (player.state == net::EntityState::Moving) {
        const double now = GetTime();
        if (now - lastStepTime_ >= kStepIntervalSeconds) {
            if (stepLoaded_) {
                PlayLocal(step_);
            }
            lastStepTime_ = now;
        }
    }

    const bool attackAnimStarted =
        net::IsAttackAnim(player.anim) &&
        (player.animStartTick != lastAttackAnimStart_ ||
         !net::IsAttackAnim(lastLocalAnim_));
    if (player.state == net::EntityState::Combat && attackAnimStarted) {
        if (attackLoaded_) {
            PlayLocal(attack_);
        }
        lastAttackAnimStart_ = player.animStartTick;
    }

    const bool enteredHitState =
        player.state == net::EntityState::Hit && lastLocalState_ != net::EntityState::Hit;
    const bool tookDamage = lastLocalHp_ >= 0 && player.hp < lastLocalHp_;
    if (enteredHitState || tookDamage) {
        if (hitLoaded_) {
            PlayLocal(hit_);
        }
    }

    lastLocalState_ = player.state;
    lastLocalAnim_ = player.anim;
    lastLocalHp_ = player.hp;
}

void AudioManager::UpdateSkillEffects(const std::vector<net::SkillEffectState>& effects) {
    if (!joined_) {
        return;
    }

    for (const net::SkillEffectState& effect : effects) {
        const SkillEffectKey key{effect.skillId, effect.casterId, effect.startTick};
        const auto it = std::find(seenSkillEffects_.begin(), seenSkillEffects_.end(), key);
        if (it != seenSkillEffects_.end()) {
            continue;
        }

        seenSkillEffects_.push_back(key);

        const net::SkillDef& def = net::SkillDefFor(net::SkillIdFromInt(effect.skillId));
        switch (def.branch) {
            case net::SkillBranch::Dps:
                if (thunderLoaded_) {
                    PlayShared(thunder_);
                }
                break;
            case net::SkillBranch::Support:
                if (healLoaded_) {
                    PlayShared(heal_);
                }
                break;
            case net::SkillBranch::Shield:
                if (chargeLoaded_) {
                    PlayShared(charge_);
                }
                break;
        }
    }
}
