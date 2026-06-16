#include <csignal>
#include <iostream>

#include "common/config.hpp"
#include "server/game_server.hpp"

namespace {
net::GameServer* gServer = nullptr;

void HandleSignal(int) {
    if (gServer) {
        gServer->Stop();
    }
}
}  // namespace

int main() {
    const uint16_t tcpPort = net::EnvPort("TCP_PORT", net::kDefaultTcpPort);
    const uint16_t wsPort = net::EnvPort("WS_PORT", net::EnvPort("PORT", net::kDefaultWsPort));

    std::cout << "=== Multiplayer Game Server ===\n";
    std::cout << "Authoritative host (headless)\n";
    std::cout << "TCP port: " << tcpPort << " (desktop clients)\n";
    std::cout << "WS  port: " << wsPort << " (web clients)\n";

    net::GameServer server;
    gServer = &server;
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    if (!server.Start(tcpPort, wsPort)) {
        std::cerr << "Failed to start server.\n";
        return 1;
    }

    server.Run();
    std::cout << "Server stopped.\n";
    return 0;
}
