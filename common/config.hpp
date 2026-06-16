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

}  // namespace net
