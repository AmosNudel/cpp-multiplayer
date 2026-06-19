#include "client/pointer_input.hpp"

#include "client/viewport.hpp"

#include <cmath>

void PointerInput::Update(const GameViewport& viewport, bool blockTouchTap) {
    wasPressed_ = false;
    virtualTouchPoints_.clear();

    const int touchCount = GetTouchPointCount();
    for (int i = 0; i < touchCount; ++i) {
        virtualTouchPoints_.push_back(viewport.ScreenToVirtual(GetTouchPosition(i)));
    }

    usesTouch_ = touchCount > 0 || (prevTouchCount_ > 0 && touchCount == 0);

    if (touchCount > 0) {
        virtualPos_ = virtualTouchPoints_[0];
        isDown_ = true;

        if (!trackingTouch_) {
            trackingTouch_ = true;
            touchStart_ = virtualPos_;
            touchMoved_ = false;
        } else if (!touchMoved_) {
            const float dx = virtualPos_.x - touchStart_.x;
            const float dy = virtualPos_.y - touchStart_.y;
            if (std::sqrt(dx * dx + dy * dy) > kTapThreshold) {
                touchMoved_ = true;
            }
        }
    } else {
        if (prevTouchCount_ > 0 && trackingTouch_) {
            const bool tap = !touchMoved_ && !blockTouchTap;
            wasPressed_ = tap;
            if (tap) {
                virtualPos_ = touchStart_;
            }
        }

        trackingTouch_ = false;
        touchMoved_ = false;
        isDown_ = false;

        if (!usesTouch_) {
            virtualPos_ = viewport.ScreenToVirtual(GetMousePosition());
            isDown_ = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
            wasPressed_ = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        }
    }

    if (touchCount == 0 && !usesTouch_) {
        virtualPos_ = viewport.ScreenToVirtual(GetMousePosition());
    }

    prevTouchCount_ = touchCount;
}
