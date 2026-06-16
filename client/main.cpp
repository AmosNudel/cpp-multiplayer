#include "raylib.h"

#include <cstdio>
#include <string>

#include "client/game_client.hpp"
#include "common/config.hpp"

#if defined(PLATFORM_WEB)
#include <emscripten.h>
#endif

static const int screenWidth = 960;
static const int screenHeight = 640;

static net::GameClient gClient;
static std::string gStatusText = "Press ENTER to connect";
static std::string gPlayerName = "Player";
static bool gEditingName = true;
static Color gLocalColor = RAYWHITE;

static Color ColorForPlayer(int playerId) {
    static const Color palette[] = {
        {230, 80, 80, 255},
        {80, 180, 255, 255},
        {120, 220, 120, 255},
        {255, 200, 80, 255},
        {200, 120, 255, 255},
        {80, 220, 200, 255},
        {255, 140, 180, 255},
        {180, 180, 180, 255},
    };
    return palette[playerId % 8];
}

static void OnConnectionState(net::ClientConnectionState state, const std::string& detail) {
    switch (state) {
        case net::ClientConnectionState::Disconnected:
            gStatusText = detail.empty() ? "Disconnected" : detail;
            gEditingName = true;
            break;
        case net::ClientConnectionState::Connecting:
            gStatusText = detail;
            break;
        case net::ClientConnectionState::Joined:
            gStatusText = "Connected - WASD to move";
            gEditingName = false;
            gLocalColor = ColorForPlayer(gClient.GetLocalPlayerId());
            break;
        case net::ClientConnectionState::Rejected:
            gStatusText = "Rejected: " + detail;
            gEditingName = true;
            break;
    }
}

static bool ConnectToServer() {
#if defined(PLATFORM_WEB)
    const char* host = net::EnvString("WS_HOST", "localhost").c_str();
    const uint16_t port = net::EnvPort("WS_PORT", net::kDefaultWsPort);
    char url[256];
    std::snprintf(url, sizeof(url), "ws://%s:%u", host, port);
    return gClient.ConnectWeb(url, gPlayerName, OnConnectionState);
#else
    const std::string host = net::EnvString("SERVER_HOST", "127.0.0.1");
    const uint16_t port = net::EnvPort("SERVER_PORT", net::kDefaultTcpPort);
    return gClient.ConnectDesktop(host, port, gPlayerName, OnConnectionState);
#endif
}

static void UpdateGame() {
    gClient.Update();

    if (gEditingName) {
        int key = GetCharPressed();
        while (key > 0) {
            if (key >= 32 && key <= 125 && gPlayerName.size() < 16) {
                gPlayerName.push_back(static_cast<char>(key));
            }
            key = GetCharPressed();
        }

        if (IsKeyPressed(KEY_BACKSPACE) && !gPlayerName.empty()) {
            gPlayerName.pop_back();
        }

        if (IsKeyPressed(KEY_ENTER) && !gPlayerName.empty()) {
            ConnectToServer();
        }
        return;
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        gClient.Disconnect();
        return;
    }

    net::PlayerInput input;
    input.up = IsKeyDown(KEY_W) || IsKeyDown(KEY_UP);
    input.down = IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN);
    input.left = IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT);
    input.right = IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT);
    gClient.SendInput(input);
}

static void DrawGame() {
    BeginDrawing();
    ClearBackground(Color{24, 26, 32, 255});

    DrawRectangle(80, 80, static_cast<int>(net::kWorldWidth), static_cast<int>(net::kWorldHeight),
                  Color{36, 40, 50, 255});
    DrawRectangleLines(80, 80, static_cast<int>(net::kWorldWidth),
                       static_cast<int>(net::kWorldHeight), Color{70, 76, 92, 255});

    for (const net::PlayerState& player : gClient.GetPlayers()) {
        const bool isLocal = player.id == gClient.GetLocalPlayerId();
        const Color color = isLocal ? gLocalColor : ColorForPlayer(player.id);
        const int x = 80 + static_cast<int>(player.x);
        const int y = 80 + static_cast<int>(player.y);
        const int radius = static_cast<int>(net::kPlayerRadius);

        DrawCircle(x, y, static_cast<float>(radius), color);
        if (isLocal) {
            DrawCircleLines(x, y, static_cast<float>(radius + 3), RAYWHITE);
        }

        DrawText(player.name.c_str(), x - 24, y - radius - 22, 16, RAYWHITE);
    }

    DrawText("Multiplayer Template", 20, 20, 24, RAYWHITE);
    DrawText(gStatusText.c_str(), 20, 52, 18, LIGHTGRAY);

    if (gEditingName) {
        DrawText("Name:", 20, 100, 20, RAYWHITE);
        DrawText(gPlayerName.c_str(), 90, 100, 20, YELLOW);
        DrawText("_", 90 + MeasureText(gPlayerName.c_str(), 20), 100, 20, YELLOW);
        DrawText("ENTER = connect   ESC = quit", 20, 130, 16, GRAY);
    } else {
        DrawText(TextFormat("Tick: %u", gClient.GetServerTick()), 20, 100, 18, GRAY);
        DrawText(TextFormat("Ping: %d ms", gClient.GetPingMs()), 20, 124, 18, GRAY);
        DrawText("ESC = disconnect", 20, 148, 16, GRAY);
    }

    EndDrawing();
}

#if defined(PLATFORM_WEB)
static void MainLoop() {
    UpdateGame();
    DrawGame();
}

int main() {
    InitWindow(screenWidth, screenHeight, "Multiplayer Game");
    SetTargetFPS(60);
    emscripten_set_main_loop(MainLoop, 0, 1);
    CloseWindow();
    return 0;
}
#else
int main() {
    InitWindow(screenWidth, screenHeight, "Multiplayer Game");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        UpdateGame();
        DrawGame();
    }

    gClient.Disconnect();
    CloseWindow();
    return 0;
}
#endif
