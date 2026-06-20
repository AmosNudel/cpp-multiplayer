#include "raylib.h"

#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "client/audio_manager.hpp"
#include "client/connection_config.hpp"
#include "client/game_client.hpp"
#include "client/pointer_input.hpp"
#include "client/viewport.hpp"
#include "client/web_text_input.hpp"
#include "client/world_view.hpp"
#include "common/config.hpp"
#include "common/enemies.hpp"
#include "common/entity_defs.hpp"
#include "common/entity_registry.hpp"
#include "common/entity_state.hpp"
#include "common/grid.hpp"
#include "common/grid_map.hpp"
#include "common/session.hpp"
#include "common/skills.hpp"
#include "common/arena_waves.hpp"

#include <algorithm>
#include <unordered_map>

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
static const char* kHitSpritePath =
    "assets/player_sprites/Sprites/with_outline/HURT.png";
static const char* kDeathSpritePath =
    "assets/player_sprites/Sprites/with_outline/DEATH.png";
static const char* kGoblinIdleSpritePath = "assets/enemy/Goblin/Idle.png";
static const char* kGoblinRunSpritePath = "assets/enemy/Goblin/Run.png";
static const char* kGoblinAttackSpritePath = "assets/enemy/Goblin/Attack.png";
static const char* kGoblinAttackVariantSpritePath =
    "assets/enemy/attack_variant1/Goblin/Attack2.png";
