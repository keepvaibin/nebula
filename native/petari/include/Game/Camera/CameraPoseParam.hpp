#pragma once

#include <nebula_geometry.hpp>

class CameraPoseParam {
public:
    CameraPoseParam();

    void copyFrom(const CameraPoseParam& rParam) {
        mWatchUpVec.set(rParam.mWatchUpVec);
        mWatchPos.set(rParam.mWatchPos);
        mUpVec.set(rParam.mUpVec);
        mPos.set(rParam.mPos);
        mFovy = rParam.mFovy;
        mGlobalOffset.set(rParam.mGlobalOffset);
        mLocalOffset.set(rParam.mLocalOffset);
        mFrontOffset = rParam.mFrontOffset;
        mUpperOffset = rParam.mUpperOffset;
        mRoll = rParam.mRoll;
    }

    const Vec3f& getWatchPos() const {
        return mWatchPos;
    }

    Vec3f mWatchUpVec;
    Vec3f mWatchPos;
    Vec3f mUpVec;
    Vec3f mPos;
    f32 mFovy;
    Vec3f mGlobalOffset;
    Vec3f mLocalOffset;
    f32 mFrontOffset;
    f32 mUpperOffset;
    f32 mRoll;
};
