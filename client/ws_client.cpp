#include "ws_client.hpp"

namespace net {

WsClient::~WsClient() {
    Disconnect();
}

bool WsClient::Connect(const std::string& url, OpenHandler onOpen, ErrorHandler onError) {
    Disconnect();
    onOpen_ = std::move(onOpen);
    onError_ = std::move(onError);

    webSocket_.setUrl(url);
    webSocket_.disableAutomaticReconnection();

    webSocket_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& message) {
        if (!message) {
            return;
        }

        if (message->type == ix::WebSocketMessageType::Open) {
            connected_ = true;
            pendingOpen_ = true;
            return;
        }

        if (message->type == ix::WebSocketMessageType::Close) {
            connected_ = false;
            connectionLost_ = true;
            EnqueueError("WebSocket closed");
            return;
        }

        if (message->type == ix::WebSocketMessageType::Error) {
            connected_ = false;
            connectionLost_ = true;
            EnqueueError(message->errorInfo.reason.empty() ? "WebSocket error"
                                                           : message->errorInfo.reason);
            return;
        }

        if (message->type != ix::WebSocketMessageType::Message) {
            return;
        }

        std::optional<Message> parsed = DeserializeMessage(message->str);
        if (parsed) {
            EnqueueMessage(*parsed);
        }
    });

    webSocket_.start();
    return true;
}

void WsClient::Disconnect() {
    connected_ = false;
    connectionLost_ = false;
    pendingOpen_ = false;
    webSocket_.stop();
    {
        std::lock_guard<std::mutex> lock(incomingMutex_);
        incoming_.clear();
        pendingError_.clear();
    }
}

bool WsClient::IsConnected() const {
    return connected_ && webSocket_.getReadyState() == ix::ReadyState::Open;
}

bool WsClient::Send(const Message& message) {
    if (!IsConnected()) {
        return false;
    }

    const std::string json = SerializeMessage(message);
    std::lock_guard<std::mutex> lock(sendMutex_);
    return webSocket_.sendText(json).success;
}

void WsClient::Poll(std::function<void(const Message&)> onMessage) {
    if (pendingOpen_.exchange(false) && onOpen_) {
        onOpen_();
    }

    if (!pendingError_.empty()) {
        std::string error;
        {
            std::lock_guard<std::mutex> lock(incomingMutex_);
            error.swap(pendingError_);
        }
        if (onError_) {
            onError_(error);
        }
    }

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

bool WsClient::ConsumeConnectionLost() {
    return connectionLost_.exchange(false);
}

void WsClient::EnqueueMessage(const Message& message) {
    std::lock_guard<std::mutex> lock(incomingMutex_);
    incoming_.push_back(message);
}

void WsClient::EnqueueError(const std::string& reason) {
    std::lock_guard<std::mutex> lock(incomingMutex_);
    pendingError_ = reason;
}

}  // namespace net
