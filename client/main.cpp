#include "raylib.h"

#include <cstdio>
#include <algorithm>
#include <optional>
#include <string>

#include "client/connection_config.hpp"
#include "client/game_client.hpp"
#include "client/viewport.hpp"
#include "client/world_view.hpp"
#include "common/config.hpp"
#include "common/enemies.hpp"
#include "common/entity_defs.hpp"
#include "common/entity_registry.hpp"
#include "common/entity_state.hpp"
#include "common/grid.hpp"
#include "common/grid_map.hpp"

#if defined(PLATFORM_WEB)
#include <emscripten.h>
#endif

static const char* kIdleSpritePath =
    "assets/player_sprites/Sprites/with_outline/IDLE.png";
static const char* kRunSpritePath =
    "assets/player_sprites/Sprites/with_outline/RUN.png";
static const char* kAttack1SpritePath =
    "assets/player_sprites/Sprites/without_outline/ATTACK 1.png";
static const char* kAttack2SpritePath =
    "assets/player_sprites/Sprites/without_outline/ATTACK 2.png";
static const char* kAttack3SpritePath =
    "assets/player_sprites/Sprites/without_outline/ATTACK 3.png";
static const char* kJumpSpritePath =
    "assets/player_sprites/Sprites/with_outline/JUMP.png";
static const char* kGoblinIdleSpritePath = "assets/enemy/Goblin/Idle.png";
static const char* kGoblinRunSpritePath = "assets/enemy/Goblin/Run.png";
static const char* kGoblinAttackSpritePath = "assets/enemy/Goblin/Attack.png";
static const float kPlayerSpriteHeight = 96.0f;
static constexpr float kGridScreenBottom = WorldView::kGridVirtualY + net::kWorldHeight;

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

    void Draw(Vector2 center, Color tint, int frame, bool facingRight,
              float spriteHeight = kPlayerSpriteHeight) const {
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
        const float scale = spriteHeight / static_cast<float>(frameHeight);
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
    SpriteSheet attack1;
    SpriteSheet attack2;
    SpriteSheet attack3;
    SpriteSheet jump;

    void Load() {
        idle.Load(ResolveAssetPath(kIdleSpritePath).c_str(), net::kIdleFrameCount);
        run.Load(ResolveAssetPath(kRunSpritePath).c_str(), net::kRunFrameCount);
        attack1.Load(ResolveAssetPath(kAttack1SpritePath).c_str(), net::kAttack1FrameCount);
        attack2.Load(ResolveAssetPath(kAttack2SpritePath).c_str(), net::kAttack2FrameCount);
        attack3.Load(ResolveAssetPath(kAttack3SpritePath).c_str(), net::kAttack3FrameCount);
        jump.Load(ResolveAssetPath(kJumpSpritePath).c_str(), net::kJumpFrameCount);
    }

    void Unload() {
        idle.Unload();
        run.Unload();
        attack1.Unload();
        attack2.Unload();
        attack3.Unload();
        jump.Unload();
    }

    bool AnyLoaded() const {
        return idle.loaded || run.loaded || attack1.loaded || attack2.loaded || attack3.loaded ||
               jump.loaded;
    }

    const SpriteSheet* SheetForAnim(net::PlayerAnim anim) const {
        switch (anim) {
            case net::PlayerAnim::Run: return run.loaded ? &run : nullptr;
            case net::PlayerAnim::Attack1: return attack1.loaded ? &attack1 : nullptr;
            case net::PlayerAnim::Attack2: return attack2.loaded ? &attack2 : nullptr;
            case net::PlayerAnim::Attack3: return attack3.loaded ? &attack3 : nullptr;
            case net::PlayerAnim::Jump: return jump.loaded ? &jump : nullptr;
            case net::PlayerAnim::Hit:
            case net::PlayerAnim::Dead:
            case net::PlayerAnim::Idle:
            default: return idle.loaded ? &idle : nullptr;
        }
    }

    void Draw(const net::PlayerState& player, uint32_t serverTick, Vector2 center,
              Color tint) const {
        if (player.state == net::EntityState::Dead) {
            tint = Color{80, 80, 80, 160};
        } else if (player.state == net::EntityState::Hit) {
            tint = Color{255, 120, 120, 255};
        }

        const int frame =
            net::AnimFrameIndex(player.anim, serverTick, player.animStartTick);

        if (const SpriteSheet* sheet = SheetForAnim(player.anim)) {
            sheet->Draw(center, tint, frame, player.facingRight);
            return;
        }

        DrawCircleV(center, net::kPlayerRadius, tint);
    }
};

