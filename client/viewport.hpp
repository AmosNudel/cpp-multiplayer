#pragma once

#include "raylib.h"

struct GameViewport {
    static constexpr int kVirtualWidth = 960;
    static constexpr int kHudHeight = 168;
    static constexpr int kBottomBarHeight = 192;
    static constexpr int kVirtualHeight = kHudHeight + 600 + kBottomBarHeight;

    void Init();
    void Shutdown();
    void UpdateLayout();
    void BeginFrame();
    void EndFrame();

    Vector2 ScreenToVirtual(Vector2 screenPos) const;
    bool ContainsScreenPoint(Vector2 screenPos) const;

private:
    RenderTexture2D target_{};
    Rectangle dest_{};
    float scale_ = 1.0f;
};

void InitGameWindow(const char* title);
