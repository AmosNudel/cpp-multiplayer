#include <csignal>
#include <iostream>

#ifndef _WIN32
#include <signal.h>
#endif

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
    const net::ServerPorts ports = net::ResolveServerPorts();

    std::cout << "=== Multiplayer Game Server ===\n";
    std::cout << "Authoritative host (headless)\n";
    std::cout << "TCP port: " << ports.tcp << " (from " << ports.tcpSource << ")\n";
    std::cout << "WS  port: " << ports.ws << " (from " << ports.wsSource << ")\n";

    if (ports.tcp == ports.ws) {
        std::cerr << "\nError: TCP and WebSocket cannot use the same port ("
                  << ports.tcp << ").\n";
        std::cerr << "On Railway with TCP proxy enabled, PORT is often 7777.\n";
        std::cerr << "Set WS_PORT to your domain target port (e.g. 8080):\n";
        std::cerr << "  TCP_PORT=7777\n";
        std::cerr << "  WS_PORT=8080\n";
        return 1;
    }

    net::GameServer server;
    gServer = &server;
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif

    if (!server.Start(ports.tcp, ports.ws)) {
        std::cerr << "Failed to start server.\n";
        return 1;
    }

    server.Run();
    std::cout << "Server stopped.\n";
    return 0;
}
