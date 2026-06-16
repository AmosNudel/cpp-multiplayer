#include "raylib.h"

#include <cstdio>
#include <string>

#include "client/connection_config.hpp"
#include "client/game_client.hpp"
#include "client/viewport.hpp"
#include "common/config.hpp"
#include "common/grid.hpp"

#if defined(PLATFORM_WEB)
#include <emscripten.h>
#endif

static const char* kIdleSpritePath =
    "assets/player_sprites/Sprites/with_outline/IDLE.png";
static const char* kRunSpritePath =
    "assets/player_sprites/Sprites/with_outline/RUN.png";
static const float kPlayerSpriteHeight = 96.0f;
static const float kWorldScreenOriginX = 80.0f;
static const float kWorldScreenOriginY = 80.0f;

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

struct SpriteSheet {
    Texture2D texture{};
    int frameWidth = 0;
    int frameHeight = 0;
    int frameCount = 0;
    bool loaded = false;

    void Load(const char* path, int frames) {
        texture = LoadTexture(path);
        if (texture.id == 0) {
            TraceLog(LOG_WARNING, "Failed to load sprite sheet: %s", path);
            return;
        }
        frameCount = frames;
        frameWidth = texture.width / frameCount;
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
        if (!loaded || frameCount <= 0) {
            return;
        }

        frame = frame % frameCount;
        const float frameX = static_cast<float>(frame * frameWidth);
        const Rectangle source = facingRight
                                     ? Rectangle{frameX, 0.0f,
                                                 static_cast<float>(frameWidth),
                                                 static_cast<float>(frameHeight)}
                                     : Rectangle{frameX + static_cast<float>(frameWidth), 0.0f,
                                                 -static_cast<float>(frameWidth),
                                                 static_cast<float>(frameHeight)};
        const float scale = kPlayerSpriteHeight / static_cast<float>(frameHeight);
        const float drawWidth = static_cast<float>(frameWidth) * scale;
        const float drawHeight = static_cast<float>(frameHeight) * scale;
        const Rectangle dest = {
            center.x - drawWidth * 0.5f,
            center.y - drawHeight * 0.5f,
            drawWidth,
            drawHeight,
        };

        DrawTexturePro(texture, source, dest, Vector2{0.0f, 0.0f}, 0.0f, tint);
    }
};

struct PlayerSprites {
    SpriteSheet idle;
    SpriteSheet run;

    void Load() {
        idle.Load(ResolveAssetPath(kIdleSpritePath).c_str(), net::kIdleFrameCount);
        run.Load(ResolveAssetPath(kRunSpritePath).c_str(), net::kRunFrameCount);
    }

    void Unload() {
        idle.Unload();
        run.Unload();
    }

    bool AnyLoaded() const { return idle.loaded || run.loaded; }

    void Draw(const net::PlayerState& player, uint32_t serverTick, Vector2 center,
              Color tint) const {
        const int frame =
            net::AnimFrameIndex(player.anim, serverTick, player.animStartTick);

        switch (player.anim) {
            case net::PlayerAnim::Run:
                if (run.loaded) {
                    run.Draw(center, tint, frame, player.facingRight);
                    return;
                }
                break;
            case net::PlayerAnim::Idle:
            default:
                if (idle.loaded) {
                    idle.Draw(center, tint, frame, player.facingRight);
                    return;
                }
                break;
        }

        DrawCircleV(center, net::kPlayerRadius, tint);
    }
};

static net::GameClient gClient;
static GameViewport gViewport;
static PlayerSprites gPlayerSprites;
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
            gStatusText = "Connected - click map to move, T to chat";
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

static Rectangle CellScreenRect(int col, int row) {
    return {
        kWorldScreenOriginX + static_cast<float>(col) * net::kGridCellSize,
        kWorldScreenOriginY + static_cast<float>(row) * net::kGridCellSize,
        net::kGridCellSize,
        net::kGridCellSize,
    };
}

static bool TryGetWorldCellFromVirtual(Vector2 virtualPos, int& col, int& row) {
    const float worldX = virtualPos.x - kWorldScreenOriginX;
    const float worldY = virtualPos.y - kWorldScreenOriginY;
    if (worldX < 0.0f || worldY < 0.0f || worldX >= net::kWorldWidth ||
        worldY >= net::kWorldHeight) {
        return false;
    }

    col = net::WorldToCellCol(worldX);
    row = net::WorldToCellRow(worldY);
    return net::IsValidCell(col, row);
}

static bool IsMouseOverChatPanel(Vector2 virtualPos) {
    if (gEditingName) {
        return false;
    }

    const Rectangle panel = {
        20.0f,
        static_cast<float>(GameViewport::kVirtualHeight - 190),
        420.0f,
        170.0f,
    };
    return CheckCollisionPointRec(virtualPos, panel);
}

