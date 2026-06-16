#include "client/viewport.hpp"

void InitGameWindow(const char* title) {
#if defined(PLATFORM_WEB)
    InitWindow(GameViewport::kVirtualWidth, GameViewport::kVirtualHeight, title);
#else
    const int monitor = GetCurrentMonitor();
    InitWindow(GetMonitorWidth(monitor), GetMonitorHeight(monitor), title);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
#endif
    SetTargetFPS(60);
}

void GameViewport::Init() {
    target_ = LoadRenderTexture(kVirtualWidth, kVirtualHeight);
    SetTextureFilter(target_.texture, TEXTURE_FILTER_POINT);
    UpdateLayout();
}

void GameViewport::Shutdown() {
    if (target_.id != 0) {
        UnloadRenderTexture(target_);
        target_ = {};
    }
}

void GameViewport::UpdateLayout() {
    const int windowW = GetScreenWidth();
    const int windowH = GetScreenHeight();
    if (windowW <= 0 || windowH <= 0) {
        return;
    }

    const float scaleX = static_cast<float>(windowW) / static_cast<float>(kVirtualWidth);
    const float scaleY = static_cast<float>(windowH) / static_cast<float>(kVirtualHeight);
    scale_ = scaleX < scaleY ? scaleX : scaleY;

    const float destW = static_cast<float>(kVirtualWidth) * scale_;
    const float destH = static_cast<float>(kVirtualHeight) * scale_;
    dest_ = {
        (static_cast<float>(windowW) - destW) * 0.5f,
        (static_cast<float>(windowH) - destH) * 0.5f,
        destW,
        destH,
    };
}

void GameViewport::BeginFrame() {
    if (IsWindowResized()) {
        UpdateLayout();
    }

    BeginTextureMode(target_);
    ClearBackground(Color{24, 26, 32, 255});
}

void GameViewport::EndFrame() {
    EndTextureMode();

    BeginDrawing();
    ClearBackground(BLACK);

    const Rectangle source = {
        0.0f,
        0.0f,
        static_cast<float>(target_.texture.width),
        -static_cast<float>(target_.texture.height),
    };
    DrawTexturePro(target_.texture, source, dest_, Vector2{0.0f, 0.0f}, 0.0f, WHITE);

    EndDrawing();
}

Vector2 GameViewport::ScreenToVirtual(Vector2 screenPos) const {
    if (scale_ <= 0.0f) {
        return screenPos;
    }

    return {
        (screenPos.x - dest_.x) / scale_,
        (screenPos.y - dest_.y) / scale_,
    };
}

bool GameViewport::ContainsScreenPoint(Vector2 screenPos) const {
    return CheckCollisionPointRec(screenPos, dest_);
}
