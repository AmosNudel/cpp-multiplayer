#include <csignal>
#include <cstdlib>
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

void LogEnv(const char* name) {
    const char* value = std::getenv(name);
    std::cout << "  " << name << "=" << (value ? value : "(unset)") << '\n';
}
}  // namespace

int main() {
    std::cout.setf(std::ios::unitbuf);

    std::cout << "=== Multiplayer Game Server ===\n";
    std::cout << "Environment:\n";
    LogEnv("PORT");
    LogEnv("TCP_PORT");
    LogEnv("WS_PORT");
    LogEnv("RAILWAY_TCP_APPLICATION_PORT");
    LogEnv("RAILWAY_PUBLIC_DOMAIN");

    const net::ServerPorts ports = net::ResolveServerPorts();

    std::cout << "Authoritative host (headless)\n";
    std::cout << "TCP port: " << ports.tcp << " (from " << ports.tcpSource << ")\n";
    std::cout << "WS  port: " << ports.ws << " (from " << ports.wsSource << ")\n";

    if (ports.tcp == ports.ws) {
        std::cerr << "\nError: TCP and WebSocket cannot use the same port ("
                  << ports.tcp << ").\n";
        std::cerr << "On Railway with TCP proxy enabled, PORT is often auto-set to 7777.\n";
        std::cerr << "Add this variable in Railway to fix public HTTP/WSS routing:\n";
        std::cerr << "  PORT=8080\n";
        std::cerr << "Keep TCP on its own port:\n";
        std::cerr << "  TCP_PORT=7777\n";
        return 1;
    }

    if (ports.wsSource != "PORT") {
        std::cerr << "\nWarning: WebSocket is not listening on PORT.\n";
        std::cerr << "Railway routes public HTTPS/WSS to PORT, not WS_PORT.\n";
        std::cerr << "Set PORT=8080 in Railway (and remove WS_PORT if redundant).\n\n";
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
