#include "tcp_client.hpp"

#include <functional>
#include <vector>

namespace net {

TcpClient::~TcpClient() {
    Disconnect();
}

bool TcpClient::Connect(const std::string& host, uint16_t port) {
    Disconnect();

    if (!InitSockets()) {
        return false;
    }

    socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == kInvalidSocket) {
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* result = nullptr;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result) {
            CloseSocket(socket_);
            socket_ = kInvalidSocket;
            return false;
        }
        address.sin_addr = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr;
        freeaddrinfo(result);
    }

    if (::connect(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        CloseSocket(socket_);
        socket_ = kInvalidSocket;
        return false;
    }

    connected_ = true;
    running_ = true;
    receiveThread_ = std::thread(&TcpClient::ReceiveLoop, this);
    return true;
}

void TcpClient::Disconnect() {
    running_ = false;
    connected_ = false;
    connectionLost_ = false;

    if (socket_ != kInvalidSocket) {
        CloseSocket(socket_);
        socket_ = kInvalidSocket;
    }

    if (receiveThread_.joinable()) {
        receiveThread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(incomingMutex_);
        incoming_.clear();
    }

    ShutdownSockets();
}

bool TcpClient::Send(const Message& message) {
    if (!connected_) {
        return false;
    }

    const std::string json = SerializeMessage(message);
    const std::vector<uint8_t> frame = FrameTcpMessage(json);

    std::lock_guard<std::mutex> lock(sendMutex_);
    return SendAll(socket_, frame.data(), frame.size());
}

void TcpClient::Poll(std::function<void(const Message&)> onMessage) {
    std::deque<Message> batch;
    {
        std::lock_guard<std::mutex> lock(incomingMutex_);
        batch.swap(incoming_);
    }

    for (const Message& message : batch) {
        if (onMessage) {
            onMessage(message);
        }
    }
}

bool TcpClient::ConsumeConnectionLost() {
    return connectionLost_.exchange(false);
}

void TcpClient::EnqueueMessage(const Message& message) {
    std::lock_guard<std::mutex> lock(incomingMutex_);
    incoming_.push_back(message);
}

void TcpClient::ReceiveLoop() {
    while (running_ && connected_) {
        if (!RecvSome(socket_, receiveBuffer_)) {
            connected_ = false;
            connectionLost_ = true;
            break;
        }

        while (true) {
            std::optional<std::string> json = TryExtractTcpMessage(receiveBuffer_);
            if (!json) {
                break;
            }

            std::optional<Message> message = DeserializeMessage(*json);
            if (message) {
                EnqueueMessage(*message);
            }
        }
    }

    if (running_) {
        connectionLost_ = true;
    }
}

}  // namespace net
