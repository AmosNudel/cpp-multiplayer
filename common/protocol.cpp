#include "protocol.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace net {
namespace {

nlohmann::json PlayerStateToJson(const PlayerState& player) {
    return {
        {"id", player.id},
        {"name", player.name},
        {"x", player.x},
        {"y", player.y},
    };
}

PlayerState PlayerStateFromJson(const nlohmann::json& json) {
    PlayerState player;
    player.id = json.at("id").get<int>();
    player.name = json.at("name").get<std::string>();
    player.x = json.at("x").get<float>();
    player.y = json.at("y").get<float>();
    return player;
}

}  // namespace

const char* MessageTypeName(MessageType type) {
    switch (type) {
        case MessageType::JoinRequest: return "join_request";
        case MessageType::JoinAccepted: return "join_accepted";
        case MessageType::JoinRejected: return "join_rejected";
        case MessageType::PlayerInput: return "player_input";
        case MessageType::WorldState: return "world_state";
        case MessageType::PlayerLeft: return "player_left";
        case MessageType::Ping: return "ping";
        case MessageType::Pong: return "pong";
    }
    return "unknown";
}

MessageType ParseMessageType(const std::string& value) {
    if (value == "join_request") return MessageType::JoinRequest;
    if (value == "join_accepted") return MessageType::JoinAccepted;
    if (value == "join_rejected") return MessageType::JoinRejected;
    if (value == "player_input") return MessageType::PlayerInput;
    if (value == "world_state") return MessageType::WorldState;
    if (value == "player_left") return MessageType::PlayerLeft;
    if (value == "ping") return MessageType::Ping;
    if (value == "pong") return MessageType::Pong;
    return MessageType::JoinRequest;
}

Message MakeJoinRequest(const std::string& name) {
    Message message;
    message.type = MessageType::JoinRequest;
    message.joinRequest.name = name;
    return message;
}

Message MakeJoinAccepted(int playerId, const std::vector<PlayerState>& players) {
    Message message;
    message.type = MessageType::JoinAccepted;
    message.joinAccepted.playerId = playerId;
    message.joinAccepted.players = players;
    return message;
}

Message MakeJoinRejected(const std::string& reason) {
    Message message;
    message.type = MessageType::JoinRejected;
    message.joinRejected.reason = reason;
    return message;
}

Message MakePlayerInput(const PlayerInput& input) {
    Message message;
    message.type = MessageType::PlayerInput;
    message.playerInput = input;
    return message;
}

Message MakeWorldState(uint32_t tick, const std::vector<PlayerState>& players) {
    Message message;
    message.type = MessageType::WorldState;
    message.worldState.tick = tick;
    message.worldState.players = players;
    return message;
}

Message MakePlayerLeft(int playerId) {
    Message message;
    message.type = MessageType::PlayerLeft;
    message.playerLeft.playerId = playerId;
    return message;
}

Message MakePing(uint32_t clientTimeMs) {
    Message message;
    message.type = MessageType::Ping;
    message.ping.clientTimeMs = clientTimeMs;
    return message;
}

Message MakePong(uint32_t clientTimeMs, uint32_t serverTimeMs) {
    Message message;
    message.type = MessageType::Pong;
    message.pong.clientTimeMs = clientTimeMs;
    message.pong.serverTimeMs = serverTimeMs;
    return message;
}

