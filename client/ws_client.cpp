#include "ws_client.hpp"

namespace net {

WsClient::~WsClient() {
    Disconnect();
}

bool WsClient::Connect(const std::string& url, MessageHandler onMessage) {
    Disconnect();
    onMessage_ = std::move(onMessage);

    webSocket_.setUrl(url);
    webSocket_.disableAutomaticReconnection();

    webSocket_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& message) {
        if (!message || message->type != ix::WebSocketMessageType::Message || !onMessage_) {
            return;
        }

        std::optional<Message> parsed = DeserializeMessage(message->str);
        if (parsed) {
            onMessage_(*parsed);
        }
    });

    webSocket_.start();
    return true;
}

void WsClient::Disconnect() {
    webSocket_.stop();
}

bool WsClient::IsConnected() const {
    return webSocket_.getReadyState() == ix::ReadyState::Open;
}

bool WsClient::Send(const Message& message) {
    if (!IsConnected()) {
        return false;
    }

    const std::string json = SerializeMessage(message);
    std::lock_guard<std::mutex> lock(sendMutex_);
    return webSocket_.sendText(json).success;
}

void WsClient::Poll() {
    // Desktop ixwebsocket runs its own background thread after start().
    // Web builds use async callbacks from the browser network stack.
}

}  // namespace net