struct GoblinSprites {
    SpriteSheet idle;
    SpriteSheet run;
    SpriteSheet attack;

    void Load() {
        idle.Load(ResolveAssetPath(kGoblinIdleSpritePath).c_str(), net::kGoblinIdleFrameCount);
        run.Load(ResolveAssetPath(kGoblinRunSpritePath).c_str(), net::kRunFrameCount);
        attack.Load(ResolveAssetPath(kGoblinAttackSpritePath).c_str(),
                    net::kGoblinAttackFrameCount);
    }

    void Unload() {
        idle.Unload();
        run.Unload();
        attack.Unload();
    }

    bool Loaded() const { return idle.loaded; }

    const SpriteSheet* SheetForAnim(net::PlayerAnim anim) const {
        switch (anim) {
            case net::PlayerAnim::Attack1: return attack.loaded ? &attack : nullptr;
            case net::PlayerAnim::Run: return run.loaded ? &run : nullptr;
            case net::PlayerAnim::Idle:
            default: return idle.loaded ? &idle : nullptr;
        }
    }

    void Draw(const net::EnemyState& enemy, uint32_t serverTick, Vector2 center) const {
        Color tint = WHITE;
        if (enemy.state == net::EntityState::Dead) {
            tint = Color{80, 80, 80, 160};
        } else if (enemy.state == net::EntityState::Hit) {
            tint = Color{255, 160, 160, 255};
        }

        if (!idle.loaded) {
            DrawCircleV(center, net::kPlayerRadius, Color{120, 200, 80, 255});
            return;
        }

        const int frame =
            net::GoblinAnimFrameIndex(enemy.anim, serverTick, enemy.animStartTick);
        if (const SpriteSheet* sheet = SheetForAnim(enemy.anim)) {
            sheet->Draw(center, tint, frame, enemy.facingRight, net::kGoblinSpriteHeight);
            return;
        }

        idle.Draw(center, tint, frame, enemy.facingRight, net::kGoblinSpriteHeight);
    }
};

static net::GameClient gClient;
static GameViewport gViewport;
static WorldView gWorldView;
static PlayerSprites gPlayerSprites;
static GoblinSprites gGoblinSprites;
static std::string gStatusText = "Press ENTER to connect";
static std::string gPlayerName = "Player";
static std::string gChatInput;
static bool gEditingName = true;
static bool gChatExpanded = false;
#if !defined(PLATFORM_WEB)
static bool gOptionsOpen = false;
#endif
static Color gLocalColor = RAYWHITE;

static constexpr int kChatPanelX = 20;
static constexpr int kChatPanelW = 420;
static constexpr int kChatCollapsedH = 46;
static constexpr int kChatExpandedH = 170;

static Vector2 GetVirtualMousePosition() {
    return gViewport.ScreenToVirtual(GetMousePosition());
}

static Rectangle ChatPanelRect() {
    const float panelH = gChatExpanded ? static_cast<float>(kChatExpandedH)
                                       : static_cast<float>(kChatCollapsedH);
    const float panelY = static_cast<float>(GameViewport::kVirtualHeight) - panelH - 12.0f;
    return {
        static_cast<float>(kChatPanelX),
        panelY,
        static_cast<float>(kChatPanelW),
        panelH,
    };
}

static Rectangle GridScreenRect() {
    return gWorldView.GridVirtualRect();
}

static Rectangle CellWorldRect(int col, int row) {
    return {
        static_cast<float>(col) * net::kGridCellSize,
        static_cast<float>(row) * net::kGridCellSize,
        net::kGridCellSize,
        net::kGridCellSize,
    };
}