std::string SerializeMessage(const Message& message) {
    nlohmann::json json;
    json["type"] = MessageTypeName(message.type);

    switch (message.type) {
        case MessageType::JoinRequest:
            json["name"] = message.joinRequest.name;
            break;
        case MessageType::JoinAccepted:
            json["player_id"] = message.joinAccepted.playerId;
            json["players"] = nlohmann::json::array();
            for (const PlayerState& player : message.joinAccepted.players) {
                json["players"].push_back(PlayerStateToJson(player));
            }
            break;
        case MessageType::JoinRejected:
            json["reason"] = message.joinRejected.reason;
            break;
        case MessageType::PlayerInput:
            json["up"] = message.playerInput.up;
            json["down"] = message.playerInput.down;
            json["left"] = message.playerInput.left;
            json["right"] = message.playerInput.right;
            break;
        case MessageType::WorldState:
            json["tick"] = message.worldState.tick;
            json["players"] = nlohmann::json::array();
            for (const PlayerState& player : message.worldState.players) {
                json["players"].push_back(PlayerStateToJson(player));
            }
            break;
        case MessageType::PlayerLeft:
            json["player_id"] = message.playerLeft.playerId;
            break;
        case MessageType::Ping:
            json["client_time_ms"] = message.ping.clientTimeMs;
            break;
        case MessageType::Pong:
            json["client_time_ms"] = message.pong.clientTimeMs;
            json["server_time_ms"] = message.pong.serverTimeMs;
            break;
    }

    return json.dump();
}

std::optional<Message> DeserializeMessage(const std::string& jsonText) {
    try {
        nlohmann::json json = nlohmann::json::parse(jsonText);
        Message message;
        message.type = ParseMessageType(json.at("type").get<std::string>());

        switch (message.type) {
            case MessageType::JoinRequest:
                message.joinRequest.name = json.at("name").get<std::string>();
                break;
            case MessageType::JoinAccepted:
                message.joinAccepted.playerId = json.at("player_id").get<int>();
                for (const auto& playerJson : json.at("players")) {
                    message.joinAccepted.players.push_back(PlayerStateFromJson(playerJson));
                }
                break;
            case MessageType::JoinRejected:
                message.joinRejected.reason = json.at("reason").get<std::string>();
                break;
            case MessageType::PlayerInput:
                message.playerInput.up = json.value("up", false);
                message.playerInput.down = json.value("down", false);
                message.playerInput.left = json.value("left", false);
                message.playerInput.right = json.value("right", false);
                break;
            case MessageType::WorldState:
                message.worldState.tick = json.at("tick").get<uint32_t>();
                for (const auto& playerJson : json.at("players")) {
                    message.worldState.players.push_back(PlayerStateFromJson(playerJson));
                }
                break;
            case MessageType::PlayerLeft:
                message.playerLeft.playerId = json.at("player_id").get<int>();
                break;
            case MessageType::Ping:
                message.ping.clientTimeMs = json.at("client_time_ms").get<uint32_t>();
                break;
            case MessageType::Pong:
                message.pong.clientTimeMs = json.at("client_time_ms").get<uint32_t>();
                message.pong.serverTimeMs = json.at("server_time_ms").get<uint32_t>();
                break;
        }

        return message;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<uint8_t> FrameTcpMessage(const std::string& json) {
    const uint32_t length = static_cast<uint32_t>(json.size());
    std::vector<uint8_t> frame(4 + json.size());
    frame[0] = static_cast<uint8_t>((length >> 24) & 0xFF);
    frame[1] = static_cast<uint8_t>((length >> 16) & 0xFF);
    frame[2] = static_cast<uint8_t>((length >> 8) & 0xFF);
    frame[3] = static_cast<uint8_t>(length & 0xFF);
    std::memcpy(frame.data() + 4, json.data(), json.size());
    return frame;
}

std::optional<std::string> TryExtractTcpMessage(std::string& buffer) {
    if (buffer.size() < 4) {
        return std::nullopt;
    }

    const auto* bytes = reinterpret_cast<const uint8_t*>(buffer.data());
    const uint32_t length =
        (static_cast<uint32_t>(bytes[0]) << 24) |
        (static_cast<uint32_t>(bytes[1]) << 16) |
        (static_cast<uint32_t>(bytes[2]) << 8) |
        static_cast<uint32_t>(bytes[3]);

    if (buffer.size() < 4 + length) {
        return std::nullopt;
    }

    std::string message = buffer.substr(4, length);
    buffer.erase(0, 4 + length);
    return message;
}

}  // namespace net
