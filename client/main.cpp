#include "raylib.h"

#include <cstdio>
#include <string>

#include "client/connection_config.hpp"
#include "client/game_client.hpp"
#include "common/config.hpp"

#if defined(PLATFORM_WEB)
#include <emscripten.h>
#endif

static const int screenWidth = 960;
static const int screenHeight = 640;

static const char* kIdleSpritePath =
    "assets/player_sprites/Sprites/with_outline/IDLE.png";
static const float kPlayerSpriteHeight = 96.0f;

struct PlayerIdleSprite {
    Texture2D texture{};
    int frameWidth = 0;
    int frameHeight = 0;
    bool loaded = false;

    void Load(const char* path) {
        texture = LoadTexture(path);
        if (texture.id == 0) {
            TraceLog(LOG_WARNING, "Failed to load player sprite: %s", path);
            return;
        }
        frameWidth = texture.width / net::kIdleFrameCount;
        frameHeight = texture.height;
        loaded = true;
    }

    void Unload() {
        if (loaded) {
            UnloadTexture(texture);
            loaded = false;
        }
    }

    void Draw(Vector2 center, Color tint, int frame, bool facingRight) const {
        if (!loaded) {
            DrawCircleV(center, net::kPlayerRadius, tint);
            return;
        }

        frame = frame % net::kIdleFrameCount;
        const Rectangle source = {
            static_cast<float>(frame * frameWidth), 0.0f,
            static_cast<float>(frameWidth), static_cast<float>(frameHeight),
        };
        const float scale = kPlayerSpriteHeight / static_cast<float>(frameHeight);
        const float drawWidth = static_cast<float>(frameWidth) * scale;
        const float drawHeight = static_cast<float>(frameHeight) * scale;
        const Rectangle dest = facingRight
                                   ? Rectangle{
                                         center.x - drawWidth * 0.5f,
                                         center.y - drawHeight * 0.5f,
                                         drawWidth,
                                         drawHeight,
                                     }
                                   : Rectangle{
                                         center.x + drawWidth * 0.5f,
                                         center.y - drawHeight * 0.5f,
                                         -drawWidth,
                                         drawHeight,
                                     };

        DrawTexturePro(texture, source, dest, Vector2{0.0f, 0.0f}, 0.0f, tint);
    }
};

static std::string ResolveAssetPath(const char* relativePath) {
    const std::string relative = relativePath;
    const std::string candidates[] = {relative, "../" + relative};
    for (const std::string& path : candidates) {
        if (FileExists(path.c_str())) {
            return path;
        }
    }
    return relative;
}

static net::GameClient gClient;
static PlayerIdleSprite gPlayerIdleSprite;
static std::string gStatusText = "Press ENTER to connect";
static std::string gPlayerName = "Player";
static std::string gChatInput;
static bool gEditingName = true;
static bool gChatOpen = false;
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
            gChatOpen = false;
            gChatInput.clear();
            break;
        case net::ClientConnectionState::Connecting:
            gStatusText = detail;
            break;
        case net::ClientConnectionState::Joined:
            gStatusText = "Connected - WASD to move, T to chat";
            gEditingName = false;
            gChatOpen = false;
            gChatInput.clear();
            gLocalColor = ColorForPlayer(gClient.GetLocalPlayerId());
            break;
        case net::ClientConnectionState::Rejected:
            gStatusText = "Rejected: " + detail;
            gEditingName = true;
            gChatOpen = false;
            gChatInput.clear();
            break;
    }
}

static bool ConnectToServer() {
#if defined(PLATFORM_WEB)
    const std::string url = net::BuildWebSocketUrl();
    return gClient.ConnectWeb(url, gPlayerName, OnConnectionState);
#else
    const net::DesktopEndpoint endpoint = net::GetDesktopEndpoint();
    return gClient.ConnectDesktop(endpoint.host, endpoint.port, gPlayerName,
                                  OnConnectionState);
#endif
}

