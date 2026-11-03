#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class JMapIdInfo;
class JMapInfoIter;
class LiveActor;
struct ViewGroupCtrlDataEntry;

class ClippingActorInfo {
public:
    ClippingActorInfo(LiveActor*);

    void judgeClipping();
    bool isJudgedToClip() const;
    bool isDrawnWhileClipped() const;
    bool isGroundShown();
    void startClipped();
    void endClipped();
    void updateDrawnClipped();
    bool isGroupClipping() const;
    void setTypeToSphere(f32, const TVec3f*);
    void setGroupClippingNo(const JMapInfoIter&);

    void setFarClipLevel(s32 level) {
        mFarClipLevel = level;
    }

    LiveActor* mActor;
    const TVec3f* _4;
    f32 _8;
    JMapIdInfo* mInfo;
    s16 mFarClipLevel;
    s16 _12;
    ViewGroupCtrlDataEntry* _14;
    const ClippingActorInfo* mGround;  // the planet it stands on; null: none
    bool mGroundFound;
};

class ClippingActorInfoList {
public:
    ClippingActorInfoList(int);

    void add(ClippingActorInfo*);
    ClippingActorInfo* find(const LiveActor*, s32*) const;
    ClippingActorInfo* findOrNone(const LiveActor*) const;
    bool isInList(const LiveActor*) const;

    ClippingActorInfo* remove(LiveActor*);

    int _0;
    s32 _4;
    ClippingActorInfo** mClippingActorList;
};
