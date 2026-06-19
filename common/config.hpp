#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>

namespace net {

// Server tick rate (authoritative simulation steps per second).
inline constexpr int kTickRate = 20;
inline constexpr float kTickDuration = 1.0f / static_cast<float>(kTickRate);

// Default ports (overridden by environment variables on the server).
inline constexpr uint16_t kDefaultTcpPort = 7777;
inline constexpr uint16_t kDefaultWsPort = 7778;

// Maximum players and world bounds for the starter template.
inline constexpr int kMaxPlayers = 8;
inline constexpr float kWorldWidth = 800.0f;
inline constexpr float kWorldHeight = 600.0f;
inline constexpr float kPlayerSpeed = 200.0f;
inline constexpr float kPlayerRadius = 16.0f;
inline constexpr int kIdleFrameCount = 7;
inline constexpr int kIdleAnimTicksPerFrame = 2;
inline constexpr int kRunFrameCount = 8;
inline constexpr int kRunAnimTicksPerFrame = 1;
inline constexpr int kAttack1FrameCount = 6;
inline constexpr int kAttack2FrameCount = 5;
inline constexpr int kAttack3FrameCount = 6;
inline constexpr int kAttack1DamageFrame = 3;
inline constexpr int kAttack2DamageFrame = 1;
inline constexpr int kAttack3DamageFrame = 2;
inline constexpr int kAttackAnimTicksPerFrame = 2;
inline constexpr int kComboPauseTicks = 4;
inline constexpr int kHitFrameCount = 4;
inline constexpr int kHitAnimTicksPerFrame = 2;
inline constexpr int kJumpFrameCount = 5;
inline constexpr int kJumpAnimTicksPerFrame = 2;
inline constexpr int kDeadFrameCount = 13;
inline constexpr int kDeadAnimTicksPerFrame = 3;
inline constexpr int kMaxChatLength = 120;

enum class PlayerAnim {
    Idle,
    Run,
    Attack1,
    Attack2,
    Attack3,
    Hit,
    Jump,
    Dead,
};

inline const char* PlayerAnimName(PlayerAnim anim) {
    switch (anim) {
        case PlayerAnim::Idle: return "idle";
        case PlayerAnim::Run: return "run";
        case PlayerAnim::Attack1: return "attack1";
        case PlayerAnim::Attack2: return "attack2";
        case PlayerAnim::Attack3: return "attack3";
        case PlayerAnim::Hit: return "hit";
        case PlayerAnim::Jump: return "jump";
        case PlayerAnim::Dead: return "dead";
    }
    return "idle";
}

inline PlayerAnim ParsePlayerAnim(const std::string& value) {
    if (value == "run") return PlayerAnim::Run;
    if (value == "attack" || value == "attack1") return PlayerAnim::Attack1;
    if (value == "attack2") return PlayerAnim::Attack2;
    if (value == "attack3") return PlayerAnim::Attack3;
    if (value == "hit") return PlayerAnim::Hit;
    if (value == "jump") return PlayerAnim::Jump;
    if (value == "dead") return PlayerAnim::Dead;
    return PlayerAnim::Idle;
}

inline int AnimFrameCount(PlayerAnim anim) {
    switch (anim) {
        case PlayerAnim::Idle: return kIdleFrameCount;
        case PlayerAnim::Run: return kRunFrameCount;
        case PlayerAnim::Attack1: return kAttack1FrameCount;
        case PlayerAnim::Attack2: return kAttack2FrameCount;
        case PlayerAnim::Attack3: return kAttack3FrameCount;
        case PlayerAnim::Hit: return kHitFrameCount;
        case PlayerAnim::Jump: return kJumpFrameCount;
        case PlayerAnim::Dead: return kDeadFrameCount;
    }
    return kIdleFrameCount;
}

inline int AnimTicksPerFrame(PlayerAnim anim) {
    switch (anim) {
        case PlayerAnim::Idle: return kIdleAnimTicksPerFrame;
        case PlayerAnim::Run: return kRunAnimTicksPerFrame;
        case PlayerAnim::Attack1:
        case PlayerAnim::Attack2:
        case PlayerAnim::Attack3:
            return kAttackAnimTicksPerFrame;
        case PlayerAnim::Hit: return kHitAnimTicksPerFrame;
        case PlayerAnim::Jump: return kJumpAnimTicksPerFrame;
        case PlayerAnim::Dead: return kDeadAnimTicksPerFrame;
    }
    return kIdleAnimTicksPerFrame;
}

inline int AttackDamageFrame(PlayerAnim anim) {
    switch (anim) {
        case PlayerAnim::Attack1: return kAttack1DamageFrame;
        case PlayerAnim::Attack2: return kAttack2DamageFrame;
        case PlayerAnim::Attack3: return kAttack3DamageFrame;
        default: return -1;
    }
}

inline bool IsAttackAnim(PlayerAnim anim) {
    return anim == PlayerAnim::Attack1 || anim == PlayerAnim::Attack2 ||
           anim == PlayerAnim::Attack3;
}

inline int AnimFrameIndex(PlayerAnim anim, uint32_t serverTick, uint32_t animStartTick) {
    if (serverTick < animStartTick) {
        return 0;
    }

    const uint32_t elapsed = serverTick - animStartTick;
    const int frameCount = AnimFrameCount(anim);
    const int ticksPerFrame = AnimTicksPerFrame(anim);
    if (frameCount <= 0 || ticksPerFrame <= 0) {
        return 0;
    }

    const int frame = static_cast<int>(elapsed / static_cast<uint32_t>(ticksPerFrame));
    if (IsAttackAnim(anim) || anim == PlayerAnim::Hit || anim == PlayerAnim::Jump ||
        anim == PlayerAnim::Dead) {
        return frame >= frameCount ? frameCount - 1 : frame;
    }

    return static_cast<int>(frame % static_cast<uint32_t>(frameCount));
}
inline constexpr int kMaxChatHistory = 8;
inline constexpr int kServerChatPlayerId = 0;

inline uint16_t EnvPort(const char* name, uint16_t fallback) {
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') {
        return fallback;
    }
    return static_cast<uint16_t>(std::stoi(value));
}