static bool TryGetWorldCellFromVirtual(Vector2 virtualPos, int& col, int& row) {
    if (!CheckCollisionPointRec(virtualPos, GridScreenRect())) {
        return false;
    }

    const Vector2 world = gWorldView.VirtualToWorld(virtualPos);
    if (world.x < 0.0f || world.y < 0.0f || world.x >= net::kWorldWidth ||
        world.y >= net::kWorldHeight) {
        return false;
    }

    col = net::WorldToCellCol(world.x);
    row = net::WorldToCellRow(world.y);
    return net::IsValidCell(col, row);
}

static void DrawUiChrome() {
    DrawRectangle(0, 0, GameViewport::kVirtualWidth, GameViewport::kHudHeight,
                  Color{24, 26, 32, 255});
    DrawRectangle(0, static_cast<int>(kGridScreenBottom), GameViewport::kVirtualWidth,
                  GameViewport::kBottomBarHeight, Color{24, 26, 32, 255});
    DrawLine(0, GameViewport::kHudHeight, GameViewport::kVirtualWidth, GameViewport::kHudHeight,
             Color{52, 58, 72, 255});
    DrawLine(0, static_cast<int>(kGridScreenBottom), GameViewport::kVirtualWidth,
             static_cast<int>(kGridScreenBottom), Color{52, 58, 72, 255});
}

static Rectangle ChatToggleButtonRect() {
    const Rectangle panel = ChatPanelRect();
    return {
        panel.x + panel.width - 34.0f,
        panel.y + 8.0f,
        24.0f,
        24.0f,
    };
}

static Rectangle RespawnGoblinButtonRect() {
    return {
        static_cast<float>(GameViewport::kVirtualWidth - 148),
        20.0f,
        88.0f,
        32.0f,
    };
}

static Rectangle ActionsPanelRect() {
    return {
        20.0f,
        kGridScreenBottom + 16.0f,
        static_cast<float>(GameViewport::kVirtualWidth - 40),
        static_cast<float>(GameViewport::kBottomBarHeight - 32),
    };
}

static Rectangle DisengageButtonRect() {
    const Rectangle panel = ActionsPanelRect();
    return {
        panel.x,
        panel.y + 28.0f,
        120.0f,
        36.0f,
    };
}

static bool IsLocalPlayerInCombat() {
    if (gClient.GetState() != net::ClientConnectionState::Joined) {
        return false;
    }

    const int localPlayerId = gClient.GetLocalPlayerId();
    for (const net::PlayerState& player : gClient.GetPlayers()) {
        if (player.id == localPlayerId) {
            return player.state == net::EntityState::Combat && player.targetId >= 0;
        }
    }
    return false;
}

static bool IsMouseOverChatPanel(Vector2 virtualPos) {
    if (gEditingName) {
        return false;
    }
    return CheckCollisionPointRec(virtualPos, ChatPanelRect());
}

static bool IsMouseOverActionsPanel(Vector2 virtualPos) {
    if (gEditingName) {
        return false;
    }
    return CheckCollisionPointRec(virtualPos, ActionsPanelRect());
}

static void DrawUiButton(const char* label, Rectangle bounds, bool enabled = true) {
    const Vector2 mouse = GetVirtualMousePosition();
    const bool hover = enabled && CheckCollisionPointRec(mouse, bounds);
    DrawRectangleRec(bounds,
                     enabled ? (hover ? Color{54, 58, 70, 255} : Color{38, 42, 52, 255})
                             : Color{32, 34, 40, 255});
    DrawRectangleLinesEx(bounds, 1.0f,
                         enabled ? Color{82, 88, 104, 255} : Color{58, 62, 72, 255});
    const int fontSize = 16;
    const int textW = MeasureText(label, fontSize);
    DrawText(label,
             static_cast<int>(bounds.x + (bounds.width - textW) * 0.5f),
             static_cast<int>(bounds.y + (bounds.height - fontSize) * 0.5f),
             fontSize, enabled ? RAYWHITE : Color{120, 124, 132, 255});
}

