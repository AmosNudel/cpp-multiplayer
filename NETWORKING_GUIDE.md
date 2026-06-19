# Multiplayer Networking & Build Guide

This guide explains how the multiplayer architecture works, what each package does, how to build every target, and how to use networking while developing your raylib game — written for someone new to C++.

---

## Table of Contents

1. [Architecture overview](#1-architecture-overview)
2. [Packages and why we use them](#2-packages-and-why-we-use-them)
3. [Project layout](#3-project-layout)
4. [The message protocol](#4-the-message-protocol)
5. [How to build everything](#5-how-to-build-everything)
6. [Running locally (step by step)](#6-running-locally-step-by-step)
7. [Deploying the server to Railway](#7-deploying-the-server-to-railway)
8. [Using networking in your game code](#8-using-networking-in-your-game-code)
9. [Adding new networked features](#9-adding-new-networked-features)
10. [Glossary](#10-glossary)

---

## 1. Architecture overview

```
┌─────────────────┐     TCP (port 7777)      ┌──────────────────────────┐
│  Desktop Client │ ───────────────────────► │                          │
│  (raylib + C++) │                            │  Authoritative Server    │
└─────────────────┘                            │  (headless, no graphics) │
                                               │  Linux container/Railway │
┌─────────────────┐     WebSocket (port 7778)  │                          │
│   Web Client    │ ───────────────────────► │                          │
│ (raylib + WASM) │                            └──────────────────────────┘
└─────────────────┘
```

### Key ideas

| Concept | What it means here |
|---|---|
| **Authoritative host** | The server owns the real game state. Clients only send *inputs* (WASD). The server moves players and broadcasts the result. Clients never decide their own position. |
| **Headless server** | `game_server` has no window and no raylib graphics. It only runs networking + game logic. This keeps it small and cheap to host. |
| **TCP (desktop)** | Reliable, ordered byte stream. Good for native Windows/Linux/Mac clients. |
| **WebSocket (web)** | Browser games cannot open raw TCP sockets to arbitrary hosts. WebSocket is the browser-friendly alternative with the same JSON messages. |
| **Dedicated server** | One server process hosts the game for everyone. Not peer-to-peer. |

### Data flow (one frame)

```
1. Client reads keyboard  →  sends PlayerInput message
2. Server receives input  →  stores it on that player's record
3. Server tick (20/sec)   →  moves all players based on stored inputs
4. Server broadcasts      →  WorldState message to every connected client
5. Client receives state  →  draws circles at server-provided positions
```

---

## 2. Packages and why we use them

### Build tools

| Package | Role | Why this one |
|---|---|---|
| **CMake** | Build system | One config builds the server, desktop client, and web client. Fetches dependencies automatically. Industry standard for C++. |
| **MinGW / w64devkit** | Windows C++ compiler | Already used by raylib templates. Compiles your game on Windows. |
| **Emscripten (emsdk)** | WebAssembly compiler | Turns C++ into `.wasm` + `.html` so the game runs in a browser. |
| **Docker** | Linux container image | Packages the server for Railway. Same binary runs locally and in the cloud. |
| **Inno Setup 6** | Windows installer | Creates `MultiplayerGame_Setup.exe` — a wizard users click through to install your game. Free and widely used. |

### Networking libraries

| Package | Role | Why this one |
|---|---|---|
| **BSD sockets** (`socket`, `connect`, `send`) | TCP on desktop + server | Built into the OS. No extra install. Simple to learn. Used in `tcp_listener.cpp` and `tcp_client.cpp`. |
| **IXWebSocket** | WebSocket client + server | Cross-platform, works on desktop, Linux server, *and* Emscripten (browser). Single library for both web transport ends. Fetched by CMake. |
| **nlohmann/json** | Message serialization | Converts C++ structs ↔ JSON strings. Easy to read in logs and debug. Beginner-friendly compared to binary protocols. Fetched by CMake. |

### Game library

| Package | Role | Why this one |
|---|---|---|
| **raylib** | Graphics, input, window | Your game renderer. Only linked by `game_client`, not the server. |

### What we deliberately did *not* add

- **Boost.Asio** — powerful but heavy for a starter template.
- **ENet / UDP** — faster for action games, but harder to debug; TCP/WS is simpler to start with.
- **Protobuf** — efficient binary format, but harder for beginners; JSON is human-readable.

You can swap these later as your game grows.

---

## 3. Project layout

```
multiplayer/
├── common/                  # Shared between client and server
│   ├── config.hpp           # Ports, tick rate, world size
│   ├── protocol.hpp/.cpp    # Message types + JSON encode/decode
│   └── socket_utils.hpp/.cpp# Cross-platform TCP helpers
├── server/                  # Headless authoritative host
│   ├── main.cpp             # Entry point
│   ├── game_server.hpp/.cpp # Game simulation + message routing
│   ├── tcp_listener.*       # Accepts desktop TCP clients
│   └── ws_listener.*        # Accepts browser WebSocket clients
├── client/                  # raylib game with networking
│   ├── main.cpp             # Window, drawing, input
│   ├── game_client.hpp/.cpp # High-level connect/send/receive API
│   ├── tcp_client.*         # Desktop transport
│   └── ws_client.*          # Web transport
├── tools/                   # Build scripts (PowerShell)
├── docker/Dockerfile        # Railway / Linux container
├── railway.toml             # Railway deployment config
└── CMakeLists.txt           # Master build file
```

### Which binary does what

| Binary | Built from | Runs where | Uses raylib? |
|---|---|---|---|
| `game_server` | `server/` | Linux container, Windows dev | No |
| `game_client` | `client/` | Windows desktop | Yes |
| `game_client.html` | `client/` (Emscripten) | Browser | Yes |

---

## 4. The message protocol

All messages are JSON. Example:

```json
{"type":"player_input","up":true,"down":false,"left":false,"right":false}
```

### Message types (defined in `common/protocol.hpp`)

| Type | Direction | Purpose |
|---|---|---|
| `join_request` | Client → Server | "I want to play as this name" |
| `join_accepted` | Server → Client | "You're player 3, here's everyone" |
| `join_rejected` | Server → Client | "Server full" or other error |
| `player_input` | Client → Server | WASD state |
| `world_state` | Server → All | Positions of every player (every tick) |
| `player_left` | Server → All | Someone disconnected |
| `ping` / `pong` | Both | Latency measurement |

### Framing: TCP vs WebSocket

- **TCP** messages are prefixed with a 4-byte length header (see `FrameTcpMessage`). This tells the receiver where one message ends and the next begins — TCP is a stream, not discrete packets.
- **WebSocket** messages are sent as raw JSON. The WebSocket protocol already frames messages.

---

## 5. How to build everything

### Prerequisites (one-time)

1. **raylib + w64devkit** — already at `C:\raylib\` if you use the standard template.
2. **CMake** — https://cmake.org/download/ (check "Add to PATH" during install).
3. **Git** — CMake downloads dependencies via Git.
4. **Emscripten** (web only) — see `tools/build_web.ps1` for setup commands.
5. **Inno Setup 6** (installer only) — https://jrsoftware.org/isdl.php

### Desktop client + local server

```powershell
cd C:\Users\nudel\Desktop\cpp\multiplayer
.\tools\build.ps1
```

Outputs:
- `build\game_client.exe`
- `build\game_server.exe`

### Server only

```powershell
.\tools\build.ps1 -ServerOnly
```

### Release folder (zip and share)

```powershell
.\tools\build_release.ps1
# Creates dist\game_client.exe
```

### Windows installer

```powershell
.\tools\build_installer.ps1
# Creates installer\MultiplayerGame_Setup.exe
```

Requires Inno Setup 6. If missing, you still get `dist\` to zip manually.

### Web (browser) build

```powershell
.\tools\build_web.ps1
# Creates build-web\game_client.html (+ .js, .wasm)
```

### VS Code shortcuts

| Task | What it does |
|---|---|
| `cmake: build debug` (Ctrl+Shift+B) | Debug client + server |
| `cmake: build server` | Server only |
| `build installer` | Release + Inno Setup wizard |
| `build web` | Emscripten web client |

### Docker (server for Railway)

```powershell
docker build -f docker/Dockerfile -t multiplayer-server .
docker run -p 7777:7777 -p 7778:7778 multiplayer-server
```

---

## 6. Running locally (step by step)

This is the fastest way to verify everything works.

### Terminal 1 — start the server

```powershell
cd C:\Users\nudel\Desktop\cpp\multiplayer
.\build\game_server.exe
```

You should see:

```
=== Multiplayer Game Server ===
[tcp] listening on port 7777
[ws]  listening on port 7778
```

### Terminal 2 — start a client

```powershell
$env:SERVER_HOST = "127.0.0.1"
$env:SERVER_PORT = "7777"
.\build\game_client.exe
```

### Terminal 3 — second player (optional)

Run another `game_client.exe`. Type a different name, press Enter, use WASD.

### What to expect

- Each player is a colored circle.
- Movement may feel slightly delayed — that's normal. The server runs at 20 ticks/sec; clients display the latest state they received.
- Ping is shown in the top-left after connecting.

---

## 7. Deploying the server to Railway

### 1. Push this repo to GitHub

Railway deploys from a Git repository.

### 2. Create a Railway project

1. Go to https://railway.app → New Project → Deploy from GitHub repo.
2. Select this repository.
3. Railway detects `railway.toml` and `docker/Dockerfile`.

### 3. Set environment variables

In the Railway service settings:

| Variable | Value | Purpose |
|---|---|---|
| `PORT` | `8080` | **Required** when TCP proxy is on — Railway routes public HTTPS/WSS here |
| `TCP_PORT` | `7777` | Desktop clients (enable TCP proxy in Railway networking) |

> **Important:** With TCP proxy enabled, Railway auto-sets `PORT=7777`. That sends browser traffic to the raw TCP listener → 502 / connection reset. You must override with `PORT=8080`. Do not set `WS_PORT=8080` alone; it does not change Railway routing.

> **Note:** Railway's default public URL works for WebSocket (`wss://your-app.up.railway.app`). For desktop TCP, enable Railway's TCP proxy feature and point clients at that host/port.

### 4. Connect clients to production

**Web client** — set before loading:

```
WS_HOST=your-app.up.railway.app
WS_PORT=443   (or Railway's assigned port)
```

Use `wss://` in production (TLS). Update `ConnectWeb` URL in `client/main.cpp` when you deploy for real.

**Desktop client:**

```powershell
$env:SERVER_HOST = "your-tcp-proxy.up.railway.app"
$env:SERVER_PORT = "7777"
.\game_client.exe
```

---

## 8. Using networking in your game code

As a game developer, you mostly interact with **`GameClient`** (client) and **`GameServer`** (server). You rarely touch sockets directly.

### Client side (in your raylib game)

The template `client/main.cpp` shows the pattern:

```cpp
#include "client/game_client.hpp"

net::GameClient gClient;

// 1. Connect once
gClient.ConnectDesktop("127.0.0.1", 7777, "Alice", OnConnectionState);

// 2. Each frame — pump network + send input
gClient.Update();
gClient.SendInput(input);

// 3. Read authoritative state for rendering
for (const net::PlayerState& player : gClient.GetPlayers()) {
    DrawCircle(player.x, player.y, 16, RED);
}
```

**Rules for client code:**

1. **Never** change `player.x` / `player.y` locally based on keyboard. Send input; draw what the server sends back.
2. Call `gClient.Update()` every frame so WebSocket messages are processed.
3. Only call `SendInput` when connected (`GetState() == ClientConnectionState::Joined`).

### Server side (authoritative logic)

Game rules live in `server/game_server.cpp`:

| Function | Your job when extending |
|---|---|
| `HandleMessage` | React to new message types (e.g. shooting, chat) |
| `SimulateTick` | Update game state — movement, collisions, scoring |
| `BroadcastWorldState` | Already sends positions; extend `PlayerState` for more data |

**Rules for server code:**

1. **Validate everything** clients send. Never trust client position.
2. Keep simulation in `SimulateTick` at fixed `kTickRate` (20 Hz). Don't tie logic to frame rate.
3. The server does not include raylib. No `DrawCircle` on the server — only math and state.

---

## 9. Adding new networked features

Example: add a "chat" message.

### Step 1 — Add the type in `common/protocol.hpp`

```cpp
enum class MessageType {
    // ... existing ...
    Chat,
};

struct ChatMessage {
    std::string text;
};
```

Add serialize/deserialize cases in `protocol.cpp`.

### Step 2 — Handle it on the server

In `GameServer::HandleMessage`:

```cpp
case MessageType::Chat:
    // validate, then broadcast to all clients
    break;
```

### Step 3 — Send it from the client

```cpp
net::Message msg;
msg.type = net::MessageType::Chat;
msg.chat.text = "hello";
// use tcpClient_.Send or add a helper on GameClient
```

### Step 4 — Rebuild both targets

```powershell
.\tools\build.ps1
```

Always change `common/protocol` first — both client and server share it.

---

## 10. Glossary

| Term | Meaning |
|---|---|
| **Authoritative** | Server is the source of truth for game state |
| **Client** | Player's game (raylib window) |
| **Tick** | One server simulation step (50 ms at 20 Hz) |
| **Transport** | How bytes move (TCP or WebSocket) |
| **Framing** | How we know where one message ends (length prefix or WS frame) |
| **Headless** | Program with no graphical window |
| **WASM** | WebAssembly — compiled C++ running in the browser |
| **Latency / ping** | Round-trip time to the server in milliseconds |

---

## Quick reference card

```powershell
# Build everything
.\tools\build.ps1

# Run server
.\build\game_server.exe

# Run client (local)
$env:SERVER_HOST="127.0.0.1"; $env:SERVER_PORT="7777"; .\build\game_client.exe

# Release zip
.\tools\build_release.ps1

# Installer
.\tools\build_installer.ps1

# Web build
.\tools\build_web.ps1

# Docker server
docker build -f docker/Dockerfile -t multiplayer-server .
```

**Client API:** `GameClient::ConnectDesktop` → `Update` → `SendInput` → `GetPlayers`

**Server logic:** `GameServer::HandleMessage` + `SimulateTick` in `server/game_server.cpp`

**Shared protocol:** `common/protocol.hpp`