inline std::string EnvString(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') {
        return fallback;
    }
    return value;
}

inline bool HasEnv(const char* name) {
    const char* value = std::getenv(name);
    return value && value[0] != '\0';
}

inline bool TryEnvPort(const char* name, uint16_t& out) {
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') {
        return false;
    }
    try {
        const int parsed = std::stoi(value);
        if (parsed <= 0 || parsed > 65535) {
            return false;
        }
        out = static_cast<uint16_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

struct ServerPorts {
    uint16_t tcp = kDefaultTcpPort;
    uint16_t ws = kDefaultWsPort;
    std::string tcpSource = "default";
    std::string wsSource = "default";
};

// When Railway TCP proxy is enabled, PORT is often auto-set to 7777 (same as TCP).
// Public HTTPS/WSS is routed to PORT, not WS_PORT — you must set PORT=8080 in Railway.
inline constexpr uint16_t kRailwayHttpPortFallback = 8080;

inline ServerPorts ResolveServerPorts() {
    ServerPorts ports;

    if (TryEnvPort("TCP_PORT", ports.tcp)) {
        ports.tcpSource = "TCP_PORT";
    } else if (TryEnvPort("RAILWAY_TCP_APPLICATION_PORT", ports.tcp)) {
        ports.tcpSource = "RAILWAY_TCP_APPLICATION_PORT";
    } else {
        ports.tcp = kDefaultTcpPort;
        ports.tcpSource = "default";
    }

    // PORT is what Railway routes public HTTP/WSS to — must differ from TCP (7777).
    if (TryEnvPort("PORT", ports.ws) && ports.ws != ports.tcp) {
        ports.wsSource = "PORT";
    } else if (TryEnvPort("WS_PORT", ports.ws)) {
        ports.wsSource = "WS_PORT";
    } else if (TryEnvPort("HTTP_PORT", ports.ws)) {
        ports.wsSource = "HTTP_PORT";
    } else {
        ports.ws = kRailwayHttpPortFallback;
        ports.wsSource = "fallback(8080)";
    }

    return ports;
}

}  // namespace net
