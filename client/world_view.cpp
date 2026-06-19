#include "client/world_view.hpp"

#include <algorithm>
#include <cmath>

#include "common/config.hpp"

namespace {
float Distance(Vector2 a, Vector2 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}
}  // namespace

void WorldView::Reset() {
    zoom_ = 1.0f;
    target_ = {net::kWorldWidth * 0.5f, net::kWorldHeight * 0.5f};
    panning_ = false;
    touchPanning_ = false;
    pinching_ = false;
    lastPinchDistance_ = 0.0f;
    touchBlockedTapLatch_ = false;
}

void WorldView::SetTarget(Vector2 worldPos) {
    target_ = worldPos;
    ClampTarget();
}

void WorldView::ClampTarget() {
    const float margin = 40.0f;
    target_.x = std::clamp(target_.x, margin, net::kWorldWidth - margin);
    target_.y = std::clamp(target_.y, margin, net::kWorldHeight - margin);
}

Rectangle WorldView::GridVirtualRect() const {
    return {kGridVirtualX, kGridVirtualY, net::kWorldWidth, net::kWorldHeight};
}

Camera2D WorldView::BuildCamera() const {
    const Rectangle grid = GridVirtualRect();
    return {
        .offset = {grid.x + grid.width * 0.5f, grid.y + grid.height * 0.5f},
        .target = target_,
        .rotation = 0.0f,
        .zoom = zoom_,
    };
}

Vector2 WorldView::VirtualToWorld(Vector2 virtualPos) const {
    return GetScreenToWorld2D(virtualPos, BuildCamera());
}

Vector2 WorldView::WorldToVirtual(Vector2 worldPos) const {
    return GetWorldToScreen2D(worldPos, BuildCamera());
}

void WorldView::ApplyZoomAt(Vector2 virtualAnchor, float newZoom) {
    const Camera2D before = BuildCamera();
    const Vector2 worldAnchor = GetScreenToWorld2D(virtualAnchor, before);

    zoom_ = std::clamp(newZoom, kMinZoom, kMaxZoom);

    const Camera2D after = BuildCamera();
    const Vector2 worldAfter = GetScreenToWorld2D(virtualAnchor, after);
    target_.x += worldAnchor.x - worldAfter.x;
    target_.y += worldAnchor.y - worldAfter.y;
    ClampTarget();
}

void WorldView::UpdateMouseInput(Vector2 virtualMousePos, bool allowInput) {
    if (IsMouseButtonReleased(MOUSE_BUTTON_MIDDLE)) {
        panning_ = false;
    }

    if (!allowInput) {
        return;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) &&
        CheckCollisionPointRec(virtualMousePos, GridVirtualRect())) {
        panning_ = true;
        panStartMouse_ = virtualMousePos;
        panStartTarget_ = target_;
    }

    if (panning_ && IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        const Vector2 delta = {
            virtualMousePos.x - panStartMouse_.x,
            virtualMousePos.y - panStartMouse_.y,
        };
        target_.x = panStartTarget_.x - delta.x / zoom_;
        target_.y = panStartTarget_.y - delta.y / zoom_;
        ClampTarget();
        return;
    }

    const float wheel = GetMouseWheelMove();
    if (wheel == 0.0f || !CheckCollisionPointRec(virtualMousePos, GridVirtualRect())) {
        return;
    }

    ApplyZoomAt(virtualMousePos, zoom_ + wheel * 0.12f);
}

void WorldView::UpdateTouchInput(const std::vector<Vector2>& virtualTouchPoints, bool allowInput) {
    if (!allowInput || virtualTouchPoints.empty()) {
        touchPanning_ = false;
        pinching_ = false;
        lastPinchDistance_ = 0.0f;
        return;
    }

    if (virtualTouchPoints.size() >= 2) {
        touchPanning_ = false;
        touchBlockedTapLatch_ = true;

        const Vector2 center = {
            (virtualTouchPoints[0].x + virtualTouchPoints[1].x) * 0.5f,
            (virtualTouchPoints[0].y + virtualTouchPoints[1].y) * 0.5f,
        };
        const float pinchDistance = Distance(virtualTouchPoints[0], virtualTouchPoints[1]);

        if (!CheckCollisionPointRec(center, GridVirtualRect())) {
            pinching_ = false;
            lastPinchDistance_ = 0.0f;
            return;
        }

        if (pinching_ && lastPinchDistance_ > 0.0f) {
            const float scale = pinchDistance / lastPinchDistance_;
            ApplyZoomAt(center, zoom_ * scale);
        }

        pinching_ = true;
        lastPinchDistance_ = pinchDistance;
        return;
    }

    pinching_ = false;
    lastPinchDistance_ = 0.0f;

    const Vector2 touchPos = virtualTouchPoints[0];
    if (!CheckCollisionPointRec(touchPos, GridVirtualRect())) {
        touchPanning_ = false;
        return;
    }

    if (!touchPanning_) {
        touchPanStart_ = touchPos;
        touchPanStartTarget_ = target_;
        touchPanning_ = true;
        return;
    }

    const Vector2 delta = {
        touchPos.x - touchPanStart_.x,
        touchPos.y - touchPanStart_.y,
    };

    const float dragDistance = Distance(touchPos, touchPanStart_);
    if (dragDistance > kTouchPanThreshold) {
        touchBlockedTapLatch_ = true;
    }

    if (dragDistance <= kTouchPanThreshold) {
        return;
    }

    target_.x = touchPanStartTarget_.x - delta.x / zoom_;
    target_.y = touchPanStartTarget_.y - delta.y / zoom_;
    ClampTarget();
}

void WorldView::UpdateInput(Vector2 virtualMousePos, bool allowInput,
                            const std::vector<Vector2>& virtualTouchPoints) {
    if (virtualTouchPoints.empty()) {
        touchPanning_ = false;
        pinching_ = false;
        lastPinchDistance_ = 0.0f;
        touchBlockedTapLatch_ = false;
        UpdateMouseInput(virtualMousePos, allowInput);
        return;
    }

    UpdateTouchInput(virtualTouchPoints, allowInput);
}
