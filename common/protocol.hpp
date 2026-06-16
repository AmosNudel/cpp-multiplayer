#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace net {

enum class MessageType {
    JoinRequest,
    JoinAccepted,
    JoinRejected,
    PlayerInput,
    WorldState,
    PlayerLeft,
    Ping,
    Pong,
    Chat,
    ChatHistory,
};

struct PlayerInput {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
};

struct PlayerState {
    int id = 0;
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
};

struct JoinRequest {
    std::string name;
};

struct JoinAccepted {
    int playerId = 0;
    std::vector<PlayerState> players;
};

struct JoinRejected {
    std::string reason;
};

struct PlayerLeft {
    int playerId = 0;
};

struct WorldState {
    uint32_t tick = 0;
    std::vector<PlayerState> players;
};

struct PingMessage {
    uint32_t clientTimeMs = 0;
};

struct PongMessage {
    uint32_t clientTimeMs = 0;
    uint32_t serverTimeMs = 0;
};

struct ChatMessage {
    int playerId = 0;
    std::string name;
    std::string text;
};

struct ChatHistoryMessage {
    std::vector<ChatMessage> messages;
};

struct Message {
    MessageType type = MessageType::JoinRequest;
    JoinRequest joinRequest;
    JoinAccepted joinAccepted;
    JoinRejected joinRejected;
    PlayerInput playerInput;
    WorldState worldState;
    PlayerLeft playerLeft;
    PingMessage ping;
    PongMessage pong;
    ChatMessage chat;
    ChatHistoryMessage chatHistory;
};

const char* MessageTypeName(MessageType type);
MessageType ParseMessageType(const std::string& value);

Message MakeJoinRequest(const std::string& name);
Message MakeJoinAccepted(int playerId, const std::vector<PlayerState>& players);
Message MakeJoinRejected(const std::string& reason);
Message MakePlayerInput(const PlayerInput& input);
Message MakeWorldState(uint32_t tick, const std::vector<PlayerState>& players);
Message MakePlayerLeft(int playerId);
Message MakePing(uint32_t clientTimeMs);
Message MakePong(uint32_t clientTimeMs, uint32_t serverTimeMs);
Message MakeChatSend(const std::string& text);
Message MakeChatBroadcast(int playerId, const std::string& name, const std::string& text);
Message MakeChatHistory(const std::vector<ChatMessage>& messages);

std::string SerializeMessage(const Message& message);
std::optional<Message> DeserializeMessage(const std::string& json);

// TCP uses a 4-byte big-endian length prefix before each JSON payload.
std::vector<uint8_t> FrameTcpMessage(const std::string& json);
std::optional<std::string> TryExtractTcpMessage(std::string& buffer);

// WebSocket sends raw JSON (the WebSocket frame already provides boundaries).
inline std::string FrameWsMessage(const std::string& json) { return json; }

}  // namespace net
