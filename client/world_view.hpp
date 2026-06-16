#pragma once

#include "raylib.h"

struct WorldView {
    static constexpr float kGridVirtualX = 80.0f;
    static constexpr float kGridVirtualY = 168.0f;
    static constexpr float kMinZoom = 0.5f;
    static constexpr float kMaxZoom = 2.5f;

    void Reset();
    void UpdateInput(Vector2 virtualMousePos, bool allowZoom);
    Camera2D BuildCamera() const;
    Vector2 VirtualToWorld(Vector2 virtualPos) const;
    Vector2 WorldToVirtual(Vector2 worldPos) const;
    Rectangle GridVirtualRect() const;
    float Zoom() const { return zoom_; }

private:
    void ClampTarget();

    float zoom_ = 1.0f;
    Vector2 target_{400.0f, 300.0f};
};
