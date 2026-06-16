#include "protocol.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace net {
namespace {

nlohmann::json PlayerStateToJson(const PlayerState& player) {
    nlohmann::json json = {
        {"id", player.id},
        {"name", player.name},
        {"x", player.x},
        {"y", player.y},
        {"state", EntityStateName(player.state)},
        {"state_start_tick", player.stateStartTick},
        {"anim", PlayerAnimName(player.anim)},
        {"anim_start_tick", player.animStartTick},
        {"facing_right", player.facingRight},
        {"hp", player.hp},
    };
    if (player.targetId >= 0) {
        json["target_id"] = player.targetId;
    }
    if (player.moveTargetCol >= 0 && player.moveTargetRow >= 0) {
        json["move_target_col"] = player.moveTargetCol;
        json["move_target_row"] = player.moveTargetRow;
    }
    return json;
}

PlayerState PlayerStateFromJson(const nlohmann::json& json) {
    PlayerState player;
    player.id = json.at("id").get<int>();
    player.name = json.at("name").get<std::string>();
    player.x = json.at("x").get<float>();
    player.y = json.at("y").get<float>();
    player.state = ParseEntityState(json.value("state", "idle"));
    player.stateStartTick = json.value("state_start_tick", 0u);
    player.anim = ParsePlayerAnim(json.value("anim", "idle"));
    player.animStartTick = json.value("anim_start_tick", 0u);
    player.facingRight = json.value("facing_right", true);
    player.hp = json.value("hp", kPlayerMaxHp);
    player.targetId = json.value("target_id", -1);
    player.moveTargetCol = json.value("move_target_col", -1);
    player.moveTargetRow = json.value("move_target_row", -1);
    return player;
}

nlohmann::json EnemyStateToJson(const EnemyState& enemy) {
    nlohmann::json json = {
        {"id", enemy.id},
        {"kind", enemy.kind},
        {"x", enemy.x},
        {"y", enemy.y},
        {"state", EntityStateName(enemy.state)},
        {"state_start_tick", enemy.stateStartTick},
        {"anim", PlayerAnimName(enemy.anim)},
        {"anim_start_tick", enemy.animStartTick},
        {"facing_right", enemy.facingRight},
        {"hp", enemy.hp},
    };
    if (enemy.targetId >= 0) {
        json["target_id"] = enemy.targetId;
    }
    return json;
}

EnemyState EnemyStateFromJson(const nlohmann::json& json) {
    EnemyState enemy;
    enemy.id = json.at("id").get<int>();
    enemy.kind = json.value("kind", "goblin");
    enemy.x = json.at("x").get<float>();
    enemy.y = json.at("y").get<float>();
    enemy.state = ParseEntityState(json.value("state", "idle"));
    enemy.stateStartTick = json.value("state_start_tick", 0u);
    enemy.anim = ParsePlayerAnim(json.value("anim", "idle"));
    enemy.animStartTick = json.value("anim_start_tick", 0u);
    enemy.facingRight = json.value("facing_right", true);
    enemy.hp = json.value("hp", kGoblinMaxHp);
    enemy.targetId = json.value("target_id", -1);
    return enemy;
}

nlohmann::json ChatMessageToJson(const ChatMessage& chat) {
    nlohmann::json json;
    if (chat.playerId != 0) {
        json["player_id"] = chat.playerId;
    }
    if (!chat.name.empty()) {
        json["name"] = chat.name;
    }
    json["text"] = chat.text;
    return json;
}

ChatMessage ChatMessageFromJson(const nlohmann::json& json) {
    ChatMessage chat;
    chat.playerId = json.value("player_id", 0);
    chat.name = json.value("name", "");
    chat.text = json.at("text").get<std::string>();
    return chat;
}

}  // namespace

const char* MessageTypeName(MessageType type) {
    switch (type) {
        case MessageType::JoinRequest: return "join_request";
        case MessageType::JoinAccepted: return "join_accepted";
        case MessageType::JoinRejected: return "join_rejected";
        case MessageType::MoveRequest: return "move_request";
        case MessageType::AttackRequest: return "attack_request";
        case MessageType::CancelCombatRequest: return "cancel_combat_request";
        case MessageType::WorldState: return "world_state";
        case MessageType::PlayerLeft: return "player_left";
        case MessageType::Ping: return "ping";
        case MessageType::Pong: return "pong";
        case MessageType::Chat: return "chat";
        case MessageType::ChatHistory: return "chat_history";
    }
    return "unknown";
}

MessageType ParseMessageType(const std::string& value) {
    if (value == "join_request") return MessageType::JoinRequest;
    if (value == "join_accepted") return MessageType::JoinAccepted;
    if (value == "join_rejected") return MessageType::JoinRejected;
    if (value == "move_request") return MessageType::MoveRequest;
    if (value == "attack_request") return MessageType::AttackRequest;
    if (value == "cancel_combat_request") return MessageType::CancelCombatRequest;
    if (value == "world_state") return MessageType::WorldState;
    if (value == "player_left") return MessageType::PlayerLeft;
    if (value == "ping") return MessageType::Ping;
    if (value == "pong") return MessageType::Pong;
    if (value == "chat") return MessageType::Chat;
    if (value == "chat_history") return MessageType::ChatHistory;
    return MessageType::JoinRequest;
}

