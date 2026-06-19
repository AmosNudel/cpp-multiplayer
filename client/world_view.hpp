#pragma once

#include "raylib.h"

#include <vector>

struct WorldView {
    static constexpr float kGridVirtualX = 80.0f;
    static constexpr float kGridVirtualY = 168.0f;
    static constexpr float kMinZoom = 0.5f;
    static constexpr float kMaxZoom = 2.5f;
    static constexpr float kTouchPanThreshold = 12.0f;

    void Reset();
    void SetTarget(Vector2 worldPos);
    void UpdateInput(Vector2 virtualMousePos, bool allowInput,
                     const std::vector<Vector2>& virtualTouchPoints = {});
    Camera2D BuildCamera() const;
    Vector2 VirtualToWorld(Vector2 virtualPos) const;
    Vector2 WorldToVirtual(Vector2 worldPos) const;
    Rectangle GridVirtualRect() const;
    float Zoom() const { return zoom_; }
    bool IsPanning() const { return panning_ || touchPanning_; }
    bool IsPinching() const { return pinching_; }
    bool BlockTouchTap() const { return touchBlockedTapLatch_; }

private:
    void ClampTarget();
    void ApplyZoomAt(Vector2 virtualAnchor, float newZoom);
    void UpdateMouseInput(Vector2 virtualMousePos, bool allowInput);
    void UpdateTouchInput(const std::vector<Vector2>& virtualTouchPoints, bool allowInput);

    float zoom_ = 1.0f;
    Vector2 target_{400.0f, 300.0f};
    bool panning_ = false;
    Vector2 panStartMouse_{};
    Vector2 panStartTarget_{};
    bool touchPanning_ = false;
    Vector2 touchPanStart_{};
    Vector2 touchPanStartTarget_{};
    bool pinching_ = false;
    float lastPinchDistance_ = 0.0f;
    bool touchBlockedTapLatch_ = false;
};
