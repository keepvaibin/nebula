#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

/// @brief The camera's viewing volume per far clip level, remade each frame. Level 0 ends at the camera's far Z; the
/// others at fixed distances (setClippingFar50m and its kin pick them).
class ClippingJudge : public NameObj {
public:
    static const s32 cFarClipLevelNum = 8;

    ClippingJudge(const char*);

    virtual void movement() override;

    bool isJudgedToClipFrustum(const Vec3f& rPos, f32 radius) const;
    bool isJudgedToClipFrustum(const Vec3f& rPos, f32 radius, s32 farClipLevel) const;
    /// How far outside the view a stopped actor stays drawn this frame: 0, or while the host traces the margin that
    /// keeps what the camera does not see in the tracer's shadows and mirrors.
    f32 getTracedDrawMargin() const { return mTracedDrawMargin; }

private:
    void calcViewingVolumes(f32 farZ);

    Hex3f mFrustums[cFarClipLevelNum];
    f32 mTracedDrawMargin;
};

namespace MR {
    ClippingJudge* getClippingJudge();
};  // namespace MR
