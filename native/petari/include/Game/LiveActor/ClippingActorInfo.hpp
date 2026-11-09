#pragma once

#include <nebula_geometry.hpp>

class PlacementId;
class TableIter;
class LiveActor;
class ClippingActorInfoList;
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
    void setTypeToSphere(f32, const Vec3f*);
    void setGroupClippingNo(const TableIter&);

    void setFarClipLevel(s32 level) {
        mFarClipLevel = level;
    }

    LiveActor* mActor;
    const Vec3f* mCenter;  // of the clipping sphere
    f32 mRadius;
    PlacementId* mInfo;  // the clipping group; null: none
    s16 mFarClipLevel;
    s32 mViewGroupId;
    ViewGroupCtrlDataEntry* mViewGroup;
    const ClippingActorInfo* mGround;  // the planet it stands on; null: none
    bool mGroundFound;
    ClippingActorInfoList* mList;  // the holder's list it is in, at mIndex
    s32 mIndex;
};

// Infos in no set order; each knows its place, so adding and removing take constant time.
class ClippingActorInfoList {
public:
    ClippingActorInfoList(s32 capacity);

    void add(ClippingActorInfo*);
    // The last one takes its place.
    void remove(ClippingActorInfo*);
    bool contains(const ClippingActorInfo* pInfo) const { return pInfo->mList == this; }

    s32 mCapacity;
    s32 mCount;
    ClippingActorInfo** mClippingActorList;
};