static const char* kGoblinHitSpritePath = "assets/enemy/Goblin/Take Hit.png";
static const char* kGoblinDeathSpritePath = "assets/enemy/Goblin/Death.png";
static const char* kThunderVfxPath = "assets/vfx/Thunderstrike w blur.png";
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
        SetTextureFilter(texture, TEXTURE_FILTER_POINT);
        SetTextureWrap(texture, TEXTURE_WRAP_CLAMP);
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
        if (frame < 0) frame = 0;

        const float frameX = static_cast<float>(frame * frameWidth);
        // Trim a tiny epsilon from frame edges to avoid sampling neighbors on WebGL.
        const float uvInset = 0.01f;
        const float sampleWidth = static_cast<float>(frameWidth) - uvInset * 2.0f;
        const float sourceY = uvInset;
        const float sourceH = static_cast<float>(frameHeight) - uvInset * 2.0f;
        const Rectangle source = facingRight
                         ? Rectangle{frameX + uvInset, sourceY, sampleWidth, sourceH}
                         : Rectangle{frameX + uvInset + sampleWidth, sourceY,
                             -sampleWidth, sourceH};

        const float scale = spriteHeight / static_cast<float>(frameHeight);
        const float drawWidth = static_cast<float>(frameWidth) * scale;
        const float drawHeight = static_cast<float>(frameHeight) * scale;
        const Rectangle dest = {
            std::round(center.x - drawWidth * 0.5f),
            std::round(center.y - drawHeight * 0.5f),
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
    SpriteSheet hit;
    SpriteSheet dead;

    void Load() {
        idle.Load(ResolveAssetPath(kIdleSpritePath).c_str(), net::kIdleFrameCount);
        run.Load(ResolveAssetPath(kRunSpritePath).c_str(), net::kRunFrameCount);
        attack1.Load(ResolveAssetPath(kAttack1SpritePath).c_str(), net::kAttack1FrameCount);
        attack2.Load(ResolveAssetPath(kAttack2SpritePath).c_str(), net::kAttack2FrameCount);
        attack3.Load(ResolveAssetPath(kAttack3SpritePath).c_str(), net::kAttack3FrameCount);
        jump.Load(ResolveAssetPath(kJumpSpritePath).c_str(), net::kJumpFrameCount);
        hit.Load(ResolveAssetPath(kHitSpritePath).c_str(), net::kHitFrameCount);
        dead.Load(ResolveAssetPath(kDeathSpritePath).c_str(), net::kDeadFrameCount);
    }

    void Unload() {
        idle.Unload();
        run.Unload();
        attack1.Unload();
        attack2.Unload();
        attack3.Unload();
        jump.Unload();
        hit.Unload();
        dead.Unload();
    }

    bool AnyLoaded() const {
        return idle.loaded || run.loaded || attack1.loaded || attack2.loaded || attack3.loaded ||
               jump.loaded || hit.loaded || dead.loaded;
    }

    const SpriteSheet* SheetForAnim(net::PlayerAnim anim) const {
        switch (anim) {
            case net::PlayerAnim::Run: return run.loaded ? &run : nullptr;
            case net::PlayerAnim::Attack1: return attack1.loaded ? &attack1 : nullptr;
            case net::PlayerAnim::Attack2: return attack2.loaded ? &attack2 : nullptr;
            case net::PlayerAnim::Attack3: return attack3.loaded ? &attack3 : nullptr;
            case net::PlayerAnim::Jump: return jump.loaded ? &jump : nullptr;
            case net::PlayerAnim::Hit: return hit.loaded ? &hit : nullptr;
            case net::PlayerAnim::Dead: return dead.loaded ? &dead : nullptr;
            case net::PlayerAnim::Idle:
            default: return idle.loaded ? &idle : nullptr;
        }
    }

    void Draw(const net::PlayerState& player, uint32_t serverTick, Vector2 center, Color tint,
              bool combatHitFlash = false) const {
        if (player.state == net::EntityState::Hit) {
            tint = Color{255, 120, 120, 255};
        } else if (combatHitFlash) {
            tint = Color{
                static_cast<unsigned char>(std::min(255, static_cast<int>(tint.r) + 80)),
                static_cast<unsigned char>(std::max(0, static_cast<int>(tint.g) - 40)),
                static_cast<unsigned char>(std::max(0, static_cast<int>(tint.b) - 40)),
                tint.a,
            };
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
    SpriteSheet attackVariant;
    SpriteSheet hit;
    SpriteSheet death;

    void Load() {
        idle.Load(ResolveAssetPath(kGoblinIdleSpritePath).c_str(), net::kGoblinIdleFrameCount);
        run.Load(ResolveAssetPath(kGoblinRunSpritePath).c_str(), net::kRunFrameCount);
        attack.Load(ResolveAssetPath(kGoblinAttackSpritePath).c_str(),
                    net::kGoblinAttackFrameCount);
        attackVariant.Load(ResolveAssetPath(kGoblinAttackVariantSpritePath).c_str(),
                           net::kGoblinAttackFrameCount);
        hit.Load(ResolveAssetPath(kGoblinHitSpritePath).c_str(), net::kHitFrameCount);
        death.Load(ResolveAssetPath(kGoblinDeathSpritePath).c_str(), net::kGoblinDeathFrameCount);
    }

    void Unload() {
        idle.Unload();
        run.Unload();
        attack.Unload();
        attackVariant.Unload();
        hit.Unload();
        death.Unload();
    }

    bool Loaded() const { return idle.loaded; }

    const SpriteSheet* SheetForAnim(net::PlayerAnim anim, bool useAttackVariant) const {
        switch (anim) {
            case net::PlayerAnim::Attack1:
                return attack.loaded ? &attack : nullptr;
            case net::PlayerAnim::Attack2:
                return useAttackVariant && attackVariant.loaded
                           ? &attackVariant
                           : (attack.loaded ? &attack : nullptr);
            case net::PlayerAnim::Run: return run.loaded ? &run : nullptr;
            case net::PlayerAnim::Hit: return hit.loaded ? &hit : nullptr;
            case net::PlayerAnim::Dead: return death.loaded ? &death : nullptr;
            case net::PlayerAnim::Idle:
            default: return idle.loaded ? &idle : nullptr;
        }
    }

    void Draw(const net::EnemyState& enemy, uint32_t serverTick, Vector2 center,
              bool combatHitFlash = false) const {
        Color tint = WHITE;
        if (enemy.state == net::EntityState::Hit) {
            tint = Color{255, 160, 160, 255};
        } else if (combatHitFlash) {
            tint = Color{255, 200, 200, 255};
        }

        const bool isBoss = net::IsGoblinBoss(enemy);
        const float spriteHeight = net::kGoblinSpriteHeight;

        if (isBoss && enemy.state != net::EntityState::Dead) {
            const float pulse = 0.85f + 0.15f * std::sin(static_cast<float>(serverTick) * 0.15f);
            const float auraRadius = spriteHeight * 0.28f * pulse;
            DrawCircleV(center, auraRadius + 10.0f, Color{255, 30, 30, 25});
            DrawCircleV(center, auraRadius + 5.0f, Color{255, 50, 50, 45});
            DrawCircleLines(center.x, center.y, auraRadius, Color{255, 70, 70, 190});
        }

        if (!idle.loaded) {
            DrawCircleV(center, net::kPlayerRadius,
                        isBoss ? Color{200, 60, 60, 255} : Color{120, 200, 80, 255});
            return;
        }

        const int frame =
            net::GoblinAnimFrameIndex(enemy.anim, serverTick, enemy.animStartTick);
        if (const SpriteSheet* sheet = SheetForAnim(enemy.anim, isBoss)) {
            sheet->Draw(center, tint, frame, enemy.facingRight, spriteHeight);
            return;
        }

        idle.Draw(center, tint, frame, enemy.facingRight, spriteHeight);
    }
};

static net::GameClient gClient;
static AudioManager gAudio;
static GameViewport gViewport;
static WorldView gWorldView;
static PointerInput gPointer;
static PlayerSprites gPlayerSprites;
static GoblinSprites gGoblinSprites;
static SpriteSheet gThunderVfx;
static std::string gStatusText = "Press ENTER to connect";
static std::string gPlayerName = "Player";
static std::string gChatInput;
static bool gEditingName = true;
static bool gNameInputFocused =
#if defined(PLATFORM_WEB)
    false;
#else
    true;
#endif
static bool gNameUsesWebInput = false;
static bool gChatUsesWebInput = false;
static bool gChatExpanded = false;
static bool gOptionsOpen = false;
static Color gLocalColor = RAYWHITE;
static bool gSpectating = false;
static int gSpectateTargetId = -1;

struct EntityVisualState {
    float displayX = 0.0f;
    float displayY = 0.0f;
    float fromX = 0.0f;
    float fromY = 0.0f;
    float toX = 0.0f;
    float toY = 0.0f;
    int lastHp = -1;
    int lastShield = -1;
    double snapTime = 0.0;
    double hitFlashUntil = 0.0;
    double loopClockStartTime = 0.0;
    bool loopClockInitialized = false;
};

struct FloatingDamage {
    float x = 0.0f;
    float y = 0.0f;
    int amount = 0;
    double spawnTime = 0.0;
};

static std::unordered_map<int, EntityVisualState> gPlayerVisuals;
static std::unordered_map<int, EntityVisualState> gEnemyVisuals;
static std::vector<FloatingDamage> gFloatingDamage;
static uint32_t gLastSyncedTick = 0;
static double gServerTickSnapTime = 0.0;
static int gPendingSkillId = -1;
static int gLastUnlockedSkillCount = 0;

static constexpr double kCombatHitFlashDuration = 0.12;
static constexpr double kFloatingDamageDuration = 0.9;

static bool IsLoopingAnim(net::PlayerAnim anim) {
    return anim == net::PlayerAnim::Idle || anim == net::PlayerAnim::Run;
}

static void DrawUiButton(const char* label, Rectangle bounds, bool enabled = true);
static bool WasUiButtonPressed(Rectangle bounds, bool enabled = true);
static Rectangle MuteButtonRect();
static Rectangle OptionsButtonRect();
static void DrawMuteButton();
static void DrawOptionsButton();
static void HandleMuteButtonClick();
static void HandleOptionsButtonClick();
static const net::PlayerState* FindLocalPlayer();
static net::SceneId GetLocalScene();
static void ResetNameInputFocus();
static Rectangle PlayerNameFieldRect();
static Rectangle PlayerNameConnectButtonRect();
static void ShowPlayerNameWebInput();
static Rectangle ChatPanelRect();
static Rectangle ChatInputFieldRect();
static void ShowChatWebInput();
static void HandlePlayerNameInput();
static void DrawPlayerNameEntry();
static bool ConnectToServer();

static constexpr int kChatPanelX = 20;
static constexpr int kChatPanelW = 420;
static constexpr int kChatCollapsedH = 46;
static constexpr int kChatExpandedH = 170;

static Vector2 GetVirtualMousePosition() {
    return gPointer.VirtualPosition();
}

static std::vector<Vector2> CollectVirtualTouchPoints() {
    std::vector<Vector2> points;
    const int touchCount = GetTouchPointCount();
    points.reserve(static_cast<size_t>(touchCount));
    for (int i = 0; i < touchCount; ++i) {
        points.push_back(gViewport.ScreenToVirtual(GetTouchPosition(i)));
    }
    return points;
}

static Vector2 GetVirtualPointerPosition(const std::vector<Vector2>& touchPoints) {
    if (!touchPoints.empty()) {
        return touchPoints[0];
    }
    return gViewport.ScreenToVirtual(GetMousePosition());
}

static void ResetNameInputFocus() {
#if defined(PLATFORM_WEB)
    WebTextInputHide();
    gNameInputFocused = false;
    gNameUsesWebInput = false;
    gChatUsesWebInput = false;
#else
    gNameInputFocused = true;
#endif
}

static Rectangle PlayerNameFieldRect() {
    return {90.0f, 96.0f, 220.0f, 32.0f};
}

static Rectangle PlayerNameConnectButtonRect() {
    return {320.0f, 94.0f, 100.0f, 36.0f};
}

static void ShowPlayerNameWebInput() {
#if defined(PLATFORM_WEB)
    const Rectangle screenRect = gViewport.VirtualToScreenRect(PlayerNameFieldRect());
    WebTextInputShow(screenRect, gPlayerName.c_str(), 16);
#endif
}

static Rectangle ChatInputFieldRect() {
    const Rectangle panel = ChatPanelRect();
    const float inputY = panel.y + panel.height - 34.0f;
    return {
        panel.x + 8.0f,
        inputY,
        panel.width - 16.0f,
        26.0f,
    };
}

static void ShowChatWebInput() {
#if defined(PLATFORM_WEB)
    const Rectangle screenRect = gViewport.VirtualToScreenRect(ChatInputFieldRect());
    WebTextInputShow(screenRect, gChatInput.c_str(), net::kMaxChatLength);
#endif
}

static void HandlePlayerNameInput() {
    if (!gEditingName) {
        return;
    }

#if defined(PLATFORM_WEB)
    if (gNameInputFocused && gNameUsesWebInput) {
        ShowPlayerNameWebInput();
        gPlayerName = WebTextInputGetValue();
        if (WebTextInputConsumeSubmit() && !gPlayerName.empty()) {
            ConnectToServer();
            return;
        }
    }
#endif

    if (gPointer.WasPressed()) {
        const Vector2 screenPos = GetMousePosition();
        if (!gViewport.ContainsScreenPoint(screenPos)) {
            return;
        }

        if (WasUiButtonPressed(PlayerNameConnectButtonRect(), !gPlayerName.empty())) {
            ConnectToServer();
            return;
        }

        if (WasUiButtonPressed(PlayerNameFieldRect())) {
            gNameInputFocused = true;
            gNameUsesWebInput = gPointer.UsesTouch();
#if defined(PLATFORM_WEB)
            if (gNameUsesWebInput) {
                ShowPlayerNameWebInput();
            } else {
                WebTextInputHide();
            }
#endif
            return;
        }

        if (gNameInputFocused &&
            !CheckCollisionPointRec(GetVirtualMousePosition(), PlayerNameFieldRect()) &&
            !CheckCollisionPointRec(GetVirtualMousePosition(), PlayerNameConnectButtonRect())) {
            const bool wasUsingWebInput = gNameUsesWebInput;
            gNameInputFocused = false;
            gNameUsesWebInput = false;
#if defined(PLATFORM_WEB)
            if (wasUsingWebInput) {
                gPlayerName = WebTextInputGetValue();
            }
            WebTextInputHide();
#endif
        }
    }

    if (!gNameInputFocused) {
        return;
    }

#if defined(PLATFORM_WEB)
    if (gNameUsesWebInput) {
        return;
    }
#endif

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
}

static void DrawPlayerNameEntry() {
    DrawText("Name:", 20, 100, 20, RAYWHITE);

    const Rectangle field = PlayerNameFieldRect();
    const bool focused = gNameInputFocused;
    DrawRectangleRec(field, Color{30, 34, 42, 255});
    DrawRectangleLinesEx(field, 1.0f,
                         focused ? Color{120, 180, 255, 255} : Color{82, 88, 104, 255});

#if !defined(PLATFORM_WEB)
    DrawText(gPlayerName.c_str(), static_cast<int>(field.x + 8),
             static_cast<int>(field.y + 6), 20, YELLOW);
    if (focused) {
        DrawText("_", static_cast<int>(field.x + 8 + MeasureText(gPlayerName.c_str(), 20)),
                 static_cast<int>(field.y + 6), 20, YELLOW);
    }
#else
    if (!focused || !gNameUsesWebInput) {
        DrawText(gPlayerName.c_str(), static_cast<int>(field.x + 8),
                 static_cast<int>(field.y + 6), 20, YELLOW);
        if (focused && !gNameUsesWebInput) {
            DrawText("_", static_cast<int>(field.x + 8 + MeasureText(gPlayerName.c_str(), 20)),
                     static_cast<int>(field.y + 6), 20, YELLOW);
        }
    }
    if (!focused) {
        DrawText("Tap name to edit", 20, 136, 16, GRAY);
    }
#endif

    DrawUiButton("Enter", PlayerNameConnectButtonRect(), !gPlayerName.empty());
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

static Rectangle OptionsButtonRect() {
    return {
        static_cast<float>(GameViewport::kVirtualWidth - 88),
        12.0f,
        36.0f,
        32.0f,
    };
}

static Rectangle MuteButtonRect() {
    return {
        static_cast<float>(GameViewport::kVirtualWidth - 44),
        12.0f,
        32.0f,
        32.0f,
    };
}

static void DrawMuteButton() {
    const Rectangle bounds = MuteButtonRect();
    const Vector2 mouse = GetVirtualMousePosition();
    const bool hover = CheckCollisionPointRec(mouse, bounds);
    DrawRectangleRec(bounds, hover ? Color{54, 58, 70, 255} : Color{38, 42, 52, 255});
    DrawRectangleLinesEx(bounds, 1.0f, Color{82, 88, 104, 255});

    const char* label = gAudio.IsMuted() ? "OFF" : "ON";
    const int fontSize = 14;
    const int textW = MeasureText(label, fontSize);
    DrawText(label,
             static_cast<int>(bounds.x + (bounds.width - textW) * 0.5f),
             static_cast<int>(bounds.y + (bounds.height - fontSize) * 0.5f),
             fontSize, gAudio.IsMuted() ? Color{180, 180, 180, 255} : RAYWHITE);
}

static void DrawOptionsButton() {
    const Rectangle bounds = OptionsButtonRect();
    const Vector2 mouse = GetVirtualMousePosition();
    const bool hover = CheckCollisionPointRec(mouse, bounds);
    const bool active = gOptionsOpen;
    DrawRectangleRec(bounds,
                     active ? Color{70, 76, 96, 255}
                            : (hover ? Color{54, 58, 70, 255} : Color{38, 42, 52, 255}));
    DrawRectangleLinesEx(bounds, 1.0f,
                         active ? Color{120, 130, 160, 255} : Color{82, 88, 104, 255});

    const char* label = "Opts";
    const int fontSize = 14;
    const int textW = MeasureText(label, fontSize);
    DrawText(label,
             static_cast<int>(bounds.x + (bounds.width - textW) * 0.5f),
             static_cast<int>(bounds.y + (bounds.height - fontSize) * 0.5f),
             fontSize, RAYWHITE);
}

static void HandleMuteButtonClick() {
    if (!gPointer.WasPressed()) {
        return;
    }
    const Vector2 screenPos = GetMousePosition();
    if (!gViewport.ContainsScreenPoint(screenPos)) {
        return;
    }
    if (WasUiButtonPressed(MuteButtonRect())) {
        gAudio.ToggleMute();
    }
}

static void HandleOptionsButtonClick() {
    if (!gPointer.WasPressed()) {
        return;
    }
    const Vector2 screenPos = GetMousePosition();
    if (!gViewport.ContainsScreenPoint(screenPos)) {
        return;
    }
    if (WasUiButtonPressed(OptionsButtonRect())) {
        if (gPendingSkillId >= 0) {
            gPendingSkillId = -1;
        } else {
            gOptionsOpen = !gOptionsOpen;
        }
    }
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

static const net::PlayerState* FindLocalPlayer() {
    const int localPlayerId = gClient.GetLocalPlayerId();
    for (const net::PlayerState& player : gClient.GetPlayers()) {
        if (player.id == localPlayerId) {
            return &player;
        }
    }
    return nullptr;
}

static bool IsLocalPlayerDead() {
    if (const net::PlayerState* local = FindLocalPlayer()) {
        return local->state == net::EntityState::Dead;
    }
    return false;
}

static Color SkillTintFor(net::SkillId skillId) {
    const net::SkillDef& def = net::SkillDefFor(skillId);
    switch (def.branch) {
        case net::SkillBranch::Dps:
            return Color{255, 120, 60, 255};
        case net::SkillBranch::Shield:
            return Color{90, 160, 255, 255};
        case net::SkillBranch::Support:
            return Color{90, 230, 120, 255};
    }
    return WHITE;
}

static bool IsSkillOnCooldown(int skillId) {
    const net::PlayerState* local = FindLocalPlayer();
    if (local == nullptr) {
        return false;
    }
    const uint32_t tick = gClient.GetServerTick();
    for (const net::PlayerSkillCooldown& cooldown : local->skillCooldowns) {
        if (cooldown.skillId == skillId && cooldown.readyAtTick > tick) {
            return true;
        }
    }
    return false;
}

static float SkillCooldownProgress(int skillId, int cooldownSeconds) {
    if (cooldownSeconds <= 0) {
        return 1.0f;
    }
    const net::PlayerState* local = FindLocalPlayer();
    if (local == nullptr) {
        return 1.0f;
    }
    const uint32_t tick = gClient.GetServerTick();
    const uint32_t totalTicks = static_cast<uint32_t>(cooldownSeconds * net::kTickRate);
    for (const net::PlayerSkillCooldown& cooldown : local->skillCooldowns) {
        if (cooldown.skillId == skillId && cooldown.readyAtTick > tick) {
            const uint32_t ticksRemaining = cooldown.readyAtTick - tick;
            return std::clamp(1.0f - static_cast<float>(ticksRemaining) /
                                      static_cast<float>(totalTicks),
                              0.0f, 1.0f);
        }
    }
    return 1.0f;
}

static std::optional<int> FindEnemyAtCell(int col, int row);
static std::optional<int> FindEnemyAtWorldPos(Vector2 worldPos);
static bool HasEnemyInSkillArea(int col, int row, int aoeRadius);

static bool LocalPlayerNeedsSkillPick() {
    const net::PlayerState* local = FindLocalPlayer();
    if (local == nullptr || GetLocalScene() != net::SceneId::Arena) {
        return false;
    }
    return local->skillTier < gClient.GetSession().teamLevel;
}

static void BeginSkillTargeting(net::SkillId skillId) {
    if (skillId == net::SkillId::None || !net::SkillRequiresGridTarget(skillId)) {
        return;
    }
    gPendingSkillId = net::SkillIdToInt(skillId);
}

static std::pair<int, int> EnemyAnchorCell(const net::EnemyState& enemy) {
    return {net::WorldToCellCol(enemy.x), net::WorldToCellRow(enemy.y)};
}

static std::pair<int, int> ResolveDamageSkillTarget(int col, int row, Vector2 worldPos) {
    if (const std::optional<int> enemyId = FindEnemyAtWorldPos(worldPos)) {
        for (const net::EnemyState& enemy : gClient.GetEnemies()) {
            if (enemy.id == *enemyId) {
                return EnemyAnchorCell(enemy);
            }
        }
    }

    if (FindEnemyAtCell(col, row).has_value()) {
        return {col, row};
    }

    static constexpr int kDirections[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1},
    };

    for (const auto& direction : kDirections) {
        const int candidateCol = col + direction[0];
        const int candidateRow = row + direction[1];
        if (!net::IsValidCell(candidateCol, candidateRow)) {
            continue;
        }
        if (!FindEnemyAtCell(candidateCol, candidateRow).has_value()) {
            continue;
        }
        return {candidateCol, candidateRow};
    }

    return {col, row};
}

static void SyncPendingSkillAfterUnlock() {
    const net::PlayerState* local = FindLocalPlayer();
    if (local == nullptr || GetLocalScene() != net::SceneId::Arena) {
        gLastUnlockedSkillCount = 0;
        return;
    }

    const int unlockedCount = static_cast<int>(local->unlockedSkillIds.size());
    if (unlockedCount > gLastUnlockedSkillCount && !local->unlockedSkillIds.empty()) {
        BeginSkillTargeting(net::SkillIdFromInt(local->unlockedSkillIds.back()));
    }
    gLastUnlockedSkillCount = unlockedCount;
}

static Rectangle SkillButtonRect(int index) {
    const Rectangle panel = ActionsPanelRect();
    return {
        panel.x + 130.0f + static_cast<float>(index) * 108.0f,
        panel.y + 28.0f,
        100.0f,
        36.0f,
    };
}

static Rectangle LevelUpOverlayPanelRect() {
    const int panelW = 420;
    const int panelH = 180;
    const int panelX = (GameViewport::kVirtualWidth - panelW) / 2;
    const int panelY = (GameViewport::kVirtualHeight - panelH) / 2;
    return {
        static_cast<float>(panelX),
        static_cast<float>(panelY),
        static_cast<float>(panelW),
        static_cast<float>(panelH),
    };
}

static Rectangle BranchVoteButtonRect(int index) {
    const Rectangle panel = LevelUpOverlayPanelRect();
    return {
        panel.x + 20.0f + static_cast<float>(index) * 130.0f,
        panel.y + 100.0f,
        120.0f,
        40.0f,
    };
}

static bool IsMouseOverLevelUpOverlay(Vector2 virtualPos) {
    return LocalPlayerNeedsSkillPick() &&
           CheckCollisionPointRec(virtualPos, LevelUpOverlayPanelRect());
}

static int SkillEffectFrame(uint32_t serverTick, uint32_t startTick) {
    if (serverTick < startTick) {
        return 0;
    }
    const uint32_t elapsed = serverTick - startTick;
    const int frame =
        static_cast<int>(elapsed / static_cast<uint32_t>(net::kThunderVfxTicksPerFrame));
    return frame >= net::kThunderVfxFrameCount ? net::kThunderVfxFrameCount - 1 : frame;
}

static bool IsArenaWipePending() {
    const net::SessionSnapshot& session = gClient.GetSession();
    return session.phase == net::SessionPhase::ArenaActive &&
           session.allDeadReturnAtTick > gClient.GetServerTick();
}

static bool IsArenaVictoryActive() {
    const net::SessionSnapshot& session = gClient.GetSession();
    return gClient.GetState() == net::ClientConnectionState::Joined &&
           GetLocalScene() == net::SceneId::Arena &&
           session.phase == net::SessionPhase::ArenaActive &&
           session.arenaVictoryEndsAtTick > gClient.GetServerTick();
}

static int ArenaVictorySecondsLeft() {
    const net::SessionSnapshot& session = gClient.GetSession();
    if (session.arenaVictoryEndsAtTick <= gClient.GetServerTick()) {
        return 0;
    }
    return static_cast<int>((session.arenaVictoryEndsAtTick - gClient.GetServerTick() +
                             net::kTickRate - 1) /
                            net::kTickRate);
}

static bool IsArenaSessionRunning() {
    const net::SessionSnapshot& session = gClient.GetSession();
    return session.arenaSessionEndsAtTick > gClient.GetServerTick();
}

static void AppendArenaResetHint(std::string& text) {
    if (!IsArenaSessionRunning() || GetLocalScene() != net::SceneId::Hub) {
        return;
    }

    const net::SessionSnapshot& session = gClient.GetSession();
    const int resetCount = static_cast<int>(session.arenaResetPlayerIds.size());
    text += TextFormat(", %d/%d voted reset (orange tile)",
                       resetCount, session.hubPlayerCount);
}

static int ArenaRejoinSecondsLeft() {
    const net::SessionSnapshot& session = gClient.GetSession();
    if (session.phase != net::SessionPhase::ArenaActive || session.arenaPlayerCount == 0) {
        return 0;
    }

    const net::PlayerState* local = FindLocalPlayer();
    uint32_t opensAt = session.arenaJoinOpensAtTick;
    if (local != nullptr && local->arenaRejoinAtTick > opensAt) {
        opensAt = local->arenaRejoinAtTick;
    }

    const uint32_t tick = gClient.GetServerTick();
    if (opensAt <= tick) {
        return 0;
    }

    return static_cast<int>((opensAt - tick + net::kTickRate - 1) / net::kTickRate);
}

static bool CanLocalRejoinArena() {
    const net::SessionSnapshot& session = gClient.GetSession();
    return GetLocalScene() == net::SceneId::Hub &&
           session.phase == net::SessionPhase::ArenaActive &&
           session.arenaPlayerCount > 0 &&
           ArenaRejoinSecondsLeft() == 0;
}

static int ArenaWipeSecondsLeft() {
    const net::SessionSnapshot& session = gClient.GetSession();
    if (session.allDeadReturnAtTick <= gClient.GetServerTick()) {
        return 0;
    }
    return static_cast<int>((session.allDeadReturnAtTick - gClient.GetServerTick()) /
                            net::kTickRate);
}

static int ArenaSecondsLeft() {
    const net::SessionSnapshot& session = gClient.GetSession();
    const uint32_t endsAt = session.arenaSessionEndsAtTick > 0
                                ? session.arenaSessionEndsAtTick
                                : session.phaseEndsAtTick;
    if (endsAt <= gClient.GetServerTick()) {
        return 0;
    }
    return static_cast<int>((endsAt - gClient.GetServerTick()) / net::kTickRate);
}

static std::vector<const net::PlayerState*> CollectSpectateTargets() {
    std::vector<const net::PlayerState*> targets;
    targets.reserve(gClient.GetPlayers().size());
    for (const net::PlayerState& player : gClient.GetPlayers()) {
        if (player.state != net::EntityState::Dead) {
            targets.push_back(&player);
        }
    }
    std::sort(targets.begin(), targets.end(),
              [](const net::PlayerState* a, const net::PlayerState* b) {
                  return a->id < b->id;
              });
    return targets;
}

static const net::PlayerState* GetSpectateTarget() {
    const std::vector<const net::PlayerState*> targets = CollectSpectateTargets();
    if (targets.empty()) {
        gSpectateTargetId = -1;
        return nullptr;
    }

    if (gSpectateTargetId >= 0) {
        for (const net::PlayerState* player : targets) {
            if (player->id == gSpectateTargetId) {
                return player;
            }
        }
    }

    gSpectateTargetId = targets.front()->id;
    return targets.front();
}

static void CycleSpectateTarget(int direction) {
    const std::vector<const net::PlayerState*> targets = CollectSpectateTargets();
    if (targets.empty()) {
        gSpectateTargetId = -1;
        return;
    }

    int index = -1;
    for (size_t i = 0; i < targets.size(); ++i) {
        if (targets[i]->id == gSpectateTargetId) {
            index = static_cast<int>(i);
            break;
        }
    }

    if (index < 0) {
        index = direction >= 0 ? 0 : static_cast<int>(targets.size()) - 1;
    } else {
        const int count = static_cast<int>(targets.size());
        index = ((index + direction) % count + count) % count;
    }

    gSpectateTargetId = targets[static_cast<size_t>(index)]->id;
}

static void SetSpectateTarget(int playerId) {
    for (const net::PlayerState& player : gClient.GetPlayers()) {
        if (player.id == playerId && player.state != net::EntityState::Dead) {
            gSpectateTargetId = playerId;
            return;
        }
    }
}

static int SpectateTargetIndex() {
    const std::vector<const net::PlayerState*> targets = CollectSpectateTargets();
    for (size_t i = 0; i < targets.size(); ++i) {
        if (targets[i]->id == gSpectateTargetId) {
            return static_cast<int>(i) + 1;
        }
    }
    return 0;
}

static void UpdateSpectateCamera() {
    if (!gSpectating || GetLocalScene() != net::SceneId::Arena || !IsLocalPlayerDead()) {
        return;
    }

    if (const net::PlayerState* target = GetSpectateTarget()) {
        gWorldView.SetTarget({target->x, target->y});
    }
}

static void BeginSpectating() {
    gSpectating = true;
    gSpectateTargetId = -1;
    UpdateSpectateCamera();
}

static void StopSpectating() {
    gSpectating = false;
    gSpectateTargetId = -1;
}

static Rectangle DeathPanelRect() {
    const int panelW = 300;
    const int panelH = 210;
    return {
        static_cast<float>((GameViewport::kVirtualWidth - panelW) / 2),
        static_cast<float>((GameViewport::kVirtualHeight - panelH) / 2),
        static_cast<float>(panelW),
        static_cast<float>(panelH),
    };
}

static Rectangle RespawnInArenaButtonRect() {
    const Rectangle panel = DeathPanelRect();
    return {panel.x + 24.0f, panel.y + 72.0f, panel.width - 48.0f, 36.0f};
}

static Rectangle ReturnToHubButtonRect() {
    const Rectangle panel = DeathPanelRect();
    return {panel.x + 24.0f, panel.y + 118.0f, 120.0f, 36.0f};
}

static Rectangle SpectateButtonRect() {
    const Rectangle panel = DeathPanelRect();
    return {panel.x + 156.0f, panel.y + 118.0f, 120.0f, 36.0f};
}

static Rectangle SpectateReturnButtonRect() {
    return {
        static_cast<float>(GameViewport::kVirtualWidth - 168),
        static_cast<float>(GameViewport::kHudHeight + 8),
        148.0f,
        32.0f,
    };
}

static Rectangle SpectatePrevButtonRect() {
    return {
        static_cast<float>(GameViewport::kVirtualWidth - 248),
        static_cast<float>(GameViewport::kHudHeight + 8),
        36.0f,
        32.0f,
    };
}

static Rectangle SpectateNextButtonRect() {
    return {
        static_cast<float>(GameViewport::kVirtualWidth - 208),
        static_cast<float>(GameViewport::kHudHeight + 8),
        36.0f,
        32.0f,
    };
}

static bool ShouldShowDeathPanel() {
    return gClient.GetState() == net::ClientConnectionState::Joined &&
           GetLocalScene() == net::SceneId::Arena && IsLocalPlayerDead() && !gSpectating &&
           !IsArenaVictoryActive();
}

static int ArenaDeathRespawnSecondsLeft() {
    const net::PlayerState* local = FindLocalPlayer();
    if (local == nullptr || local->state != net::EntityState::Dead) {
        return 0;
    }

    const uint32_t opensAt = local->stateStartTick + net::kArenaDeathRespawnDelayTicks;
    const uint32_t tick = gClient.GetServerTick();
    if (opensAt <= tick) {
        return 0;
    }

    return static_cast<int>((opensAt - tick + net::kTickRate - 1) / net::kTickRate);
}

static bool CanLocalRespawnInArena() {
    return ShouldShowDeathPanel() && ArenaDeathRespawnSecondsLeft() == 0;
}

static void DrawDeathPanel() {
    if (!ShouldShowDeathPanel()) {
        return;
    }

    DrawRectangle(0, 0, GameViewport::kVirtualWidth, GameViewport::kVirtualHeight,
                  Color{0, 0, 0, 100});

    const Rectangle panel = DeathPanelRect();
    DrawRectangleRec(panel, Color{28, 30, 38, 245});
    DrawRectangleLinesEx(panel, 1.0f, Color{82, 88, 104, 255});
    DrawText("You died", static_cast<int>(panel.x + 24), static_cast<int>(panel.y + 20), 24,
             RAYWHITE);
    DrawText("Respawn in the arena, return to hub, or spectate.", static_cast<int>(panel.x + 24),
             static_cast<int>(panel.y + 48), 16, LIGHTGRAY);

    const int respawnSecondsLeft = ArenaDeathRespawnSecondsLeft();
    const char* respawnLabel = respawnSecondsLeft > 0
                                   ? TextFormat("Respawn in Arena (%ds)", respawnSecondsLeft)
                                   : "Respawn in Arena";
    DrawUiButton(respawnLabel, RespawnInArenaButtonRect(), respawnSecondsLeft == 0);
    DrawUiButton("Return to Hub", ReturnToHubButtonRect());
    DrawUiButton("Spectate", SpectateButtonRect(), GetSpectateTarget() != nullptr);

    if (IsArenaWipePending()) {
        DrawText(TextFormat("Team wipe - returning to hub in %ds", ArenaWipeSecondsLeft()),
                 static_cast<int>(panel.x + 24), static_cast<int>(panel.y + 162), 14,
                 Color{255, 180, 120, 255});
    }
}

static void DrawArenaWipeOverlay() {
    if (gClient.GetState() != net::ClientConnectionState::Joined ||
        GetLocalScene() != net::SceneId::Arena || !IsArenaWipePending() || IsLocalPlayerDead() ||
        IsArenaVictoryActive()) {
        return;
    }

    DrawRectangle(0, 0, GameViewport::kVirtualWidth, GameViewport::kVirtualHeight,
                  Color{0, 0, 0, 120});

    const int panelW = 360;
    const int panelH = 90;
    const int panelX = (GameViewport::kVirtualWidth - panelW) / 2;
    const int panelY = (GameViewport::kVirtualHeight - panelH) / 2;
    DrawRectangle(panelX, panelY, panelW, panelH, Color{28, 30, 38, 245});
    DrawRectangleLines(panelX, panelY, panelW, panelH, Color{82, 88, 104, 255});
    DrawText("Everyone is down", panelX + 24, panelY + 16, 22, RAYWHITE);
    DrawText(TextFormat("Returning to hub in %ds...", ArenaWipeSecondsLeft()), panelX + 24,
             panelY + 48, 18, LIGHTGRAY);
}

static void DrawVictoryOverlay() {
    if (!IsArenaVictoryActive()) {
        return;
    }

    DrawRectangle(0, 0, GameViewport::kVirtualWidth, GameViewport::kVirtualHeight,
                  Color{0, 0, 0, 140});

    const int panelW = 380;
    const int panelH = 160;
    const int panelX = (GameViewport::kVirtualWidth - panelW) / 2;
    const int panelY = (GameViewport::kVirtualHeight - panelH) / 2;
    DrawRectangle(panelX, panelY, panelW, panelH, Color{28, 36, 28, 250});
    DrawRectangleLines(panelX, panelY, panelW, panelH, Color{120, 200, 90, 255});
    DrawText("Victory!", panelX + 24, panelY + 24, 36, Color{140, 255, 120, 255});
    DrawText("Goblin Boss defeated", panelX + 24, panelY + 72, 22, RAYWHITE);
    DrawText(TextFormat("Returning to hub in %ds...", ArenaVictorySecondsLeft()), panelX + 24,
             panelY + 108, 18, LIGHTGRAY);
}

static void ResetClientVisualState() {
    gPlayerVisuals.clear();
    gEnemyVisuals.clear();
    gFloatingDamage.clear();
    gLastSyncedTick = 0;
    gServerTickSnapTime = 0.0;
    gPendingSkillId = -1;
    gLastUnlockedSkillCount = 0;
}

// Extrapolate server tick between WorldState packets so sprite animations advance
// every render frame. Positions already interpolate this way; raw serverTick jumps
// when packets arrive late or batched (common over WebSocket), skipping anim frames.
static uint32_t DisplayServerTick() {
    const uint32_t tick = gClient.GetServerTick();
    if (tick == 0 || gClient.GetState() != net::ClientConnectionState::Joined) {
        return tick;
    }

    const double elapsed = GetTime() - gServerTickSnapTime;
    if (elapsed <= 0.0) {
        return tick;
    }

    constexpr uint32_t kMaxLeadTicks = 2;
    const uint32_t lead = std::min(
        kMaxLeadTicks,
        static_cast<uint32_t>(elapsed / static_cast<double>(net::kTickDuration)));
    return tick + lead;
}

static uint32_t LoopingAnimTick(const EntityVisualState* visual, uint32_t fallbackServerTick,
                                uint32_t fallbackAnimStartTick) {
    if (visual == nullptr || !visual->loopClockInitialized) {
        return fallbackServerTick >= fallbackAnimStartTick
                   ? (fallbackServerTick - fallbackAnimStartTick)
                   : 0;
    }

    const double elapsedSeconds = std::max(0.0, GetTime() - visual->loopClockStartTime);
    return static_cast<uint32_t>(
        elapsedSeconds / static_cast<double>(net::kTickDuration));
}

static Vector2 InterpolatedPosition(const EntityVisualState& visual) {
    const double elapsed = GetTime() - visual.snapTime;
    const float alpha =
        std::min(1.0f, static_cast<float>(elapsed / static_cast<double>(net::kTickDuration)));
    return {
        visual.fromX + (visual.toX - visual.fromX) * alpha,
        visual.fromY + (visual.toY - visual.fromY) * alpha,
    };
}

static void SyncEntityVisual(const net::PlayerState& entity, EntityVisualState& visual,
                             bool trackDamage, uint32_t serverTick) {
    const double now = GetTime();
    if (visual.lastHp < 0) {
        visual.displayX = entity.x;
        visual.displayY = entity.y;
        visual.fromX = entity.x;
        visual.fromY = entity.y;
        visual.toX = entity.x;
        visual.toY = entity.y;
        visual.snapTime = now;
        visual.lastHp = entity.hp;
        visual.lastShield = entity.shield;
        if (!visual.loopClockInitialized) {
            const uint32_t elapsedTicks = serverTick >= entity.animStartTick
                                              ? (serverTick - entity.animStartTick)
                                              : 0;
            visual.loopClockStartTime =
                now - static_cast<double>(elapsedTicks) * static_cast<double>(net::kTickDuration);
            visual.loopClockInitialized = true;
        }
        return;
    }

    if (entity.x != visual.toX || entity.y != visual.toY) {
        const Vector2 current = InterpolatedPosition(visual);
        visual.fromX = current.x;
        visual.fromY = current.y;
        visual.toX = entity.x;
        visual.toY = entity.y;
        visual.snapTime = now;
    }

    if (trackDamage) {
        const int totalHp = entity.hp + entity.shield;
        const int lastTotal = visual.lastHp + std::max(0, visual.lastShield);
        if (totalHp < lastTotal) {
            gFloatingDamage.push_back(
                FloatingDamage{entity.x, entity.y - 24.0f, lastTotal - totalHp, now});
            if (entity.state == net::EntityState::Combat) {
                visual.hitFlashUntil = now + kCombatHitFlashDuration;
            }
        }
        visual.lastHp = entity.hp;
        visual.lastShield = entity.shield;
    }

    if (!visual.loopClockInitialized) {
        const uint32_t elapsedTicks = serverTick >= entity.animStartTick
                                          ? (serverTick - entity.animStartTick)
                                          : 0;
        visual.loopClockStartTime =
            now - static_cast<double>(elapsedTicks) * static_cast<double>(net::kTickDuration);
        visual.loopClockInitialized = true;
    }
}

static void SyncEntityVisuals() {
    const uint32_t tick = gClient.GetServerTick();
    if (gClient.GetState() != net::ClientConnectionState::Joined) {
        return;
    }
    if (tick == gLastSyncedTick && !gPlayerVisuals.empty()) {
        return;
    }
    gServerTickSnapTime = GetTime();
    gLastSyncedTick = tick;

    std::unordered_map<int, EntityVisualState> nextPlayers;
    for (const net::PlayerState& player : gClient.GetPlayers()) {
        EntityVisualState visual = gPlayerVisuals[player.id];
        SyncEntityVisual(player, visual, true, tick);
        nextPlayers[player.id] = visual;
    }
    gPlayerVisuals = std::move(nextPlayers);

    std::unordered_map<int, EntityVisualState> nextEnemies;
    for (const net::EnemyState& enemy : gClient.GetEnemies()) {
        EntityVisualState visual = gEnemyVisuals[enemy.id];
        net::PlayerState proxy;
        proxy.x = enemy.x;
        proxy.y = enemy.y;
        proxy.hp = enemy.hp;
        proxy.shield = 0;
        proxy.state = enemy.state;
        proxy.animStartTick = enemy.animStartTick;
        SyncEntityVisual(proxy, visual, true, tick);
        nextEnemies[enemy.id] = visual;
    }
    gEnemyVisuals = std::move(nextEnemies);

    SyncPendingSkillAfterUnlock();

    const double now = GetTime();
    gFloatingDamage.erase(
        std::remove_if(gFloatingDamage.begin(), gFloatingDamage.end(),
                       [now](const FloatingDamage& entry) {
                           return now - entry.spawnTime > kFloatingDamageDuration;
                       }),
        gFloatingDamage.end());
}

static Vector2 DisplayPositionForPlayer(const net::PlayerState& player) {
    const auto it = gPlayerVisuals.find(player.id);
    if (it == gPlayerVisuals.end()) {
        return {player.x, player.y};
    }
    return InterpolatedPosition(it->second);
}

static Vector2 DisplayPositionForEnemy(const net::EnemyState& enemy) {
    Vector2 position = {enemy.x, enemy.y};
    const auto it = gEnemyVisuals.find(enemy.id);
    if (it != gEnemyVisuals.end()) {
        position = InterpolatedPosition(it->second);
    }
    return position;
}

static bool IsCombatHitFlashing(const EntityVisualState* visual) {
    return visual != nullptr && GetTime() < visual->hitFlashUntil;
}

static void DrawFloatingDamage() {
    const double now = GetTime();
    for (const FloatingDamage& entry : gFloatingDamage) {
        const float t = static_cast<float>((now - entry.spawnTime) / kFloatingDamageDuration);
        const Vector2 virtualPos =
            gWorldView.WorldToVirtual({entry.x, entry.y - t * 28.0f});
        const int alpha = static_cast<int>(255.0f * (1.0f - t));
        DrawText(TextFormat("-%d", entry.amount), static_cast<int>(virtualPos.x - 12.0f),
                 static_cast<int>(virtualPos.y), 18, Color{255, 90, 90, static_cast<unsigned char>(alpha)});
    }
}

static void DrawCombatTargetHighlights() {
    const net::PlayerState* local = FindLocalPlayer();
    if (local == nullptr || local->targetId < 0) {
        return;
    }

    for (const net::EnemyState& enemy : gClient.GetEnemies()) {
        if (enemy.id != local->targetId || enemy.state == net::EntityState::Dead) {
            continue;
        }
        const Vector2 pos = DisplayPositionForEnemy(enemy);
        const float highlightRadius = net::kPlayerRadius + 10.0f;
        DrawCircleLines(pos.x, pos.y, highlightRadius, Color{255, 220, 80, 220});
        DrawCircleLines(pos.x, pos.y, highlightRadius + 2.0f, Color{255, 220, 80, 100});
    }
}

static net::SceneId GetLocalScene() {
    if (const net::PlayerState* player = FindLocalPlayer()) {
        return player->sceneId;
    }
    return net::SceneId::Hub;
}

static const net::GridMap& GetActiveMap() {
    return GetLocalScene() == net::SceneId::Arena ? net::ArenaGridMap() : net::HubGridMap();
}

static void UpdateStatusText() {
    if (gClient.GetState() != net::ClientConnectionState::Joined) {
        return;
    }

    const net::SessionSnapshot& session = gClient.GetSession();
    if (GetLocalScene() == net::SceneId::Hub &&
        session.phase == net::SessionPhase::ArenaActive &&
        session.arenaPlayerCount > 0) {
        const int secondsLeft = ArenaSecondsLeft();
        const int rejoinSecondsLeft = ArenaRejoinSecondsLeft();
        if (rejoinSecondsLeft > 0) {
            gStatusText = TextFormat(
                "Hub - arena active (%d:%02d left), rejoin in %ds",
                secondsLeft / 60, secondsLeft % 60, rejoinSecondsLeft);
        } else {
            gStatusText = TextFormat(
                "Hub - arena active (%d:%02d left), click portal to rejoin",
                secondsLeft / 60, secondsLeft % 60);
        }
        AppendArenaResetHint(gStatusText);
        return;
    }

    if (GetLocalScene() == net::SceneId::Hub && IsArenaSessionRunning()) {
        const int secondsLeft = ArenaSecondsLeft();
        if (session.phase == net::SessionPhase::Lobby) {
            int lobbySecondsLeft = 0;
            if (session.phaseEndsAtTick > gClient.GetServerTick()) {
                lobbySecondsLeft = static_cast<int>(
                    (session.phaseEndsAtTick - gClient.GetServerTick()) / net::kTickRate);
            }
            const int readyCount = static_cast<int>(session.readyPlayerIds.size());
            gStatusText = TextFormat(
                "Hub - arena paused (%d:%02d left), %d/%d ready, starting in %ds (click portal)",
                secondsLeft / 60, secondsLeft % 60, readyCount, session.hubPlayerCount,
                lobbySecondsLeft);
            AppendArenaResetHint(gStatusText);
            return;
        }

        if (const net::PlayerState* local = FindLocalPlayer(); local != nullptr && local->isReady) {
            gStatusText = TextFormat("Hub - arena paused (%d:%02d left), ready (click portal to unready)",
                                     secondsLeft / 60, secondsLeft % 60);
        } else {
            gStatusText = TextFormat("Hub - arena paused (%d:%02d left), click portal to ready up",
                                     secondsLeft / 60, secondsLeft % 60);
        }
        AppendArenaResetHint(gStatusText);
        return;
    }

    if (GetLocalScene() == net::SceneId::Arena) {
        if (IsArenaVictoryActive()) {
            gStatusText = TextFormat("Arena - victory! Returning to hub in %ds",
                                     ArenaVictorySecondsLeft());
            return;
        }
        if (IsArenaWipePending()) {
            gStatusText = TextFormat("Arena - returning to hub in %ds", ArenaWipeSecondsLeft());
            return;
        }

        const int secondsLeft = ArenaSecondsLeft();
        const int nextThreshold = session.teamLevel >= net::kMaxTeamLevel
                                      ? session.teamXp
                                      : net::TeamXpThresholdForLevel(session.teamLevel + 1);
        gStatusText = TextFormat("Arena - %d:%02d | Wave %d/%d | Team Lv%d (%d/%d XP)",
                                 secondsLeft / 60, secondsLeft % 60, session.arenaWave,
                                 session.arenaWaveCount > 0 ? session.arenaWaveCount
                                                            : net::kArenaWaveCount,
                                 session.teamLevel, session.teamXp, nextThreshold);
        return;
    }

    if (session.phase == net::SessionPhase::Lobby) {
        int secondsLeft = 0;
        if (session.phaseEndsAtTick > gClient.GetServerTick()) {
            secondsLeft = static_cast<int>((session.phaseEndsAtTick - gClient.GetServerTick()) /
                                           net::kTickRate);
        }
        const int readyCount = static_cast<int>(session.readyPlayerIds.size());
        gStatusText = TextFormat("Hub - %d/%d ready, starting in %ds (click portal)",
                                 readyCount, session.hubPlayerCount, secondsLeft);
        return;
    }

    if (const net::PlayerState* local = FindLocalPlayer(); local != nullptr && local->isReady) {
        gStatusText = "Hub - ready (click portal to unready)";
    } else {
        gStatusText = "Hub - click portal to ready up";
    }
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

static void DrawUiButton(const char* label, Rectangle bounds, bool enabled) {
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

static void DrawSkillButton(const char* label, Rectangle bounds, bool enabled, net::SkillId skillId,
                            float cooldownProgress) {
    const bool onCooldown = cooldownProgress < 1.0f;
    const bool interactive = enabled && !onCooldown;

    const Vector2 mouse = GetVirtualMousePosition();
    const bool hover = interactive && CheckCollisionPointRec(mouse, bounds);
    DrawRectangleRec(bounds,
                     interactive ? (hover ? Color{54, 58, 70, 255} : Color{38, 42, 52, 255})
                                 : Color{32, 34, 40, 255});

    if (onCooldown) {
        const Color tint = SkillTintFor(skillId);
        constexpr float inset = 1.0f;
        const float innerH = bounds.height - inset * 2.0f;
        const float fillH = innerH * cooldownProgress;
        DrawRectangleRec({bounds.x + inset, bounds.y + bounds.height - inset - fillH,
                          bounds.width - inset * 2.0f, fillH},
                         Color{tint.r, tint.g, tint.b, 140});

        const float overlayH = innerH - fillH;
        if (overlayH > 0.0f) {
            DrawRectangleRec({bounds.x + inset, bounds.y + inset, bounds.width - inset * 2.0f,
                              overlayH},
                             Color{0, 0, 0, 90});
        }
    }

    DrawRectangleLinesEx(bounds, 1.0f,
                         interactive ? Color{82, 88, 104, 255} : Color{58, 62, 72, 255});
    const int fontSize = 16;
    const int textW = MeasureText(label, fontSize);
    DrawText(label,
             static_cast<int>(bounds.x + (bounds.width - textW) * 0.5f),
             static_cast<int>(bounds.y + (bounds.height - fontSize) * 0.5f),
             fontSize, interactive ? RAYWHITE : Color{120, 124, 132, 255});
}

static bool WasUiButtonPressed(Rectangle bounds, bool enabled) {
    return enabled && CheckCollisionPointRec(GetVirtualMousePosition(), bounds) &&
           gPointer.WasPressed();
}

static void DrawSpectateHud() {
    if (!gSpectating || GetLocalScene() != net::SceneId::Arena || !IsLocalPlayerDead() ||
        IsArenaWipePending()) {
        return;
    }

    const std::vector<const net::PlayerState*> targets = CollectSpectateTargets();
    const net::PlayerState* target = GetSpectateTarget();
    const int targetIndex = SpectateTargetIndex();
    const int targetCount = static_cast<int>(targets.size());
    const bool canCycle = targetCount > 1;

    if (target != nullptr) {
        DrawText(TextFormat("Spectating: %s (%d/%d)", target->name.c_str(), targetIndex,
                            targetCount),
                 20, GameViewport::kHudHeight + 12, 16, LIGHTGRAY);
    } else {
        DrawText("Spectating", 20, GameViewport::kHudHeight + 12, 16, LIGHTGRAY);
    }

    DrawUiButton("<", SpectatePrevButtonRect(), canCycle);
    DrawUiButton(">", SpectateNextButtonRect(), canCycle);
    DrawUiButton("Return to Hub", SpectateReturnButtonRect());
}

static void HandleSpectateInput() {
    if (!gSpectating || GetLocalScene() != net::SceneId::Arena || !IsLocalPlayerDead() ||
        IsArenaWipePending() || gChatExpanded || gEditingName || gOptionsOpen) {
        return;
    }

    if (IsKeyPressed(KEY_TAB)) {
        const bool reverse = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        CycleSpectateTarget(reverse ? -1 : 1);
        UpdateSpectateCamera();
    }

    if (!gPointer.WasPressed()) {
        return;
    }

    const Vector2 screenPos = GetMousePosition();
    if (!gViewport.ContainsScreenPoint(screenPos)) {
        return;
    }

    const Vector2 virtualPos = GetVirtualMousePosition();
    if (IsMouseOverChatPanel(virtualPos) ||
        CheckCollisionPointRec(virtualPos, SpectateReturnButtonRect()) ||
        CheckCollisionPointRec(virtualPos, SpectatePrevButtonRect()) ||
        CheckCollisionPointRec(virtualPos, SpectateNextButtonRect())) {
        return;
    }

    if (!CheckCollisionPointRec(virtualPos, gWorldView.GridVirtualRect())) {
        return;
    }

    const Vector2 world = gWorldView.VirtualToWorld(virtualPos);
    const float pickRadius = net::kPlayerRadius * 1.75f;
    const float pickRadiusSq = pickRadius * pickRadius;
    const net::PlayerState* bestTarget = nullptr;
    float bestDistSq = pickRadiusSq;

    for (const net::PlayerState& player : gClient.GetPlayers()) {
        if (player.state == net::EntityState::Dead) {
            continue;
        }

        const float dx = player.x - world.x;
        const float dy = player.y - world.y;
        const float distSq = dx * dx + dy * dy;
        if (distSq <= bestDistSq) {
            bestDistSq = distSq;
            bestTarget = &player;
        }
    }

    if (bestTarget != nullptr) {
        SetSpectateTarget(bestTarget->id);
        UpdateSpectateCamera();
    }
}

static void DrawActionsPanel() {
    if (gEditingName || gClient.GetState() != net::ClientConnectionState::Joined ||
        IsLocalPlayerDead()) {
        return;
    }

    const Rectangle panel = ActionsPanelRect();
    DrawText("Actions", static_cast<int>(panel.x), static_cast<int>(panel.y), 18, LIGHTGRAY);
    DrawUiButton("Disengage", DisengageButtonRect(), IsLocalPlayerInCombat());

    if (GetLocalScene() != net::SceneId::Arena) {
        return;
    }

    const net::SessionSnapshot& session = gClient.GetSession();
    const int nextThreshold = session.teamLevel >= net::kMaxTeamLevel
                                  ? session.teamXp
                                  : net::TeamXpThresholdForLevel(session.teamLevel + 1);
    DrawText(TextFormat("Team Lv%d  %d/%d XP", session.teamLevel, session.teamXp, nextThreshold),
             static_cast<int>(panel.x + 250.0f), static_cast<int>(panel.y), 16, YELLOW);

    int buttonIndex = 0;
    const net::PlayerState* local = FindLocalPlayer();
    if (local != nullptr) {
        for (int skillId : local->unlockedSkillIds) {
            const net::SkillId skill = net::SkillIdFromInt(skillId);
            const net::SkillDef& def = net::SkillDefFor(skill);
            const bool selected = gPendingSkillId == skillId;
            const Rectangle button = SkillButtonRect(buttonIndex);
            const float cooldownProgress =
                SkillCooldownProgress(skillId, def.cooldownSeconds);
            DrawSkillButton(def.displayName, button, true, skill, cooldownProgress);
            if (selected) {
                DrawRectangleLinesEx(button, 2.0f, SkillTintFor(skill));
            }
            ++buttonIndex;
        }
    }

    if (gPendingSkillId >= 0) {
        DrawText("Click a grid tile to cast skill (click skill again to cancel)",
                 static_cast<int>(panel.x), static_cast<int>(panel.y + 72.0f), 14, SKYBLUE);
    }
}

static void DrawLevelUpOverlay() {
    if (!LocalPlayerNeedsSkillPick() || IsLocalPlayerDead()) {
        return;
    }

    const net::PlayerState* local = FindLocalPlayer();
    const net::SessionSnapshot& session = gClient.GetSession();
    const int pickTier = local != nullptr ? local->skillTier + 1 : 1;

    const Rectangle panel = LevelUpOverlayPanelRect();
    const int panelX = static_cast<int>(panel.x);
    const int panelY = static_cast<int>(panel.y);
    const int panelW = static_cast<int>(panel.width);
    const int panelH = static_cast<int>(panel.height);

    DrawRectangle(0, 0, GameViewport::kVirtualWidth, GameViewport::kVirtualHeight,
                  Color{0, 0, 0, 140});
    DrawRectangle(panelX, panelY, panelW, panelH, Color{28, 30, 38, 255});
    DrawRectangleLines(panelX, panelY, panelW, panelH, Color{82, 88, 104, 255});
    DrawText(TextFormat("Team Level %d - pick your skill (tier %d)", session.teamLevel, pickTier),
             panelX + 20, panelY + 20, 20, RAYWHITE);
    DrawText("Each player chooses their own branch",
             panelX + 20, panelY + 48, 16, GRAY);

    DrawUiButton("DPS", BranchVoteButtonRect(0));
    DrawUiButton("Shield", BranchVoteButtonRect(1));
    DrawUiButton("Support", BranchVoteButtonRect(2));
}

static void DrawSkillEffects() {
    if (!gThunderVfx.loaded) {
        return;
    }

    const uint32_t tick = DisplayServerTick();
    for (const net::SkillEffectState& effect : gClient.GetSession().skillEffects) {
        const net::SkillId skillId = net::SkillIdFromInt(effect.skillId);
        const int frame = SkillEffectFrame(tick, effect.startTick);
        const Vector2 center = {
            net::CellCenterX(effect.col),
            net::CellCenterY(effect.row),
        };
        gThunderVfx.Draw(center, SkillTintFor(skillId), frame, true, 96.0f);
    }
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
            gAudio.OnDisconnected();
            gStatusText = detail.empty() ? "Disconnected" : detail;
            gEditingName = true;
            gChatExpanded = false;
            gChatInput.clear();
            gSpectating = false;
            gSpectateTargetId = -1;
            ResetClientVisualState();
            gOptionsOpen = false;
            ResetNameInputFocus();
            break;
        case net::ClientConnectionState::Connecting:
            gStatusText = detail;
            break;
        case net::ClientConnectionState::Joined:
            gAudio.OnJoined();
            gAudio.SeedSkillEffects(gClient.GetSession().skillEffects);
            UpdateStatusText();
            gEditingName = false;
            gChatExpanded = false;
            gChatInput.clear();
            gSpectating = false;
            gSpectateTargetId = -1;
            gOptionsOpen = false;
            gLocalColor = ColorForPlayer(gClient.GetLocalPlayerId());
            ResetNameInputFocus();
            break;
        case net::ClientConnectionState::Rejected:
            gStatusText = "Rejected: " + detail;
            gEditingName = true;
            gChatExpanded = false;
            gChatInput.clear();
            ResetClientVisualState();
            gOptionsOpen = false;
            ResetNameInputFocus();
            break;
    }
}

static bool ConnectToServer() {
#if defined(PLATFORM_WEB)
    if (gNameInputFocused && gNameUsesWebInput) {
        gPlayerName = WebTextInputGetValue();
    }
    WebTextInputHide();
    gNameInputFocused = false;
    gNameUsesWebInput = false;
    gChatUsesWebInput = false;
#endif
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
        for (const std::pair<int, int>& cell : net::EnemyOccupiedCells(enemy)) {
            if (cell.first == col && cell.second == row) {
                return enemy.id;
            }
        }
    }
    return std::nullopt;
}

static std::optional<int> FindEnemyAtWorldPos(Vector2 worldPos) {
    std::optional<int> bestId;
    float bestDistSq = net::kGridCellSize * net::kGridCellSize;

    for (const net::EnemyState& enemy : gClient.GetEnemies()) {
        if (enemy.state == net::EntityState::Dead) {
            continue;
        }

        const net::EntityDef& def = net::DefaultEntityRegistry().MustFind(enemy.kind);
        const float hitRadius = std::max(net::kGridCellSize * 0.5f,
                                         static_cast<float>(def.spriteHeight) * 0.35f);
        const Vector2 center = DisplayPositionForEnemy(enemy);
        const float dx = center.x - worldPos.x;
        const float dy = center.y - worldPos.y;
        const float distSq = dx * dx + dy * dy;
        if (distSq <= hitRadius * hitRadius &&
            (!bestId.has_value() || distSq < bestDistSq)) {
            bestDistSq = distSq;
            bestId = enemy.id;
        }
    }

    return bestId;
}

static bool HasEnemyInSkillArea(int col, int row, int aoeRadius) {
    for (const net::EnemyState& enemy : gClient.GetEnemies()) {
        if (enemy.state == net::EntityState::Dead) {
            continue;
        }
        if (net::IsEnemyInSkillArea(enemy, col, row, aoeRadius)) {
            return true;
        }
    }
    return false;
}

static void HandleMapClick() {
    if (!gPointer.WasPressed()) {
        return;
    }
    if (gWorldView.IsPanning() || gWorldView.IsPinching() || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        return;
    }

    const Vector2 screenPos = GetMousePosition();
    if (!gViewport.ContainsScreenPoint(screenPos)) {
        return;
    }

    const Vector2 virtualPos = GetVirtualMousePosition();
    if (gOptionsOpen) {
        return;
    }
    if (IsMouseOverChatPanel(virtualPos) || IsMouseOverLevelUpOverlay(virtualPos)) {
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
    if (!net::IsValidCell(col, row)) {
        return;
    }

    const Vector2 worldPos = gWorldView.VirtualToWorld(virtualPos);

    if (GetLocalScene() == net::SceneId::Hub && net::IsPortalCell(col, row)) {
        const net::SessionSnapshot& session = gClient.GetSession();
        if (session.phase == net::SessionPhase::ArenaActive && session.arenaPlayerCount > 0) {
            if (CanLocalRejoinArena()) {
                gClient.SendRejoinArena();
            }
            return;
        }
        const net::PlayerState* local = FindLocalPlayer();
        const bool newReady = local == nullptr || !local->isReady;
        gClient.SendSetReady(newReady);
        return;
    }

    if (GetLocalScene() == net::SceneId::Hub && net::IsResetArenaCell(col, row)) {
        if (!IsArenaSessionRunning()) {
            return;
        }
        const net::PlayerState* local = FindLocalPlayer();
        const bool newSelected = local == nullptr || !local->wantsArenaReset;
        gClient.SendSetArenaReset(newSelected);
        return;
    }

    if (IsLocalPlayerDead()) {
        return;
    }

    if (gPendingSkillId >= 0 && GetLocalScene() == net::SceneId::Arena) {
        const net::SkillDef& def = net::SkillDefFor(net::SkillIdFromInt(gPendingSkillId));
        int targetCol = col;
        int targetRow = row;
        if (def.damage > 0) {
            const std::pair<int, int> resolved = ResolveDamageSkillTarget(col, row, worldPos);
            targetCol = resolved.first;
            targetRow = resolved.second;
            if (!HasEnemyInSkillArea(targetCol, targetRow, def.aoeRadius)) {
                return;
            }
        }

        const net::PlayerState* local = FindLocalPlayer();
        if (local == nullptr ||
            !net::IsCellInSkillRange(net::WorldToCellCol(local->x), net::WorldToCellRow(local->y),
                                     targetCol, targetRow, def.rangeCells)) {
            return;
        }

        gClient.SendUseSkill(gPendingSkillId, targetCol, targetRow);
        gPendingSkillId = -1;
        return;
    }

    if (!GetActiveMap().IsWalkable(col, row)) {
        return;
    }

    if (GetLocalScene() == net::SceneId::Arena) {
        if (const std::optional<int> enemyId = FindEnemyAtWorldPos(worldPos)) {
            gClient.SendAttackRequest(*enemyId);
            return;
        }
        if (const std::optional<int> enemyId = FindEnemyAtCell(col, row)) {
            gClient.SendAttackRequest(*enemyId);
            return;
        }
    }

    gClient.SendMoveRequest(col, row);
}

static void HandleUiClicks() {
    if (!gPointer.WasPressed()) {
        return;
    }

    const Vector2 screenPos = GetMousePosition();
    if (!gViewport.ContainsScreenPoint(screenPos)) {
        return;
    }

    if (gOptionsOpen) {
        return;
    }

    if (WasUiButtonPressed(ChatToggleButtonRect())) {
        if (gChatExpanded) {
            gChatExpanded = false;
            gChatInput.clear();
#if defined(PLATFORM_WEB)
            gChatUsesWebInput = false;
            WebTextInputHide();
#endif
        } else {
            gChatExpanded = true;
#if defined(PLATFORM_WEB)
            gChatUsesWebInput = gPointer.UsesTouch();
            if (gChatUsesWebInput) {
                ShowChatWebInput();
            }
#endif
        }
        return;
    }

#if defined(PLATFORM_WEB)
    if (gChatExpanded) {
        const Vector2 pointerPos = GetVirtualMousePosition();
        const bool touchesChatInput = CheckCollisionPointRec(pointerPos, ChatInputFieldRect());
        if (touchesChatInput) {
            gChatUsesWebInput = gPointer.UsesTouch();
            if (gChatUsesWebInput) {
                ShowChatWebInput();
            } else {
                WebTextInputHide();
            }
            return;
        }

        if (!CheckCollisionPointRec(pointerPos, ChatPanelRect())) {
            gChatUsesWebInput = false;
            WebTextInputHide();
        }
    }
#endif

    if (gClient.GetState() == net::ClientConnectionState::Joined &&
        WasUiButtonPressed(DisengageButtonRect(), IsLocalPlayerInCombat())) {
        gClient.SendDisengage();
        return;
    }

    const net::SessionSnapshot& session = gClient.GetSession();
    if (LocalPlayerNeedsSkillPick()) {
        if (WasUiButtonPressed(BranchVoteButtonRect(0))) {
            gClient.SendVoteSkillBranch(net::SkillBranch::Dps);
            return;
        }
        if (WasUiButtonPressed(BranchVoteButtonRect(1))) {
            gClient.SendVoteSkillBranch(net::SkillBranch::Shield);
            return;
        }
        if (WasUiButtonPressed(BranchVoteButtonRect(2))) {
            gClient.SendVoteSkillBranch(net::SkillBranch::Support);
            return;
        }
    }

    if (gClient.GetState() == net::ClientConnectionState::Joined &&
        GetLocalScene() == net::SceneId::Arena && !IsLocalPlayerDead()) {
        const net::PlayerState* local = FindLocalPlayer();
        if (local != nullptr) {
            int skillButtonIndex = 0;
            for (int skillId : local->unlockedSkillIds) {
                if (WasUiButtonPressed(SkillButtonRect(skillButtonIndex),
                                       !IsSkillOnCooldown(skillId))) {
                    if (gPendingSkillId == skillId) {
                        gPendingSkillId = -1;
                    } else {
                        BeginSkillTargeting(net::SkillIdFromInt(skillId));
                    }
                    return;
                }
                ++skillButtonIndex;
            }
        }
    }

    if (gSpectating && GetLocalScene() == net::SceneId::Arena && IsLocalPlayerDead()) {
        if (WasUiButtonPressed(SpectatePrevButtonRect(), CollectSpectateTargets().size() > 1)) {
            CycleSpectateTarget(-1);
            UpdateSpectateCamera();
            return;
        }
        if (WasUiButtonPressed(SpectateNextButtonRect(), CollectSpectateTargets().size() > 1)) {
            CycleSpectateTarget(1);
            UpdateSpectateCamera();
            return;
        }
        if (WasUiButtonPressed(SpectateReturnButtonRect())) {
            StopSpectating();
            gClient.SendReturnToHub();
            return;
        }
    }

    if (ShouldShowDeathPanel()) {
        if (WasUiButtonPressed(RespawnInArenaButtonRect(), CanLocalRespawnInArena())) {
            StopSpectating();
            gClient.SendRespawnInArena();
            return;
        }
        if (WasUiButtonPressed(ReturnToHubButtonRect())) {
            StopSpectating();
            gClient.SendReturnToHub();
            return;
        }
        if (WasUiButtonPressed(SpectateButtonRect(), GetSpectateTarget() != nullptr)) {
            BeginSpectating();
            return;
        }
    }
}

struct OptionsPanelLayout {
    int panelX = 0;
    int panelY = 0;
    int panelW = 260;
    int panelH = 180;
    Rectangle resumeBtn{};
    Rectangle returnToHubBtn{};
    Rectangle exitBtn{};
    bool showReturnToHub = false;
};

static OptionsPanelLayout BuildOptionsPanelLayout() {
    OptionsPanelLayout layout;
    layout.showReturnToHub = gClient.GetState() == net::ClientConnectionState::Joined &&
                             GetLocalScene() == net::SceneId::Arena;
    layout.panelH = layout.showReturnToHub ? 232 : 180;
    layout.panelX = (GameViewport::kVirtualWidth - layout.panelW) / 2;
    layout.panelY = (GameViewport::kVirtualHeight - layout.panelH) / 2;
    layout.resumeBtn = {static_cast<float>(layout.panelX + 30),
                        static_cast<float>(layout.panelY + 52), 200.0f, 32.0f};
    if (layout.showReturnToHub) {
        layout.returnToHubBtn = {static_cast<float>(layout.panelX + 30),
                                 static_cast<float>(layout.panelY + 102), 200.0f, 32.0f};
        layout.exitBtn = {static_cast<float>(layout.panelX + 30),
                          static_cast<float>(layout.panelY + 152), 200.0f, 32.0f};
    } else {
        layout.exitBtn = {static_cast<float>(layout.panelX + 30),
                          static_cast<float>(layout.panelY + 102), 200.0f, 32.0f};
    }
    return layout;
}

static void HandleOptionsInput() {
    if (!gOptionsOpen) {
        return;
    }

    const OptionsPanelLayout layout = BuildOptionsPanelLayout();

    if (WasUiButtonPressed(layout.resumeBtn)) {
        gOptionsOpen = false;
    }

    if (layout.showReturnToHub && WasUiButtonPressed(layout.returnToHubBtn)) {
        StopSpectating();
        gClient.SendReturnToHub();
        gOptionsOpen = false;
    }

    if (WasUiButtonPressed(layout.exitBtn)) {
        if (gClient.GetState() == net::ClientConnectionState::Joined) {
            gClient.Disconnect();
        }
        CloseWindow();
    }
}

static void DrawGridTiles() {
    const net::GridMap& map = GetActiveMap();
    const Color wallColor = Color{90, 94, 104, 255};
    const Color propColor = Color{220, 130, 45, 255};
    const bool inHub = GetLocalScene() == net::SceneId::Hub;

    for (int row = 0; row < net::kGridRows; ++row) {
        for (int col = 0; col < net::kGridCols; ++col) {
            const net::TileType tile = map.Get(col, row);
            if (tile == net::TileType::Empty || tile == net::TileType::Enemy) {
                if (inHub && net::IsPortalCell(col, row)) {
                    DrawRectangleRec(CellWorldRect(col, row), Color{70, 120, 255, 220});
                } else if (inHub && net::IsResetArenaCell(col, row)) {
                    const Color resetColor = IsArenaSessionRunning()
                                                 ? Color{235, 90, 60, 220}
                                                 : Color{120, 70, 55, 120};
                    DrawRectangleRec(CellWorldRect(col, row), resetColor);
                }
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
    if (gOptionsOpen) {
        return;
    }

    const Vector2 screenPos = GetMousePosition();
    if (!gViewport.ContainsScreenPoint(screenPos)) {
        return;
    }

    const Vector2 virtualPos = GetVirtualMousePosition();
    int hoverCol = 0;
    int hoverRow = 0;
    if (!IsMouseOverChatPanel(virtualPos) &&
        TryGetWorldCellFromVirtual(virtualPos, hoverCol, hoverRow)) {
        const net::GridMap& map = GetActiveMap();
        Color hoverColor = map.IsWalkable(hoverCol, hoverRow)
                               ? Color{90, 100, 130, 60}
                               : Color{180, 80, 80, 50};
        if (GetLocalScene() == net::SceneId::Hub && net::IsPortalCell(hoverCol, hoverRow)) {
            hoverColor = Color{100, 160, 255, 90};
        } else if (GetLocalScene() == net::SceneId::Hub &&
                   net::IsResetArenaCell(hoverCol, hoverRow)) {
            hoverColor = IsArenaSessionRunning() ? Color{255, 120, 80, 90}
                                                 : Color{160, 90, 70, 50};
        } else if (gPendingSkillId >= 0 && GetLocalScene() == net::SceneId::Arena) {
            const net::SkillId skill = net::SkillIdFromInt(gPendingSkillId);
            const net::SkillDef& def = net::SkillDefFor(skill);
            const net::PlayerState* local = FindLocalPlayer();
            bool inRange = false;
            if (local != nullptr) {
                inRange = net::IsCellInSkillRange(net::WorldToCellCol(local->x),
                                                  net::WorldToCellRow(local->y), hoverCol,
                                                  hoverRow, def.rangeCells);
            }
            hoverColor = inRange ? Color{120, 200, 255, 90} : Color{180, 80, 80, 70};
        }
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
#if defined(PLATFORM_WEB)
    if (gChatUsesWebInput) {
        ShowChatWebInput();
        gChatInput = WebTextInputGetValue();
        if (WebTextInputConsumeSubmit() && !gChatInput.empty()) {
            gClient.SendChat(gChatInput);
            gChatInput.clear();
        }
    }
#endif

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
    gAudio.Update();
    if (const net::PlayerState* local = FindLocalPlayer()) {
        gAudio.UpdateLocalPlayer(*local, gClient.GetServerTick());
    }
    gAudio.UpdateSkillEffects(gClient.GetSession().skillEffects);
    SyncEntityVisuals();
    UpdateStatusText();

    if (GetLocalScene() != net::SceneId::Arena || !IsLocalPlayerDead()) {
        StopSpectating();
    }
    UpdateSpectateCamera();

    const std::vector<Vector2> virtualTouches = CollectVirtualTouchPoints();
    const Vector2 virtualMouse = GetVirtualPointerPosition(virtualTouches);
    const bool blockGameplayInput =
        ShouldShowDeathPanel() || IsArenaWipePending() || IsArenaVictoryActive() ||
        (gSpectating && GetLocalScene() == net::SceneId::Arena && IsLocalPlayerDead());
    const bool allowCameraInput =
        !gEditingName && !gChatExpanded && !IsMouseOverActionsPanel(virtualMouse) &&
        !blockGameplayInput && !gOptionsOpen;
    gPointer.Update(gViewport, gWorldView.BlockTouchTap());
    gWorldView.UpdateInput(virtualMouse, allowCameraInput, virtualTouches);

#if defined(PLATFORM_WEB)
    if (!gEditingName && gChatExpanded && !gOptionsOpen && !virtualTouches.empty()) {
        bool touchingChatInput = false;
        const Rectangle chatInput = ChatInputFieldRect();
        for (const Vector2 touch : virtualTouches) {
            if (CheckCollisionPointRec(touch, chatInput)) {
                touchingChatInput = true;
                break;
            }
        }
        if (touchingChatInput) {
            gChatUsesWebInput = true;
            ShowChatWebInput();
        }
    }
#endif

    HandleSpectateInput();

    if (IsKeyPressed(KEY_R) && allowCameraInput) {
        gWorldView.Reset();
    }

    if (IsKeyPressed(KEY_F11)) {
        ToggleFullscreen();
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        if (gPendingSkillId >= 0) {
            gPendingSkillId = -1;
        } else if (gChatExpanded) {
            gChatExpanded = false;
            gChatInput.clear();
#if defined(PLATFORM_WEB)
            gChatUsesWebInput = false;
            WebTextInputHide();
#endif
        }
    }

    HandleMuteButtonClick();
    HandleOptionsButtonClick();

    if (gEditingName) {
        HandlePlayerNameInput();
        HandleOptionsInput();
        if (gOptionsOpen) {
            return;
        }
        return;
    }

    if (gChatExpanded) {
        HandleChatInput();
    }

    HandleOptionsInput();
    if (gOptionsOpen) {
        return;
    }

    HandleUiClicks();
    if (!blockGameplayInput) {
        HandleMapClick();
    }
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

static void DrawOptionsPanel() {
    if (!gOptionsOpen) {
        return;
    }

    const OptionsPanelLayout layout = BuildOptionsPanelLayout();

    DrawRectangle(0, 0, GameViewport::kVirtualWidth, GameViewport::kVirtualHeight,
                  Color{0, 0, 0, 120});

    DrawRectangle(layout.panelX, layout.panelY, layout.panelW, layout.panelH,
                  Color{28, 30, 38, 255});
    DrawRectangleLines(layout.panelX, layout.panelY, layout.panelW, layout.panelH,
                       Color{82, 88, 104, 255});
    DrawText("Options", layout.panelX + 16, layout.panelY + 12, 22, RAYWHITE);

    DrawUiButton("Resume", layout.resumeBtn);
    if (layout.showReturnToHub) {
        DrawUiButton("Return to Hub", layout.returnToHubBtn);
    }
    DrawUiButton("Exit Game", layout.exitBtn);
}

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
    const Vector2 center = DisplayPositionForPlayer(player);
    const EntityVisualState* visual = nullptr;
    if (const auto it = gPlayerVisuals.find(player.id); it != gPlayerVisuals.end()) {
        visual = &it->second;
    }
    const bool combatHitFlash =
        player.state == net::EntityState::Combat && IsCombatHitFlashing(visual);
    net::PlayerState displayPlayer = player;
    if (IsLoopingAnim(player.anim)) {
        displayPlayer.animStartTick = 0;
    }
    const uint32_t displayTick = IsLoopingAnim(player.anim)
                                     ? LoopingAnimTick(visual, DisplayServerTick(),
                                                       player.animStartTick)
                                     : DisplayServerTick();
    gPlayerSprites.Draw(displayPlayer, displayTick, center, color, combatHitFlash);
    DrawPlayerBars(player, center);
}

static void DrawPlayerName(const net::PlayerState& player, float nameOffsetY) {
    const Vector2 displayPos = DisplayPositionForPlayer(player);
    const Vector2 virtualCenter = gWorldView.WorldToVirtual(displayPos);
    std::string label = player.name;
    if (player.sceneId == net::SceneId::Hub && player.isReady) {
        label += " [ready]";
    }
    if (player.sceneId == net::SceneId::Hub && player.wantsArenaReset) {
        label += " [reset]";
    }
    DrawText(label.c_str(),
             static_cast<int>(virtualCenter.x - 24.0f),
             static_cast<int>(virtualCenter.y - nameOffsetY),
             16, RAYWHITE);
}

static void DrawEnemy(const net::EnemyState& enemy) {
    const Vector2 center = DisplayPositionForEnemy(enemy);
    const EntityVisualState* visual = nullptr;
    if (const auto it = gEnemyVisuals.find(enemy.id); it != gEnemyVisuals.end()) {
        visual = &it->second;
    }
    const bool combatHitFlash =
        enemy.state == net::EntityState::Combat && IsCombatHitFlashing(visual);
    net::EnemyState displayEnemy = enemy;
    if (IsLoopingAnim(enemy.anim)) {
        displayEnemy.animStartTick = 0;
    }
    const uint32_t displayTick = IsLoopingAnim(enemy.anim)
                                     ? LoopingAnimTick(visual, DisplayServerTick(),
                                                       enemy.animStartTick)
                                     : DisplayServerTick();
    gGoblinSprites.Draw(displayEnemy, displayTick, center, combatHitFlash);
    if (enemy.state != net::EntityState::Dead) {
        const net::EntityDef& def = net::DefaultEntityRegistry().MustFind(enemy.kind);
        DrawEntityHpBar(center, enemy.hp, def.stats.maxHp, def.spriteHeight);
    }
}

static void DrawEnemies() {
    for (const net::EnemyState& enemy : gClient.GetEnemies()) {
        if (enemy.kind == net::kGoblinEntityId || enemy.kind == net::kGoblinBossEntityId) {
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
    DrawCombatTargetHighlights();
    DrawSkillEffects();
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

    if (gSpectating) {
        if (const net::PlayerState* target = GetSpectateTarget()) {
            DrawCircleLines(target->x, target->y, net::kPlayerRadius + 8.0f,
                            Color{255, 220, 80, 255});
            DrawCircleLines(target->x, target->y, net::kPlayerRadius + 10.0f,
                            Color{255, 220, 80, 120});
        }
    }

    EndMode2D();
    EndScissorMode();

    DrawFloatingDamage();

    const float nameOffsetY = gPlayerSprites.AnyLoaded()
                                  ? kPlayerSpriteHeight * 0.5f + 8.0f
                                  : net::kPlayerRadius + 22.0f;

    for (const net::PlayerState& player : gClient.GetPlayers()) {
        DrawPlayerName(player, nameOffsetY);
    }

    DrawText(gStatusText.c_str(), 20, 52, 18, LIGHTGRAY);
    DrawActionsPanel();

    if (gEditingName) {
        DrawPlayerNameEntry();
    } else {
        DrawChatPanel();
    }

    DrawOptionsPanel();

    DrawDeathPanel();
    DrawSpectateHud();
    DrawArenaWipeOverlay();
    DrawVictoryOverlay();
    DrawLevelUpOverlay();

    DrawMuteButton();
    DrawOptionsButton();

    gViewport.EndFrame();
}

#if defined(PLATFORM_WEB)
static void MainLoop() {
    const double frameStart = GetTime();
    UpdateGame();
    DrawGame();
}

int main() {
    InitGameWindow("Multiplayer Game");
    SetGesturesEnabled(GESTURE_PINCH_IN | GESTURE_PINCH_OUT | GESTURE_DRAG);
    net::InitializeEntityRegistry();
    gAudio.Init();
    gViewport.Init();
    gPlayerSprites.Load();
    gGoblinSprites.Load();
    gThunderVfx.Load(ResolveAssetPath(kThunderVfxPath).c_str(), net::kThunderVfxFrameCount);
    emscripten_set_main_loop(MainLoop, 0, 1);
    gThunderVfx.Unload();
    gGoblinSprites.Unload();
    gPlayerSprites.Unload();
    gViewport.Shutdown();
    gAudio.Shutdown();
    CloseWindow();
    return 0;
}
#else
int main() {
    InitGameWindow("Multiplayer Game");
    SetGesturesEnabled(GESTURE_PINCH_IN | GESTURE_PINCH_OUT | GESTURE_DRAG);
    net::InitializeEntityRegistry();
    gAudio.Init();
    gViewport.Init();
    gPlayerSprites.Load();
    gGoblinSprites.Load();
    gThunderVfx.Load(ResolveAssetPath(kThunderVfxPath).c_str(), net::kThunderVfxFrameCount);

    while (!WindowShouldClose()) {
        UpdateGame();
        DrawGame();
    }

    gClient.Disconnect();
    gThunderVfx.Unload();
    gGoblinSprites.Unload();
    gPlayerSprites.Unload();
    gViewport.Shutdown();
    gAudio.Shutdown();
    CloseWindow();
    return 0;
}
#endif
