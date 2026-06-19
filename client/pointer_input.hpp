#pragma once

#include "raylib.h"

#include <vector>

struct GameViewport;

struct PointerInput {
    static constexpr float kTapThreshold = 12.0f;

    void Update(const GameViewport& viewport, bool blockTouchTap);

    Vector2 VirtualPosition() const { return virtualPos_; }
    bool WasPressed() const { return wasPressed_; }
    bool IsDown() const { return isDown_; }
    bool UsesTouch() const { return usesTouch_; }
    const std::vector<Vector2>& VirtualTouchPoints() const { return virtualTouchPoints_; }

private:
    Vector2 virtualPos_{};
    bool wasPressed_ = false;
    bool isDown_ = false;
    bool usesTouch_ = false;
    std::vector<Vector2> virtualTouchPoints_;

    bool trackingTouch_ = false;
    Vector2 touchStart_{};
    bool touchMoved_ = false;
    int prevTouchCount_ = 0;
};
