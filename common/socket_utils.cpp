#include "socket_utils.hpp"

#include <cerrno>
#include <cstring>

namespace net {

bool InitSockets() {
#ifdef _WIN32
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#else
    return true;
#endif
}

void ShutdownSockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}

bool SetSocketNonBlocking(SocketHandle socket) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }
    return fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool SetSocketReuseAddr(SocketHandle socket) {
    int enabled = 1;
    return setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,
                      reinterpret_cast<const char*>(&enabled), sizeof(enabled)) == 0;
}

bool SendAll(SocketHandle socket, const uint8_t* data, size_t size) {
    size_t sentTotal = 0;
    while (sentTotal < size) {
        const int sent = send(socket,
                              reinterpret_cast<const char*>(data + sentTotal),
                              static_cast<int>(size - sentTotal),
                              0);
        if (sent <= 0) {
            return false;
        }
        sentTotal += static_cast<size_t>(sent);
    }
    return true;
}

bool RecvSome(SocketHandle socket, std::string& buffer) {
    char chunk[4096];
    const int received = recv(socket, chunk, sizeof(chunk), 0);
    if (received > 0) {
        buffer.append(chunk, static_cast<size_t>(received));
        return true;
    }
    if (received == 0) {
        return false;
    }
#ifdef _WIN32
    const int err = WSAGetLastError();
    return err == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

}  // namespace net
