#include "ws_client.hpp"

#if defined(PLATFORM_WEB)

#include <emscripten.h>

#include <optional>

namespace net {
namespace {

WsClient* gActiveWsClient = nullptr;
int gNextSocketId = 1;

EM_JS(void, JsWebSocketConnect, (int socketId, const char* url), {
    const id = socketId;
    const wsUrl = UTF8ToString(url);
    if (!Module._gameWs) {
        Module._gameWs = {};
    }

    const existing = Module._gameWs[id];
    if (existing) {
        existing.onopen = null;
        existing.onmessage = null;
        existing.onerror = null;
        existing.onclose = null;
        existing.close();
        delete Module._gameWs[id];
    }

    const ws = new WebSocket(wsUrl);
    Module._gameWs[id] = ws;

    ws.onopen = () => {
        Module.ccall('NetWsOnOpen', null, ['number'], [id]);
    };

    ws.onmessage = (event) => {
        const json = typeof event.data === 'string' ? event.data : "";
        const len = lengthBytesUTF8(json) + 1;
        const ptr = _malloc(len);
        stringToUTF8(json, ptr, len);
        Module.ccall('NetWsOnMessage', null, ['number', 'number'], [id, ptr]);
        _free(ptr);
    };

    ws.onerror = () => {
        Module.ccall('NetWsOnError', null, ['number'], [id]);
    };

    ws.onclose = () => {
        Module.ccall('NetWsOnClose', null, ['number'], [id]);
    };
});

EM_JS(void, JsWebSocketDisconnect, (int socketId), {
    if (!Module._gameWs) {
        return;
    }

    const ws = Module._gameWs[socketId];
    if (!ws) {
        return;
    }

    ws.onopen = null;
    ws.onmessage = null;
    ws.onerror = null;
    ws.onclose = null;
    ws.close();
    delete Module._gameWs[socketId];
});

EM_JS(int, JsWebSocketSend, (int socketId, const char* text), {
    if (!Module._gameWs) {
        return 0;
    }

    const ws = Module._gameWs[socketId];
    if (!ws || ws.readyState !== WebSocket.OPEN) {
        return 0;
    }

    ws.send(UTF8ToString(text));
    return 1;
});

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE
void NetWsOnOpen(int socketId) {
    if (gActiveWsClient != nullptr && gActiveWsClient->MatchesSocket(socketId)) {
        gActiveWsClient->HandleBrowserOpen();
    }
}

EMSCRIPTEN_KEEPALIVE
void NetWsOnMessage(int socketId, const char* json) {
    if (gActiveWsClient != nullptr && gActiveWsClient->MatchesSocket(socketId) && json != nullptr) {
        gActiveWsClient->HandleBrowserMessage(json);
    }
}

EMSCRIPTEN_KEEPALIVE
void NetWsOnError(int socketId) {
    if (gActiveWsClient != nullptr && gActiveWsClient->MatchesSocket(socketId)) {
        gActiveWsClient->HandleBrowserError("WebSocket error");
    }
}

EMSCRIPTEN_KEEPALIVE
void NetWsOnClose(int socketId) {
    if (gActiveWsClient != nullptr && gActiveWsClient->MatchesSocket(socketId)) {
        gActiveWsClient->HandleBrowserClose();
    }
}

}  // extern "C"

WsClient::~WsClient() {
    Disconnect();
}

bool WsClient::Connect(const std::string& url, OpenHandler onOpen, ErrorHandler onError) {
    Disconnect();
    onOpen_ = std::move(onOpen);
    onError_ = std::move(onError);

    socketId_ = gNextSocketId++;
    gActiveWsClient = this;
    JsWebSocketConnect(socketId_, url.c_str());
    return true;
}

void WsClient::Disconnect() {
    if (socketId_ != 0) {
        JsWebSocketDisconnect(socketId_);
    }

    if (gActiveWsClient == this) {
        gActiveWsClient = nullptr;
    }

    connected_ = false;
    connectionLost_ = false;
    pendingOpen_ = false;
    socketId_ = 0;

    {
        std::lock_guard<std::mutex> lock(incomingMutex_);
        incoming_.clear();
        pendingError_.clear();
    }
}

bool WsClient::IsConnected() const {
    return connected_;
}

bool WsClient::Send(const Message& message) {
    if (!IsConnected() || socketId_ == 0) {
        return false;
    }

    const std::string json = SerializeMessage(message);
    std::lock_guard<std::mutex> lock(sendMutex_);
    return JsWebSocketSend(socketId_, json.c_str()) != 0;
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

void WsClient::HandleBrowserOpen() {
    connected_ = true;
    pendingOpen_ = true;
}

void WsClient::HandleBrowserMessage(const std::string& json) {
    std::optional<Message> parsed = DeserializeMessage(json);
    if (parsed) {
        EnqueueMessage(*parsed);
    }
}

void WsClient::HandleBrowserError(const std::string& reason) {
    connected_ = false;
    connectionLost_ = true;
    EnqueueError(reason);
}

void WsClient::HandleBrowserClose() {
    connected_ = false;
    connectionLost_ = true;
    EnqueueError("WebSocket closed");
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

#endif  // PLATFORM_WEB
