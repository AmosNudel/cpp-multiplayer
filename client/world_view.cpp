#include "client/world_view.hpp"

#include <algorithm>

#include "common/config.hpp"

void WorldView::Reset() {
    zoom_ = 1.0f;
    target_ = {net::kWorldWidth * 0.5f, net::kWorldHeight * 0.5f};
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

void WorldView::UpdateInput(Vector2 virtualMousePos, bool allowZoom) {
    if (!allowZoom) {
        return;
    }

    const float wheel = GetMouseWheelMove();
    if (wheel == 0.0f || !CheckCollisionPointRec(virtualMousePos, GridVirtualRect())) {
        return;
    }

    const Camera2D before = BuildCamera();
    const Vector2 worldAnchor = GetScreenToWorld2D(virtualMousePos, before);

    zoom_ = std::clamp(zoom_ + wheel * 0.12f, kMinZoom, kMaxZoom);

    const Camera2D after = BuildCamera();
    const Vector2 worldAfter = GetScreenToWorld2D(virtualMousePos, after);
    target_.x += worldAnchor.x - worldAfter.x;
    target_.y += worldAnchor.y - worldAfter.y;
    ClampTarget();
}
