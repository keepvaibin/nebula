#pragma once

#include "Game/Sound/SoundParams.hpp"
#include "Game/Sound/SoundAudibleParam.hpp"
#include <nebula_geometry.hpp>

class SoundDopplerAbsPos {
public:
    void init(Vec3f* pAudiblePos, const Vec3f& rPos, const Vec3f* pPos) {
        pAudiblePos->set(rPos);

        if (pPos != nullptr) {
            mPos.set(*pPos);
            mVelocity.sub(rPos, mPos);
        } else {
            mPos.set(rPos);
            mVelocity.zero();
        }
    }

    void calc(const Vec3f& rPos) {
        mVelocity.sub(rPos, mPos);
        mPos.set(rPos);
    }

    const Vec3f& getPos() const {
        return mPos;
    }

    const Vec3f& getVelocity() const {
        return mVelocity;
    }

    Vec3f mPos;
    Vec3f mVelocity;
};

class SoundDopplerRelPos {
public:
    const Vec3f& getPos() const {
        return mPos;
    }

    const Vec3f& getDir() const {
        return mDir;
    }

    f32 getDist() const {
        return mDist;
    }

    Vec3f mPos;
    Vec3f mDir;
    f32 mDist;
};

class SoundDopplerChannel {
public:
    const Vec3f& getDir() const {
        return mRelPos.getDir();
    }

    SoundParams mParams;
    SoundDopplerRelPos mRelPos;
    f32 mPitch;
};
