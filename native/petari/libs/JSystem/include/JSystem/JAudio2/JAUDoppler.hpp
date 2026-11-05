#pragma once

#include "JSystem/JAudio2/JASSoundParams.hpp"
#include "JSystem/JAudio2/JAUAudible.hpp"
#include <nebula_geometry.hpp>

class JAUDopplerAudibleAbsPos {
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

class JAUDopplerAudibleRelPos {
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

class JAUDopplerAudibleChannel {
public:
    const Vec3f& getDir() const {
        return mRelPos.getDir();
    }

    JASSoundParams mParams;
    JAUDopplerAudibleRelPos mRelPos;
    f32 mPitch;
};
