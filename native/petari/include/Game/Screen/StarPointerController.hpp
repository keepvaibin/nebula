#pragma once

#include <nebula_geometry.hpp>

class PointerInfo {
public:
    PointerInfo();

    Vec2f mPos;
    bool mInScreen;
    f32 mViewDistZ;
};

class StarPointerController {
public:
    StarPointerController();

    bool isOutScreenLong() const;
    void initAndSetPort(s32);
    // isQueried: the scene is asked for the point under the pointer this step (StarPointerDirector::update).
    void movement(bool isQueried);
    void updateWorldPos(bool isQueried);
    void storePastPointingData();
    void calcPastPointingPosOnScreen(Vec2f*, s32) const;
    void updatePointerInfo();
    void updateAdditionalInfo();

    void forcePointerOut();

    f32 calcViewDistanceZ(const Vec3f&, MtxPtr);

    bool isInScreen() const {
        return mPastInfo.mInScreen;
    }

    f32 getViewDistZ() const {
        return mPastInfo.mViewDistZ;
    }

    s32 mPadChannel;
    PointerInfo mPastInfo;
    PointerInfo mInfo;
    Vec2f mScreenPos;
    s32 mOutScreenTime;
    Vec2f* mNextPastPosition;
    s32 mNextPastPointNum;
    f32 mScreenDist;
    f32 mScreenSpeed;
    Vec2f mScreenVel;
    Vec3f mWorldVel;
    Vec3f mWorldPos;
};
