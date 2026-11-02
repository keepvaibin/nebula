#pragma once

#include <JSystem/JGeometry/TVec.hpp>

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

    const TVec3f& getWatchPos() const {
        return mWatchPos;
    }

    TVec3f mWatchUpVec;
    TVec3f mWatchPos;
    TVec3f mUpVec;
    TVec3f mPos;
    f32 mFovy;
    TVec3f mGlobalOffset;
    TVec3f mLocalOffset;
    f32 mFrontOffset;
    f32 mUpperOffset;
    f32 mRoll;
};