static void HandleMapClick() {
    const Vector2 screenPos = GetMousePosition();
    if (!gViewport.ContainsScreenPoint(screenPos)) {
        return;
    }

    const Vector2 virtualPos = gViewport.ScreenToVirtual(screenPos);
    if (IsMouseOverChatPanel(virtualPos)) {
        return;
    }

    int col = 0;
    int row = 0;
    if (!TryGetWorldCellFromVirtual(virtualPos, col, row)) {
        return;
    }

    gClient.SendMoveRequest(col, row);
}

static void DrawGrid() {
    const int originX = static_cast<int>(kWorldScreenOriginX);
    const int originY = static_cast<int>(kWorldScreenOriginY);
    const int worldW = static_cast<int>(net::kWorldWidth);
    const int worldH = static_cast<int>(net::kWorldHeight);
    const Color gridColor = Color{52, 58, 72, 255};

    for (int col = 0; col <= net::kGridCols; ++col) {
        const int x = originX + col * static_cast<int>(net::kGridCellSize);
        DrawLine(x, originY, x, originY + worldH, gridColor);
    }

    for (int row = 0; row <= net::kGridRows; ++row) {
        const int y = originY + row * static_cast<int>(net::kGridCellSize);
        DrawLine(originX, y, originX + worldW, y, gridColor);
    }
}

static void DrawGridHighlights() {
    if (gEditingName || gChatOpen) {
        return;
    }

    const Vector2 screenPos = GetMousePosition();
    if (!gViewport.ContainsScreenPoint(screenPos)) {
        return;
    }

    const Vector2 virtualPos = gViewport.ScreenToVirtual(screenPos);
    int hoverCol = 0;
    int hoverRow = 0;
    if (!IsMouseOverChatPanel(virtualPos) &&
        TryGetWorldCellFromVirtual(virtualPos, hoverCol, hoverRow)) {
        DrawRectangleRec(CellScreenRect(hoverCol, hoverRow), Color{90, 100, 130, 60});
    }

    for (const net::PlayerState& player : gClient.GetPlayers()) {
        if (player.moveTargetCol < 0 || player.moveTargetRow < 0) {
            continue;
        }

        const bool isLocal = player.id == gClient.GetLocalPlayerId();
        const Color tint = isLocal ? Color{120, 180, 255, 80}
                                   : Color{180, 180, 180, 50};
        DrawRectangleRec(CellScreenRect(player.moveTargetCol, player.moveTargetRow), tint);
    }
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
    gViewport.UpdateLayout();
    gClient.Update();

    if (IsKeyPressed(KEY_F11)) {
        ToggleFullscreen();
    }

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

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        HandleMapClick();
    }
}

static void DrawChatPanel() {
    const int panelX = 20;
    const int panelY = GameViewport::kVirtualHeight - 190;
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
    gViewport.BeginFrame();

    DrawRectangle(static_cast<int>(kWorldScreenOriginX),
                  static_cast<int>(kWorldScreenOriginY),
                  static_cast<int>(net::kWorldWidth), static_cast<int>(net::kWorldHeight),
                  Color{36, 40, 50, 255});
    DrawGrid();
    DrawRectangleLines(static_cast<int>(kWorldScreenOriginX),
                       static_cast<int>(kWorldScreenOriginY),
                       static_cast<int>(net::kWorldWidth),
                       static_cast<int>(net::kWorldHeight), Color{70, 76, 92, 255});
    DrawGridHighlights();

    const float nameOffsetY = gPlayerSprites.AnyLoaded()
                                  ? kPlayerSpriteHeight * 0.5f + 8.0f
                                  : net::kPlayerRadius + 22.0f;

    for (const net::PlayerState& player : gClient.GetPlayers()) {
        const bool isLocal = player.id == gClient.GetLocalPlayerId();
        const Color color = isLocal ? gLocalColor : ColorForPlayer(player.id);
        const Vector2 center = {
            kWorldScreenOriginX + player.x,
            kWorldScreenOriginY + player.y,
        };

        gPlayerSprites.Draw(player, gClient.GetServerTick(), center, color);
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
        DrawText("F11 = fullscreen   ESC = disconnect", 20, 148, 16, GRAY);
        DrawChatPanel();
    }

    gViewport.EndFrame();
}

#if defined(PLATFORM_WEB)
static void MainLoop() {
    UpdateGame();
    DrawGame();
}

int main() {
    InitGameWindow("Multiplayer Game");
    gViewport.Init();
    gPlayerSprites.Load();
    emscripten_set_main_loop(MainLoop, 0, 1);
    gPlayerSprites.Unload();
    gViewport.Shutdown();
    CloseWindow();
    return 0;
}
#else
int main() {
    InitGameWindow("Multiplayer Game");
    gViewport.Init();
    gPlayerSprites.Load();

    while (!WindowShouldClose()) {
        UpdateGame();
        DrawGame();
    }

    gClient.Disconnect();
    gPlayerSprites.Unload();
    gViewport.Shutdown();
    CloseWindow();
    return 0;
}
#endif
