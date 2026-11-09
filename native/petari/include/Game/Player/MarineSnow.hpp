#pragma once

#include <nebula_geometry.hpp>

class GameTexture;

class MarineSnow {
public:
    MarineSnow();

    void view();
    void clear();
    void draw(const Vec3f& rPosition, const Vec3f& rUp, f32 surfaceDistance) const;

    u32 mParticleCount;
    Vec3f* mParticlePositions;
    f32 mCellSize;
    u16 mFadeStep;
    GameTexture* mTexture;
};