static bool WasUiButtonPressed(Rectangle bounds, bool enabled = true) {
    return enabled && CheckCollisionPointRec(GetVirtualMousePosition(), bounds) &&
           IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static void DrawRespawnGoblinButton() {
    if (gEditingName || gClient.GetState() != net::ClientConnectionState::Joined) {
        return;
    }

    DrawUiButton("Respawn", RespawnGoblinButtonRect());
}

static void DrawActionsPanel() {
    if (gEditingName || gClient.GetState() != net::ClientConnectionState::Joined) {
        return;
    }

    const Rectangle panel = ActionsPanelRect();
    DrawText("Actions", static_cast<int>(panel.x), static_cast<int>(panel.y), 18, LIGHTGRAY);
    DrawUiButton("Disengage", DisengageButtonRect(), IsLocalPlayerInCombat());
}

static std::string TruncateToWidth(const std::string& text, int maxWidth, int fontSize) {
    if (MeasureText(text.c_str(), fontSize) <= maxWidth) {
        return text;
    }

    std::string truncated = text;
    while (!truncated.empty() &&
           MeasureText((truncated + "...").c_str(), fontSize) > maxWidth) {
        truncated.pop_back();
    }
    return truncated + "...";
}

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
            gChatExpanded = false;
            gChatInput.clear();
#if !defined(PLATFORM_WEB)
            gOptionsOpen = false;
#endif
            break;
        case net::ClientConnectionState::Connecting:
            gStatusText = detail;
            break;
        case net::ClientConnectionState::Joined:
            gStatusText = "Connected - click map to move, click enemy to attack";
            gEditingName = false;
            gChatExpanded = false;
            gChatInput.clear();
#if !defined(PLATFORM_WEB)
            gOptionsOpen = false;
#endif
            gLocalColor = ColorForPlayer(gClient.GetLocalPlayerId());
            break;
        case net::ClientConnectionState::Rejected:
            gStatusText = "Rejected: " + detail;
            gEditingName = true;
            gChatExpanded = false;
            gChatInput.clear();
#if !defined(PLATFORM_WEB)
            gOptionsOpen = false;
#endif
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

static std::optional<int> FindEnemyAtCell(int col, int row) {
    for (const net::EnemyState& enemy : gClient.GetEnemies()) {
        if (enemy.state == net::EntityState::Dead) {
            continue;
        }
        if (net::WorldToCellCol(enemy.x) == col && net::WorldToCellRow(enemy.y) == row) {
            return enemy.id;
        }
    }
    return std::nullopt;
}

static void HandleMapClick() {
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        return;
    }
    if (gWorldView.IsPanning() || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        return;
    }

    const Vector2 screenPos = GetMousePosition();
    if (!gViewport.ContainsScreenPoint(screenPos)) {
        return;
    }

    const Vector2 virtualPos = GetVirtualMousePosition();
#if !defined(PLATFORM_WEB)
    if (gOptionsOpen) {
        return;
    }
#endif
    if (IsMouseOverChatPanel(virtualPos)) {
        return;
    }

    if (!CheckCollisionPointRec(virtualPos, GridScreenRect())) {
        return;
    }

    int col = 0;
    int row = 0;
    if (!TryGetWorldCellFromVirtual(virtualPos, col, row)) {
        return;
    }
    if (!net::DefaultGridMap().IsWalkable(col, row)) {
        return;
    }

    if (const std::optional<int> enemyId = FindEnemyAtCell(col, row)) {
        gClient.SendAttackRequest(*enemyId);
        return;
    }

    gClient.SendMoveRequest(col, row);
}

static void HandleUiClicks() {
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        return;
    }

    const Vector2 screenPos = GetMousePosition();
    if (!gViewport.ContainsScreenPoint(screenPos)) {
        return;
    }

#if !defined(PLATFORM_WEB)
    if (gOptionsOpen) {
        return;
    }
