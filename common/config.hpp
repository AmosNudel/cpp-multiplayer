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
inline constexpr int kMaxChatLength = 120;

enum class PlayerAnim {
    Idle,
    Run,
};

inline const char* PlayerAnimName(PlayerAnim anim) {
    switch (anim) {
        case PlayerAnim::Idle: return "idle";
        case PlayerAnim::Run: return "run";
    }
    return "idle";
}

inline PlayerAnim ParsePlayerAnim(const std::string& value) {
    if (value == "run") {
        return PlayerAnim::Run;
    }
    return PlayerAnim::Idle;
}

inline int AnimFrameCount(PlayerAnim anim) {
    switch (anim) {
        case PlayerAnim::Idle: return kIdleFrameCount;
        case PlayerAnim::Run: return kRunFrameCount;
    }
    return kIdleFrameCount;
}

inline int AnimTicksPerFrame(PlayerAnim anim) {
    switch (anim) {
        case PlayerAnim::Idle: return kIdleAnimTicksPerFrame;
        case PlayerAnim::Run: return kRunAnimTicksPerFrame;
    }
    return kIdleAnimTicksPerFrame;
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

    return static_cast<int>((elapsed / static_cast<uint32_t>(ticksPerFrame)) %
                            static_cast<uint32_t>(frameCount));
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

// When Railway TCP proxy is enabled, PORT is often set to the TCP port (7777)
// instead of the public HTTP port. Use WS_PORT or HTTP_PORT for WebSocket.
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

    if (TryEnvPort("WS_PORT", ports.ws)) {
        ports.wsSource = "WS_PORT";
    } else if (TryEnvPort("HTTP_PORT", ports.ws)) {
        ports.wsSource = "HTTP_PORT";
    } else if (TryEnvPort("PORT", ports.ws) && ports.ws != ports.tcp) {
        ports.wsSource = "PORT";
    } else {
        // Railway TCP proxy sets PORT=7777, same as TCP_PORT — use HTTP fallback.
        ports.ws = kRailwayHttpPortFallback;
        ports.wsSource = "fallback(8080)";
    }

    return ports;
}

}  // namespace net
