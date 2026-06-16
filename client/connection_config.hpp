#pragma once

#include <cstdint>
#include <string>

namespace net {

struct DesktopEndpoint {
    std::string host;
    uint16_t port = 0;
};

// Builds ws:// or wss:// URL from WS_HOST, WS_PORT, WS_TLS env vars.
// Web builds fall back to the browser hostname when WS_HOST is unset.
std::string BuildWebSocketUrl();

// Desktop TCP endpoint from SERVER_HOST / SERVER_PORT env vars.
DesktopEndpoint GetDesktopEndpoint();

}  // namespace net
