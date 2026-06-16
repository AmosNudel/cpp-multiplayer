#include "tcp_listener.hpp"

#include <iostream>
#include <memory>

namespace net {

TcpListener::~TcpListener() {
    Stop();
}

bool TcpListener::Start(uint16_t port, MessageHandler onMessage, DisconnectHandler onDisconnect) {
    if (running_) {
        return true;
    }

    if (!InitSockets()) {
        std::cerr << "[tcp] failed to initialize sockets\n";
        return false;
    }

    onMessage_ = std::move(onMessage);
    onDisconnect_ = std::move(onDisconnect);

    listenSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket_ == kInvalidSocket) {
        std::cerr << "[tcp] failed to create listen socket\n";
        return false;
    }

    if (!SetSocketReuseAddr(listenSocket_)) {
        std::cerr << "[tcp] failed to set SO_REUSEADDR\n";
        CloseSocket(listenSocket_);
        listenSocket_ = kInvalidSocket;
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(listenSocket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        std::cerr << "[tcp] failed to bind port " << port << "\n";
        CloseSocket(listenSocket_);
        listenSocket_ = kInvalidSocket;
        return false;
    }

    if (listen(listenSocket_, SOMAXCONN) != 0) {
        std::cerr << "[tcp] failed to listen on port " << port << "\n";
        CloseSocket(listenSocket_);
        listenSocket_ = kInvalidSocket;
        return false;
    }

    running_ = true;
    acceptThread_ = std::thread(&TcpListener::AcceptLoop, this);
    std::cout << "[tcp] listening on port " << port << "\n";
    return true;
}

void TcpListener::Stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    if (listenSocket_ != kInvalidSocket) {
        CloseSocket(listenSocket_);
        listenSocket_ = kInvalidSocket;
    }

    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }

    std::vector<std::shared_ptr<Client>> clientsCopy;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clientsCopy = clients_;
        clients_.clear();
    }

    for (const auto& client : clientsCopy) {
        client->alive = false;
        if (client->socket != kInvalidSocket) {
            CloseSocket(client->socket);
        }
        if (client->thread.joinable()) {
            client->thread.join();
        }
    }

    ShutdownSockets();
}

bool TcpListener::SendTo(int clientId, const Message& message) {
    std::shared_ptr<Client> client;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (const auto& entry : clients_) {
            if (entry->id == clientId) {
                client = entry;
                break;
            }
        }
    }

    if (!client || !client->alive) {
        return false;
    }

    const std::string json = SerializeMessage(message);
    const std::vector<uint8_t> frame = FrameTcpMessage(json);
    return SendAll(client->socket, frame.data(), frame.size());
}

void TcpListener::AcceptLoop() {
    while (running_) {
        sockaddr_in clientAddress{};
        socklen_t clientLength = sizeof(clientAddress);
        SocketHandle clientSocket = accept(
            listenSocket_,
            reinterpret_cast<sockaddr*>(&clientAddress),
            &clientLength);

        if (clientSocket == kInvalidSocket) {
            if (!running_) {
                break;
            }
            continue;
        }

        auto client = std::make_shared<Client>();
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            client->id = nextClientId_++;
            client->socket = clientSocket;
            clients_.push_back(client);
        }

        std::cout << "[tcp] client " << client->id << " connected\n";
        client->thread = std::thread(&TcpListener::ClientLoop, this, client);
    }
}

void TcpListener::ClientLoop(const std::shared_ptr<Client>& client) {
    while (client->alive && running_) {
        if (!RecvSome(client->socket, client->receiveBuffer)) {
            break;
        }

        while (true) {
            std::optional<std::string> json = TryExtractTcpMessage(client->receiveBuffer);
            if (!json) {
                break;
            }

            std::optional<Message> message = DeserializeMessage(*json);
            if (!message) {
                continue;
            }

            if (onMessage_) {
                onMessage_(client->id, *message);
            }
        }
    }

    client->alive = false;
    if (client->socket != kInvalidSocket) {
        CloseSocket(client->socket);
        client->socket = kInvalidSocket;
    }

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_.erase(
            std::remove_if(clients_.begin(), clients_.end(),
                           [&](const std::shared_ptr<Client>& entry) {
                               return entry->id == client->id;
                           }),
            clients_.end());
    }

    std::cout << "[tcp] client " << client->id << " disconnected\n";
    if (onDisconnect_) {
        onDisconnect_(client->id);
    }
}

}  // namespace net