Message MakeJoinRequest(const std::string& name) {
    Message message;
    message.type = MessageType::JoinRequest;
    message.joinRequest.name = name;
    return message;
}

Message MakeJoinAccepted(int playerId, const std::vector<PlayerState>& players,
                         const std::vector<EnemyState>& enemies) {
    Message message;
    message.type = MessageType::JoinAccepted;
    message.joinAccepted.playerId = playerId;
    message.joinAccepted.players = players;
    message.joinAccepted.enemies = enemies;
    return message;
}

Message MakeJoinRejected(const std::string& reason) {
    Message message;
    message.type = MessageType::JoinRejected;
    message.joinRejected.reason = reason;
    return message;
}

Message MakeMoveRequest(int col, int row) {
    Message message;
    message.type = MessageType::MoveRequest;
    message.moveRequest.col = col;
    message.moveRequest.row = row;
    return message;
}

Message MakeAttackRequest(int enemyId) {
    Message message;
    message.type = MessageType::AttackRequest;
    message.attackRequest.enemyId = enemyId;
    return message;
}

Message MakeCancelCombatRequest() {
    Message message;
    message.type = MessageType::CancelCombatRequest;
    return message;
}

Message MakeWorldState(uint32_t tick, const std::vector<PlayerState>& players,
                       const std::vector<EnemyState>& enemies) {
    Message message;
    message.type = MessageType::WorldState;
    message.worldState.tick = tick;
    message.worldState.players = players;
    message.worldState.enemies = enemies;
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

Message MakeChatSend(const std::string& text) {
    Message message;
    message.type = MessageType::Chat;
    message.chat.text = text;
    return message;
}

Message MakeChatBroadcast(int playerId, const std::string& name, const std::string& text) {
    Message message;
    message.type = MessageType::Chat;
    message.chat.playerId = playerId;
    message.chat.name = name;
    message.chat.text = text;
    return message;
}

Message MakeChatHistory(const std::vector<ChatMessage>& messages) {
    Message message;
    message.type = MessageType::ChatHistory;
    message.chatHistory.messages = messages;
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
            json["enemies"] = nlohmann::json::array();
            for (const EnemyState& enemy : message.joinAccepted.enemies) {
                json["enemies"].push_back(EnemyStateToJson(enemy));
            }
            break;
        case MessageType::JoinRejected:
            json["reason"] = message.joinRejected.reason;
            break;
        case MessageType::MoveRequest:
            json["col"] = message.moveRequest.col;
            json["row"] = message.moveRequest.row;
            break;
        case MessageType::AttackRequest:
            json["enemy_id"] = message.attackRequest.enemyId;
            break;
        case MessageType::CancelCombatRequest:
            break;
        case MessageType::WorldState:
            json["tick"] = message.worldState.tick;
            json["players"] = nlohmann::json::array();
            for (const PlayerState& player : message.worldState.players) {
                json["players"].push_back(PlayerStateToJson(player));
            }
            json["enemies"] = nlohmann::json::array();
            for (const EnemyState& enemy : message.worldState.enemies) {
                json["enemies"].push_back(EnemyStateToJson(enemy));
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
        case MessageType::Chat:
            json = ChatMessageToJson(message.chat);
            json["type"] = MessageTypeName(message.type);
            break;
        case MessageType::ChatHistory:
            json["messages"] = nlohmann::json::array();
            for (const ChatMessage& chat : message.chatHistory.messages) {
                json["messages"].push_back(ChatMessageToJson(chat));
            }
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
                if (json.contains("enemies")) {
                    for (const auto& enemyJson : json.at("enemies")) {
                        message.joinAccepted.enemies.push_back(EnemyStateFromJson(enemyJson));
                    }
                }
                break;
            case MessageType::JoinRejected:
                message.joinRejected.reason = json.at("reason").get<std::string>();
                break;
            case MessageType::MoveRequest:
                message.moveRequest.col = json.at("col").get<int>();
                message.moveRequest.row = json.at("row").get<int>();
                break;
            case MessageType::AttackRequest:
                message.attackRequest.enemyId = json.at("enemy_id").get<int>();
                break;
            case MessageType::CancelCombatRequest:
                break;
            case MessageType::WorldState:
                message.worldState.tick = json.at("tick").get<uint32_t>();
                for (const auto& playerJson : json.at("players")) {
                    message.worldState.players.push_back(PlayerStateFromJson(playerJson));
                }
                if (json.contains("enemies")) {
                    for (const auto& enemyJson : json.at("enemies")) {
                        message.worldState.enemies.push_back(EnemyStateFromJson(enemyJson));
                    }
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
            case MessageType::Chat:
                message.chat = ChatMessageFromJson(json);
                break;
            case MessageType::ChatHistory:
                for (const auto& chatJson : json.at("messages")) {
                    message.chatHistory.messages.push_back(ChatMessageFromJson(chatJson));
                }
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
