#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

namespace Ui {
    struct Rect {
    public:
        Rect() : left(0), top(0), right(0), bottom(0) {
        }

        Rect(f32 l, f32 t, f32 r, f32 b) : left(l), top(t), right(r), bottom(b) {
        }

        ~Rect() {
        }

        f32 GetWidth() const {
            return right - left;
        }

        f32 GetHeight() const {
            return bottom - top;
        }

        void SetHeight(f32 height) {
            bottom = top + height;
        }

        void MoveTo(f32 x, f32 y) {
            right = x + GetWidth();
            left = x;
            bottom = y + GetHeight();
            top = y;
        }

        void Normalize() {
            const f32 l = left;
            const f32 t = top;
            const f32 r = right;
            const f32 b = bottom;

            left = r - l >= 0.0f ? l : r;
            right = r - l >= 0.0f ? r : l;
            top = b - t >= 0.0f ? t : b;
            bottom = b - t >= 0.0f ? b : t;
        }

        f32 left;
        f32 top;
        f32 right;
        f32 bottom;
    };
}  // namespace Ui
