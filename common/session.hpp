#pragma once

#include <cstdint>
#include <string>

#include "common/config.hpp"

namespace net {

enum class SceneId : uint8_t {
    Hub,
    Arena,
};

enum class SessionPhase : uint8_t {
    HubIdle,
    Lobby,
    ArenaActive,
};

inline constexpr int kHubPortalCol = 10;
inline constexpr int kHubPortalRow = 13;
inline constexpr int kHubResetCol = 8;
inline constexpr int kHubResetRow = 13;
inline constexpr int kLobbyDurationSeconds = 30;
inline constexpr int kArenaDurationSeconds = 180;
inline constexpr int kAllDeadReturnSeconds = 5;
inline constexpr int kArenaDeathRespawnDelaySeconds = 20;
inline constexpr int kArenaRejoinDelaySeconds = 30;
inline constexpr uint32_t kLobbyDurationTicks =
    static_cast<uint32_t>(kLobbyDurationSeconds * kTickRate);
inline constexpr uint32_t kArenaDurationTicks =
    static_cast<uint32_t>(kArenaDurationSeconds * kTickRate);
inline constexpr uint32_t kAllDeadReturnTicks =
    static_cast<uint32_t>(kAllDeadReturnSeconds * kTickRate);
inline constexpr uint32_t kArenaDeathRespawnDelayTicks =
    static_cast<uint32_t>(kArenaDeathRespawnDelaySeconds * kTickRate);
inline constexpr uint32_t kArenaRejoinDelayTicks =
    static_cast<uint32_t>(kArenaRejoinDelaySeconds * kTickRate);

inline const char* SceneIdName(SceneId scene) {
    switch (scene) {
        case SceneId::Hub: return "hub";
        case SceneId::Arena: return "arena";
    }
    return "hub";
}

inline const char* SessionPhaseName(SessionPhase phase) {
    switch (phase) {
        case SessionPhase::HubIdle: return "hub_idle";
        case SessionPhase::Lobby: return "lobby";
        case SessionPhase::ArenaActive: return "arena_active";
    }
    return "hub_idle";
}

inline SceneId ParseSceneId(const std::string& value) {
    if (value == "arena") {
        return SceneId::Arena;
    }
    return SceneId::Hub;
}

inline SessionPhase ParseSessionPhase(const std::string& value) {
    if (value == "lobby") {
        return SessionPhase::Lobby;
    }
    if (value == "arena_active") {
        return SessionPhase::ArenaActive;
    }
    return SessionPhase::HubIdle;
}

inline bool IsPortalCell(int col, int row) {
    return col == kHubPortalCol && row == kHubPortalRow;
}

inline bool IsResetArenaCell(int col, int row) {
    return col == kHubResetCol && row == kHubResetRow;
}

}  // namespace net
