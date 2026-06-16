#include "game_server.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <thread>

#include "common/config.hpp"

namespace net {
namespace {

TransportKind TransportForClientId(int clientId) {
    return clientId >= 10000 ? TransportKind::WebSocket : TransportKind::Tcp;
}

std::string SanitizeChatText(const std::string& text) {
    std::string trimmed;
    trimmed.reserve(text.size());

    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }

    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    for (size_t i = start; i < end && trimmed.size() < kMaxChatLength; ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (ch >= 32 && ch <= 126) {
            trimmed.push_back(static_cast<char>(ch));
        }
    }

    return trimmed;
}

}  // namespace

bool GameServer::Start(uint16_t tcpPort, uint16_t wsPort) {
    auto onMessage = [this](int clientId, const Message& message) {
        EnqueueMessage(clientId, TransportForClientId(clientId), message);
    };

    auto onDisconnect = [this](int clientId) {
        EnqueueDisconnect(clientId, TransportForClientId(clientId));
    };

    if (!tcpListener_.Start(tcpPort, onMessage, onDisconnect)) {
        return false;
    }

    if (!wsListener_.Start(wsPort, onMessage, onDisconnect)) {
        tcpListener_.Stop();
        return false;
    }

    running_ = true;
    return true;
}

void GameServer::Stop() {
    running_ = false;
    tcpListener_.Stop();
    wsListener_.Stop();
    clients_.clear();
    transportByClientId_.clear();
    players_.clear();
    chatHistory_.clear();
}

