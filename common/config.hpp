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

    // Railway injects PORT at runtime for the public HTTP/WebSocket listener.
    // Prefer PORT over WS_PORT — do not set WS_PORT=${{PORT}} in the dashboard.
    if (TryEnvPort("PORT", ports.ws)) {
        ports.wsSource = "PORT";
    } else if (TryEnvPort("WS_PORT", ports.ws)) {
        ports.wsSource = "WS_PORT";
    } else {
        ports.ws = kDefaultWsPort;
        ports.wsSource = "default";
    }

    return ports;
}

}  // namespace net
