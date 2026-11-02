#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class DpdInfo {
public:
    DpdInfo();

    TVec2f mPos;
    bool mInScreen;
    bool mDrawReady;
    f32 mZDepth;  // 0 near to 1 far
    f32 mViewDistZ;
};

class StarPointerController {
public:
    StarPointerController();

    bool isOutScreenLong() const;
    void initAndSetPort(s32);
    void movement(const f32*, const f32*);
    void storeDataFromCallback(const f32*, const f32*);
    void storePastPointingData();
    void calcPastPointingPosOnScreen(TVec2f*, s32) const;
    void updateDpdInfo();
    void updateAdditionalInfo();

    void forcePointerOut();

    f32 calcViewDistanceZ(const TVec3f&, MtxPtr);

    bool isInScreen() const {
        return mPastInfo.mInScreen;
    }

    f32 getViewDistZ() const {
        return mPastInfo.mViewDistZ;
    }

    const TVec2f& getPastPos() const {
        return mPastInfo.mPos;
    }

    s32 mPadChannel;
    DpdInfo mPastInfo;
    DpdInfo mInfo;
    TVec2f mScreenPos;
    s32 mOutScreenTime;
    TVec2f* mPastPosition;
    s32 mPastPointNum;
    TVec2f* mNextPastPosition;
    s32 mNextPastPointNum;
    f32 mScreenDist;
    f32 mScreenSpeed;
    TVec2f mScreenVel;
    TVec3f mWorldVel;
    TVec3f mWorldPos;
    bool mNotInScreen;
};