#endif

    if (WasUiButtonPressed(ChatToggleButtonRect())) {
        if (gChatExpanded) {
            gChatExpanded = false;
            gChatInput.clear();
        } else {
            gChatExpanded = true;
        }
        return;
    }

    if (gClient.GetState() == net::ClientConnectionState::Joined &&
        WasUiButtonPressed(RespawnGoblinButtonRect())) {
        gClient.SendRespawnEnemy(net::kDefaultGoblinId);
        return;
    }

    if (gClient.GetState() == net::ClientConnectionState::Joined &&
        WasUiButtonPressed(DisengageButtonRect(), IsLocalPlayerInCombat())) {
        gClient.SendDisengage();
        return;
    }
}

#if !defined(PLATFORM_WEB)
static void HandleOptionsInput() {
    if (!gOptionsOpen) {
        return;
    }

    const int panelW = 260;
    const int panelH = 180;
    const int panelX = (GameViewport::kVirtualWidth - panelW) / 2;
    const int panelY = (GameViewport::kVirtualHeight - panelH) / 2;
    const Rectangle resumeBtn = {static_cast<float>(panelX + 30), static_cast<float>(panelY + 52),
                                 200.0f, 32.0f};
    const Rectangle exitBtn = {static_cast<float>(panelX + 30), static_cast<float>(panelY + 102),
                               200.0f, 32.0f};

    if (WasUiButtonPressed(resumeBtn)) {
        gOptionsOpen = false;
    }

    if (WasUiButtonPressed(exitBtn)) {
        if (gClient.GetState() == net::ClientConnectionState::Joined) {
            gClient.Disconnect();
        }
        CloseWindow();
    }
}
#endif

static void DrawGridTiles() {
    const net::GridMap& map = net::DefaultGridMap();
    const Color wallColor = Color{90, 94, 104, 255};
    const Color propColor = Color{220, 130, 45, 255};

    for (int row = 0; row < net::kGridRows; ++row) {
        for (int col = 0; col < net::kGridCols; ++col) {
            const net::TileType tile = map.Get(col, row);
            if (tile == net::TileType::Empty || tile == net::TileType::Enemy) {
                continue;
            }

            const Color color = tile == net::TileType::Wall ? wallColor : propColor;
            DrawRectangleRec(CellWorldRect(col, row), color);
        }
    }
}

static void DrawGrid() {
    const int worldW = static_cast<int>(net::kWorldWidth);
    const int worldH = static_cast<int>(net::kWorldHeight);
    const Color gridColor = Color{52, 58, 72, 255};

    for (int col = 0; col <= net::kGridCols; ++col) {
        const int x = col * static_cast<int>(net::kGridCellSize);
        DrawLine(x, 0, x, worldH, gridColor);
    }

    for (int row = 0; row <= net::kGridRows; ++row) {
        const int y = row * static_cast<int>(net::kGridCellSize);
        DrawLine(0, y, worldW, y, gridColor);
    }
}

static void DrawGridHighlights() {
    if (gEditingName || gChatExpanded) {
        return;
    }
#if !defined(PLATFORM_WEB)
    if (gOptionsOpen) {
        return;
    }
#endif

    const Vector2 screenPos = GetMousePosition();
    if (!gViewport.ContainsScreenPoint(screenPos)) {
        return;
    }

    const Vector2 virtualPos = GetVirtualMousePosition();
    int hoverCol = 0;
    int hoverRow = 0;
    if (!IsMouseOverChatPanel(virtualPos) &&
        TryGetWorldCellFromVirtual(virtualPos, hoverCol, hoverRow)) {
        const net::GridMap& map = net::DefaultGridMap();
        const Color hoverColor = map.IsWalkable(hoverCol, hoverRow)
                                     ? Color{90, 100, 130, 60}
                                     : Color{180, 80, 80, 50};
        DrawRectangleRec(CellWorldRect(hoverCol, hoverRow), hoverColor);
    }

    for (const net::PlayerState& player : gClient.GetPlayers()) {
        if (player.moveTargetCol < 0 || player.moveTargetRow < 0) {
            continue;
        }

        const bool isLocal = player.id == gClient.GetLocalPlayerId();
        const Color tint = isLocal ? Color{120, 180, 255, 80}
                                   : Color{180, 180, 180, 50};
        DrawRectangleRec(CellWorldRect(player.moveTargetCol, player.moveTargetRow), tint);
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
    }
}