void GameServer::Run() {
    using Clock = std::chrono::steady_clock;
    const auto tickDuration = std::chrono::duration<double>(kTickDuration);
    auto nextTick = Clock::now();

    while (running_) {
        ProcessMessages();
        ProcessDisconnects();

        const auto now = Clock::now();
        if (now >= nextTick) {
            SimulateTick();
            BroadcastWorldState();
            ++tick_;
            nextTick += std::chrono::duration_cast<Clock::duration>(tickDuration);
            if (now - nextTick > std::chrono::seconds(1)) {
                nextTick = now;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void GameServer::EnqueueMessage(int clientId, TransportKind transport, const Message& message) {
    std::lock_guard<std::mutex> lock(incomingMutex_);
    incoming_.push_back(IncomingMessage{clientId, transport, message});
}

void GameServer::EnqueueDisconnect(int clientId, TransportKind transport) {
    std::lock_guard<std::mutex> lock(incomingMutex_);
    pendingDisconnects_.push_back(PendingDisconnect{clientId, transport});
}

void GameServer::ProcessMessages() {
    std::deque<IncomingMessage> batch;
    {
        std::lock_guard<std::mutex> lock(incomingMutex_);
        batch.swap(incoming_);
    }

    for (const IncomingMessage& incoming : batch) {
        HandleMessage(incoming);
    }
}

void GameServer::ProcessDisconnects() {
    std::deque<PendingDisconnect> batch;
    {
        std::lock_guard<std::mutex> lock(incomingMutex_);
        batch.swap(pendingDisconnects_);
    }

    for (const PendingDisconnect& pending : batch) {
        HandleDisconnect(pending.clientId, pending.transport);
    }
}

void GameServer::HandleMessage(const IncomingMessage& incoming) {
    switch (incoming.message.type) {
        case MessageType::JoinRequest: {
            if (static_cast<int>(players_.size()) >= kMaxPlayers) {
                SendToClient(incoming.clientId, incoming.transport,
                             MakeJoinRejected("Server is full"));
                if (incoming.transport == TransportKind::Tcp) {
                    tcpListener_.DisconnectClient(incoming.clientId);
                }
                return;
            }

            ConnectedClient client;
            client.id = incoming.clientId;
            client.transport = incoming.transport;
            client.name = incoming.message.joinRequest.name;
            if (client.name.empty()) {
                client.name = "Player" + std::to_string(incoming.clientId);
            }
            client.hasJoined = true;
            clients_[incoming.clientId] = client;
            transportByClientId_[incoming.clientId] = incoming.transport;

            PlayerState player;
            player.id = incoming.clientId;
            player.name = client.name;
            player.x = kWorldWidth * 0.5f;
            player.y = kWorldHeight * 0.5f;
            players_.push_back(player);

            SendToClient(incoming.clientId, incoming.transport,
                         MakeJoinAccepted(incoming.clientId, BuildPlayerSnapshot()));
            if (!chatHistory_.empty()) {
                SendToClient(incoming.clientId, incoming.transport,
                             MakeChatHistory(chatHistory_));
            }
            std::cout << "[game] " << client.name << " joined as client "
                      << incoming.clientId << "\n";
            break;
        }
        case MessageType::PlayerInput: {
            auto it = clients_.find(incoming.clientId);
            if (it == clients_.end() || !it->second.hasJoined) {
                return;
            }
            it->second.input = incoming.message.playerInput;
            break;
        }
        case MessageType::Ping: {
            SendToClient(incoming.clientId, incoming.transport,
                         MakePong(incoming.message.ping.clientTimeMs, NowMs()));
            break;
        }
        case MessageType::Chat: {
            auto it = clients_.find(incoming.clientId);
            if (it == clients_.end() || !it->second.hasJoined) {
                return;
            }

            const std::string text = SanitizeChatText(incoming.message.chat.text);
            if (text.empty()) {
                return;
            }

            const ChatMessage entry{
                incoming.clientId,
                it->second.name,
                text,
            };
            RecordAndBroadcastChat(entry);
            std::cout << "[chat] " << it->second.name << ": " << text << "\n";
            break;
        }
        default:
            break;
    }
}

void GameServer::HandleDisconnect(int clientId, TransportKind transport) {
    (void)transport;
    if (clients_.find(clientId) == clients_.end() &&
        transportByClientId_.find(clientId) == transportByClientId_.end()) {
        return;
    }

    clients_.erase(clientId);
    transportByClientId_.erase(clientId);
    players_.erase(
        std::remove_if(players_.begin(), players_.end(),
                       [&](const PlayerState& player) { return player.id == clientId; }),
        players_.end());
    std::cout << "[game] client " << clientId << " removed\n";
}

void GameServer::SimulateTick() {
    for (PlayerState& player : players_) {
        auto clientIt = clients_.find(player.id);
        if (clientIt == clients_.end()) {
            continue;
        }

        const PlayerInput& input = clientIt->second.input;
        float dx = 0.0f;
        float dy = 0.0f;
        if (input.up) dy -= 1.0f;
        if (input.down) dy += 1.0f;
        if (input.left) dx -= 1.0f;
        if (input.right) dx += 1.0f;

        const float length = std::sqrt(dx * dx + dy * dy);
        if (length > 0.0f) {
            dx /= length;
            dy /= length;
        }

        player.x += dx * kPlayerSpeed * kTickDuration;
        player.y += dy * kPlayerSpeed * kTickDuration;

        const float margin = kPlayerRadius;
        player.x = std::clamp(player.x, margin, kWorldWidth - margin);
        player.y = std::clamp(player.y, margin, kWorldHeight - margin);
    }
}

void GameServer::BroadcastWorldState() {
    BroadcastToAll(MakeWorldState(tick_, players_));
}

void GameServer::RecordAndBroadcastChat(const ChatMessage& entry) {
    chatHistory_.push_back(entry);
    if (static_cast<int>(chatHistory_.size()) > kMaxChatHistory) {
        chatHistory_.erase(chatHistory_.begin());
    }
    BroadcastToAll(MakeChatBroadcast(entry.playerId, entry.name, entry.text));
}

void GameServer::BroadcastToAll(const Message& message) {
    std::vector<std::pair<int, TransportKind>> recipients;
    recipients.reserve(transportByClientId_.size());
    for (const auto& [clientId, transport] : transportByClientId_) {
        recipients.emplace_back(clientId, transport);
    }

    for (const auto& [clientId, transport] : recipients) {
        if (transportByClientId_.find(clientId) == transportByClientId_.end()) {
            continue;
        }
        if (!SendToClient(clientId, transport, message)) {
            EnqueueDisconnect(clientId, transport);
        }
    }
}

bool GameServer::SendToClient(int clientId, TransportKind transport, const Message& message) {
    if (transport == TransportKind::Tcp) {
        return tcpListener_.SendTo(clientId, message);
    }
    return wsListener_.SendTo(clientId, message);
}

std::vector<PlayerState> GameServer::BuildPlayerSnapshot() const {
    return players_;
}

uint32_t GameServer::NowMs() const {
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now().time_since_epoch();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

}  // namespace net