static void HandleChatInput() {
    int key = GetCharPressed();
    while (key > 0) {
        if (key >= 32 && key <= 125 &&
            gChatInput.size() < net::kMaxChatLength) {
            gChatInput.push_back(static_cast<char>(key));
        }
        key = GetCharPressed();
    }

    if (IsKeyPressed(KEY_BACKSPACE) && !gChatInput.empty()) {
        gChatInput.pop_back();
    }

    if (IsKeyPressed(KEY_ENTER) && !gChatInput.empty()) {
        gClient.SendChat(gChatInput);
        gChatInput.clear();
        gChatOpen = false;
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        gChatInput.clear();
        gChatOpen = false;
    }
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

    if (IsKeyPressed(KEY_T) && !gChatOpen) {
        gChatOpen = true;
        return;
    }

    if (gChatOpen) {
        HandleChatInput();
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

static void DrawChatPanel() {
    const int panelX = 20;
    const int panelY = screenHeight - 190;
    const int panelW = 420;
    const int panelH = 170;

    DrawRectangle(panelX, panelY, panelW, panelH, Color{18, 20, 26, 220});
    DrawRectangleLines(panelX, panelY, panelW, panelH, Color{70, 76, 92, 255});
    DrawText("Chat", panelX + 10, panelY + 8, 18, RAYWHITE);

    int lineY = panelY + 34;
    for (const net::ChatMessage& line : gClient.GetChatLog()) {
        const std::string formatted = line.name + ": " + line.text;
        DrawText(formatted.c_str(), panelX + 10, lineY, 16, LIGHTGRAY);
        lineY += 18;
    }

    if (gChatOpen) {
        const int inputY = panelY + panelH - 34;
        DrawRectangle(panelX + 8, inputY, panelW - 16, 26, Color{30, 34, 42, 255});
        DrawText("> ", panelX + 14, inputY + 5, 16, YELLOW);
        DrawText(gChatInput.c_str(), panelX + 34, inputY + 5, 16, YELLOW);
        DrawText("_", panelX + 34 + MeasureText(gChatInput.c_str(), 16), inputY + 5, 16, YELLOW);
        DrawText("ENTER = send   ESC = close chat", panelX + 10, panelY + panelH - 56, 14, GRAY);
    } else {
        DrawText("T = open chat", panelX + 10, panelY + panelH - 28, 14, GRAY);
    }
}

static void DrawGame() {
    BeginDrawing();
    ClearBackground(Color{24, 26, 32, 255});

    DrawRectangle(80, 80, static_cast<int>(net::kWorldWidth), static_cast<int>(net::kWorldHeight),
                  Color{36, 40, 50, 255});
    DrawRectangleLines(80, 80, static_cast<int>(net::kWorldWidth),
                       static_cast<int>(net::kWorldHeight), Color{70, 76, 92, 255});

    const float nameOffsetY = gPlayerIdleSprite.loaded
                                  ? kPlayerSpriteHeight * 0.5f + 8.0f
                                  : net::kPlayerRadius + 22.0f;

    for (const net::PlayerState& player : gClient.GetPlayers()) {
        const bool isLocal = player.id == gClient.GetLocalPlayerId();
        const Color color = isLocal ? gLocalColor : ColorForPlayer(player.id);
        const Vector2 center = {
            80.0f + player.x,
            80.0f + player.y,
        };

        const int frame = net::AnimFrameIndex(player.anim, gClient.GetServerTick(),
                                              player.animStartTick);
        gPlayerIdleSprite.Draw(center, color, frame, player.facingRight);
        DrawText(player.name.c_str(),
                 static_cast<int>(center.x - 24.0f),
                 static_cast<int>(center.y - nameOffsetY),
                 16, RAYWHITE);
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
        DrawChatPanel();
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
    gPlayerIdleSprite.Load(ResolveAssetPath(kIdleSpritePath).c_str());
    emscripten_set_main_loop(MainLoop, 0, 1);
    gPlayerIdleSprite.Unload();
    CloseWindow();
    return 0;
}
#else
int main() {
    InitWindow(screenWidth, screenHeight, "Multiplayer Game");
    SetTargetFPS(60);
    gPlayerIdleSprite.Load(ResolveAssetPath(kIdleSpritePath).c_str());

    while (!WindowShouldClose()) {
        UpdateGame();
        DrawGame();
    }

    gClient.Disconnect();
    gPlayerIdleSprite.Unload();
    CloseWindow();
    return 0;
}
#endif