static void UpdateGame() {
    gViewport.UpdateLayout();
    gClient.Update();

    const Vector2 virtualMouse = GetVirtualMousePosition();
    const bool allowCameraInput = !gEditingName && !gChatExpanded && !IsMouseOverActionsPanel(virtualMouse)
#if !defined(PLATFORM_WEB)
                                  && !gOptionsOpen
#endif
        ;
    gWorldView.UpdateInput(virtualMouse, allowCameraInput);

    if (IsKeyPressed(KEY_R) && allowCameraInput) {
        gWorldView.Reset();
    }

    if (IsKeyPressed(KEY_F11)) {
        ToggleFullscreen();
    }

#if !defined(PLATFORM_WEB)
    if (IsKeyPressed(KEY_ESCAPE)) {
        gOptionsOpen = !gOptionsOpen;
    }
#else
    if (IsKeyPressed(KEY_ESCAPE) && gChatExpanded) {
        gChatExpanded = false;
        gChatInput.clear();
    }
#endif

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
#if !defined(PLATFORM_WEB)
        HandleOptionsInput();
        if (gOptionsOpen) {
            return;
        }
#endif
        return;
    }

    if (gChatExpanded) {
        HandleChatInput();
    }

#if !defined(PLATFORM_WEB)
    HandleOptionsInput();
    if (gOptionsOpen) {
        return;
    }
#endif

    HandleUiClicks();
    HandleMapClick();
}

static void DrawChatPanel() {
    const Rectangle panel = ChatPanelRect();
    const int panelX = static_cast<int>(panel.x);
    const int panelY = static_cast<int>(panel.y);
    const int panelW = static_cast<int>(panel.width);
    const int panelH = static_cast<int>(panel.height);

    DrawRectangle(panelX, panelY, panelW, panelH, Color{18, 20, 26, 220});
    DrawRectangleLines(panelX, panelY, panelW, panelH, Color{70, 76, 92, 255});

    const Rectangle toggleButton = ChatToggleButtonRect();
    if (gChatExpanded) {
        DrawText("Chat", panelX + 10, panelY + 8, 18, RAYWHITE);
        DrawUiButton("-", toggleButton);

        int lineY = panelY + 34;
        for (const net::ChatMessage& line : gClient.GetChatLog()) {
            const std::string formatted = line.name + ": " + line.text;
            DrawText(formatted.c_str(), panelX + 10, lineY, 16, LIGHTGRAY);
            lineY += 18;
        }

        const int inputY = panelY + panelH - 34;
        DrawRectangle(panelX + 8, inputY, panelW - 16, 26, Color{30, 34, 42, 255});
        DrawText("> ", panelX + 14, inputY + 5, 16, YELLOW);
        DrawText(gChatInput.c_str(), panelX + 34, inputY + 5, 16, YELLOW);
        DrawText("_", panelX + 34 + MeasureText(gChatInput.c_str(), 16), inputY + 5, 16, YELLOW);
        DrawText("ENTER = send", panelX + 10, panelY + panelH - 56, 14, GRAY);
    } else {
        const net::ChatMessage* latest = nullptr;
        if (!gClient.GetChatLog().empty()) {
            latest = &gClient.GetChatLog().back();
        }

        std::string preview = "No messages yet";
        if (latest != nullptr) {
            preview = latest->name + ": " + latest->text;
        }
        preview = TruncateToWidth(preview, panelW - 56, 16);
        DrawText(preview.c_str(), panelX + 10, panelY + 14, 16, LIGHTGRAY);
        DrawUiButton("+", toggleButton);
    }
}

