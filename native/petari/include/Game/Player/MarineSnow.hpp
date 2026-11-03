#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class JUTTexture;

class MarineSnow {
public:
    MarineSnow();

    void view();
    void clear();
    void draw(const TVec3f& rPosition, const TVec3f& rUp, f32 surfaceDistance) const;

    u32 mParticleCount;
    TVec3f* mParticlePositions;
    f32 mCellSize;
    u16 mFadeStep;
    JUTTexture* mTexture;
};
