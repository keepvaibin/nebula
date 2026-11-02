#pragma once

#include "JSystem/JAudio2/JASSoundParams.hpp"
#include "JSystem/JAudio2/JAUAudible.hpp"
#include "JSystem/JGeometry/TVec.hpp"

class JAUDopplerAudibleAbsPos {
public:
    void init(TVec3f* pAudiblePos, const TVec3f& rPos, const TVec3f* pPos) {
        pAudiblePos->set(rPos);

        if (pPos != nullptr) {
            mPos.set(*pPos);
            mVelocity.sub(rPos, mPos);
        } else {
            mPos.set(rPos);
            mVelocity.zero();
        }
    }

    void calc(const TVec3f& rPos) {
        mVelocity.sub(rPos, mPos);
        mPos.set(rPos);
    }

    const TVec3f& getPos() const {
        return mPos;
    }

    const TVec3f& getVelocity() const {
        return mVelocity;
    }

    TVec3f mPos;
    TVec3f mVelocity;
};

class JAUDopplerAudibleRelPos {
public:
    const TVec3f& getPos() const {
        return mPos;
    }

    const TVec3f& getDir() const {
        return mDir;
    }

    f32 getDist() const {
        return mDist;
    }

    TVec3f mPos;
    TVec3f mDir;
    f32 mDist;
};

class JAUDopplerAudibleChannel {
public:
    const TVec3f& getDir() const {
        return mRelPos.getDir();
    }

    JASSoundParams mParams;
    JAUDopplerAudibleRelPos mRelPos;
    f32 mPitch;
};