#if !defined(PLATFORM_WEB)
static void DrawOptionsPanel() {
    if (!gOptionsOpen) {
        return;
    }

    DrawRectangle(0, 0, GameViewport::kVirtualWidth, GameViewport::kVirtualHeight,
                  Color{0, 0, 0, 120});

    const int panelW = 260;
    const int panelH = 180;
    const int panelX = (GameViewport::kVirtualWidth - panelW) / 2;
    const int panelY = (GameViewport::kVirtualHeight - panelH) / 2;

    DrawRectangle(panelX, panelY, panelW, panelH, Color{28, 30, 38, 255});
    DrawRectangleLines(panelX, panelY, panelW, panelH, Color{82, 88, 104, 255});
    DrawText("Options", panelX + 16, panelY + 12, 22, RAYWHITE);

    DrawUiButton("Resume", {static_cast<float>(panelX + 30), static_cast<float>(panelY + 52),
                            200.0f, 32.0f});
    DrawUiButton("Exit Game", {static_cast<float>(panelX + 30), static_cast<float>(panelY + 102),
                               200.0f, 32.0f});
}
#endif

static void DrawEntityHpBar(Vector2 worldCenter, int hp, int maxHp, float spriteHeight) {
    if (maxHp <= 0) {
        return;
    }

    const float barWidth = 36.0f;
    const float barHeight = 4.0f;
    const float y = worldCenter.y - spriteHeight * 0.5f - 10.0f;
    const float x = worldCenter.x - barWidth * 0.5f;
    const float fillRatio = static_cast<float>(std::max(0, hp)) / static_cast<float>(maxHp);

    DrawRectangle(x, y, barWidth, barHeight, Color{40, 40, 40, 200});
    DrawRectangle(x, y, barWidth * fillRatio, barHeight, Color{220, 60, 60, 255});
}

static void DrawEntityShieldBar(Vector2 worldCenter, int shield, int maxShield, float spriteHeight) {
    if (maxShield <= 0 || shield <= 0) {
        return;
    }

    const float barWidth = 36.0f;
    const float barHeight = 4.0f;
    const float y = worldCenter.y - spriteHeight * 0.5f - 16.0f;
    const float x = worldCenter.x - barWidth * 0.5f;
    const float fillRatio = static_cast<float>(shield) / static_cast<float>(maxShield);

    DrawRectangle(x, y, barWidth, barHeight, Color{40, 40, 40, 200});
    DrawRectangle(x, y, barWidth * fillRatio, barHeight, Color{80, 160, 255, 255});
}

static void DrawPlayerBars(const net::PlayerState& player, Vector2 center) {
    if (player.state == net::EntityState::Dead) {
        return;
    }
    const net::EntityDef& def = net::DefaultEntityRegistry().MustFind(net::kPlayerEntityId);
    DrawEntityShieldBar(center, player.shield, def.stats.maxShield, def.spriteHeight);
    DrawEntityHpBar(center, player.hp, def.stats.maxHp, def.spriteHeight);
}

static void DrawPlayerSprite(const net::PlayerState& player, Color color) {
    const Vector2 center = {player.x, player.y};
    gPlayerSprites.Draw(player, gClient.GetServerTick(), center, color);
    DrawPlayerBars(player, center);
}

static void DrawPlayerName(const net::PlayerState& player, float nameOffsetY) {
    const Vector2 virtualCenter = gWorldView.WorldToVirtual({player.x, player.y});
    DrawText(player.name.c_str(),
             static_cast<int>(virtualCenter.x - 24.0f),
             static_cast<int>(virtualCenter.y - nameOffsetY),
             16, RAYWHITE);
}

static void DrawEnemy(const net::EnemyState& enemy) {
    const Vector2 center = {enemy.x, enemy.y};
    gGoblinSprites.Draw(enemy, gClient.GetServerTick(), center);
    if (enemy.state != net::EntityState::Dead) {
        const net::EntityDef& def = net::DefaultEntityRegistry().MustFind(enemy.kind);
        DrawEntityHpBar(center, enemy.hp, def.stats.maxHp, def.spriteHeight);
    }
}

static void DrawEnemies() {
    for (const net::EnemyState& enemy : gClient.GetEnemies()) {
        if (enemy.kind == net::kGoblinEntityId) {
            DrawEnemy(enemy);
        }
    }
}

