#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#undef DrawText
#undef CloseWindow
#undef ShowCursor
#undef Rectangle
using SocketHandle = SOCKET;
inline constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
inline int CloseSocket(SocketHandle socket) { return closesocket(socket); }
inline int LastSocketError() { return WSAGetLastError(); }
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
inline constexpr SocketHandle kInvalidSocket = -1;
inline int CloseSocket(SocketHandle socket) { return close(socket); }
inline int LastSocketError() { return errno; }
#endif

#include <cstddef>
#include <cstdint>
#include <string>

namespace net {

bool InitSockets();
void ShutdownSockets();

bool SetSocketNonBlocking(SocketHandle socket);
bool SetSocketReuseAddr(SocketHandle socket);
bool SendAll(SocketHandle socket, const uint8_t* data, size_t size);
bool RecvSome(SocketHandle socket, std::string& buffer);

}  // namespace net
