#pragma once

#include <nebula_geometry.hpp>
#include "Game/Ui/Rect.hpp"

namespace Ui {
    class DrawInfo {
    public:
        DrawInfo();
        virtual ~DrawInfo();

        const Mtx34f& GetViewMtx() const { return mViewMtx; }
        bool IsMultipleViewMtxOnDraw() const { return mFlag.mulViewDraw; }
        void SetMultipleViewMtxOnDraw(bool bEnable) { mFlag.mulViewDraw = bEnable; }
        bool IsInfluencedAlpha() const { return mFlag.influencedAlpha; }
        void SetInfluencedAlpha(bool bEnable) { mFlag.influencedAlpha = bEnable; }
        bool IsLocationAdjust() const { return mFlag.locationAdjust; }
        void SetLocationAdjust(bool bEnable) { mFlag.locationAdjust = bEnable; }
        const Vec2f& GetLocationAdjustScale() const { return mLocationAdjustScale; }
        void SetLocationAdjustScale(const Vec2f& scale) { mLocationAdjustScale = scale; }
        f32 GetGlobalAlpha() const { return mGlobalAlpha; }
        bool IsInvisiblePaneCalculateMtx() const { return mFlag.invisiblePaneCalculateMtx; }
        bool IsYAxisUp() const { return mViewRect.bottom - mViewRect.top < 0.0f; }

        Mtx34f mViewMtx;
        Rect mViewRect;
        Vec2f mLocationAdjustScale;
        f32 mGlobalAlpha;

        struct {
            u8 mulViewDraw : 1;
            u8 influencedAlpha : 1;
            u8 locationAdjust : 1;
            u8 invisiblePaneCalculateMtx : 1;
        } mFlag;
    };
}  // namespace Ui