static void DrawGame() {
    gViewport.BeginFrame();

    DrawUiChrome();

    const Rectangle gridRect = gWorldView.GridVirtualRect();
    BeginScissorMode(static_cast<int>(gridRect.x), static_cast<int>(gridRect.y),
                     static_cast<int>(gridRect.width), static_cast<int>(gridRect.height));

    const Camera2D worldCamera = gWorldView.BuildCamera();
    BeginMode2D(worldCamera);

    DrawRectangle(0, 0, static_cast<int>(net::kWorldWidth),
                  static_cast<int>(net::kWorldHeight), Color{36, 40, 50, 255});
    DrawGridTiles();
    DrawGrid();
    DrawRectangleLines(0, 0, static_cast<int>(net::kWorldWidth),
                       static_cast<int>(net::kWorldHeight), Color{70, 76, 92, 255});
    DrawGridHighlights();
    DrawEnemies();

    const int localPlayerId = gClient.GetLocalPlayerId();

    for (const net::PlayerState& player : gClient.GetPlayers()) {
        if (player.id == localPlayerId) {
            continue;
        }
        DrawPlayerSprite(player, ColorForPlayer(player.id));
    }

    for (const net::PlayerState& player : gClient.GetPlayers()) {
        if (player.id != localPlayerId) {
            continue;
        }
        DrawPlayerSprite(player, gLocalColor);
    }

    EndMode2D();
    EndScissorMode();

    const float nameOffsetY = gPlayerSprites.AnyLoaded()
                                  ? kPlayerSpriteHeight * 0.5f + 8.0f
                                  : net::kPlayerRadius + 22.0f;

    for (const net::PlayerState& player : gClient.GetPlayers()) {
        DrawPlayerName(player, nameOffsetY);
    }

    DrawText("Multiplayer Template", 20, 20, 24, RAYWHITE);
    DrawText(gStatusText.c_str(), 20, 52, 18, LIGHTGRAY);
    DrawRespawnGoblinButton();
    DrawActionsPanel();

    if (gEditingName) {
        DrawText("Name:", 20, 100, 20, RAYWHITE);
        DrawText(gPlayerName.c_str(), 90, 100, 20, YELLOW);
        DrawText("_", 90 + MeasureText(gPlayerName.c_str(), 20), 100, 20, YELLOW);
        DrawText("ENTER = connect", 20, 130, 16, GRAY);
#if !defined(PLATFORM_WEB)
        DrawText("ESC = options", 20, 152, 16, GRAY);
#endif
    } else {
        DrawText(TextFormat("Tick: %u", gClient.GetServerTick()), 20, 100, 18, GRAY);
        DrawText(TextFormat("Ping: %d ms", gClient.GetPingMs()), 20, 124, 18, GRAY);
        DrawText(TextFormat("Zoom: %.0f%%   Scroll = zoom   MMB = pan   R = reset",
                            gWorldView.Zoom() * 100.0f),
                 20, 148, 16, GRAY);
#if !defined(PLATFORM_WEB)
        DrawText("F11 = fullscreen   ESC = options", 20, 172, 16, GRAY);
#else
        DrawText("F11 = fullscreen", 20, 172, 16, GRAY);
#endif
        DrawChatPanel();
    }

#if !defined(PLATFORM_WEB)
    DrawOptionsPanel();
#endif

    gViewport.EndFrame();
}

#if defined(PLATFORM_WEB)
static void MainLoop() {
    UpdateGame();
    DrawGame();
}

int main() {
    InitGameWindow("Multiplayer Game");
    net::InitializeEntityRegistry();
    gViewport.Init();
    gPlayerSprites.Load();
    gGoblinSprites.Load();
    emscripten_set_main_loop(MainLoop, 0, 1);
    gGoblinSprites.Unload();
    gPlayerSprites.Unload();
    gViewport.Shutdown();
    CloseWindow();
    return 0;
}
#else
int main() {
    InitGameWindow("Multiplayer Game");
    net::InitializeEntityRegistry();
    gViewport.Init();
    gPlayerSprites.Load();
    gGoblinSprites.Load();

    while (!WindowShouldClose()) {
        UpdateGame();
        DrawGame();
    }

    gClient.Disconnect();
    gGoblinSprites.Unload();
    gPlayerSprites.Unload();
    gViewport.Shutdown();
    CloseWindow();
    return 0;
}
#endif
