#pragma once

#include <JSystem/JGeometry/TVec.hpp>
#include <revolution/types.h>

class JMapIdInfo;
class JMapInfoIter;
class LiveActor;
class ViewGroupCtrlDataEntry;

class ClippingActorInfo {
public:
    ClippingActorInfo(LiveActor*);

    void judgeClipping();
    bool isJudgedToClip() const;
    bool isGroupClipping() const;
    void setTypeToSphere(f32, const TVec3f*);
    void setGroupClippingNo(const JMapInfoIter&);

    void setFarClipLevel(s32 level) {
        mFarClipLevel = level;
    }
#ifdef TARGET_PC
    // Nebula, ray-traced shadows and light: whether ray-traced light wants this actor though the view would clip it (its
    // shadow, or its light as a light source, reaches the view). isJudgedToClip keeps such an actor unclipped, so it
    // moves, animates and is drawn as a visible one is.
    bool isWantedForLight(s32 farClipLevel) const;
#endif

    /* 0x00 */ LiveActor* mActor;
    /* 0x04 */ const TVec3f* _4;
    /* 0x08 */ f32 _8;
    /* 0x0C */ JMapIdInfo* mInfo;
    /* 0x10 */ s16 mFarClipLevel;
    /* 0x12 */ s16 _12;
    /* 0x14 */ ViewGroupCtrlDataEntry* _14;
#ifdef TARGET_PC
    mutable bool mKeptForLight;  // the last clipping judgement kept it unclipped for ray-traced light
    mutable f32 mLightReach;     // how far its traced light reaches (0: no light source; < 0: not asked yet)
#endif
};

class ClippingActorInfoList {
public:
    ClippingActorInfoList(int);

    void add(ClippingActorInfo*);
    ClippingActorInfo* find(const LiveActor*, s32*) const;
    ClippingActorInfo* findOrNone(const LiveActor*) const;
    bool isInList(const LiveActor*) const;

    ClippingActorInfo* remove(LiveActor*);

    /* 0x0 */ int _0;
    /* 0x4 */ s32 _4;
    /* 0x8 */ ClippingActorInfo** mClippingActorList;
};
