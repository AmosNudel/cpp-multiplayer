#pragma once

#include <cstdint>
#include <string>

namespace net {

struct DesktopEndpoint {
    std::string host;
    uint16_t port = 0;
};

// Builds ws:// or wss:// URL from WS_HOST, WS_PORT, WS_TLS env vars.
// Production web builds use WS_HOST_DEFAULT / WS_PORT_DEFAULT (Railway).
// Local-only web builds (no WS_HOST_DEFAULT) fall back to ws://localhost:7778.
std::string BuildWebSocketUrl();

// Desktop TCP endpoint from SERVER_HOST / SERVER_PORT env vars.
DesktopEndpoint GetDesktopEndpoint();

}  // namespace net
