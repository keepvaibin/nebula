#pragma once

#include <nebula_geometry.hpp>

class PlacementId;
class TableIter;
class LiveActor;
class ClippingActorInfoList;
struct ViewGroupCtrlDataEntry;

class ClippingJudge;

class ClippingActorInfo {
public:
    ClippingActorInfo(LiveActor*);

#if defined(NEBULA_PLAIN_PATHS)
    // The scene's judge for the length of a clipping pass, looked up once for all its actors (P-135); null between
    // passes, when each test looks it up.
    static ClippingJudge* sPassJudge;
    // The pass's frustum tests, made for every target before any is judged (on the step threads, P-040): each as the
    // actor stands when the pass starts. judgeClipping takes a test's result while the state it was made for still holds
    // (an earlier actor's start or end of clipping can change it) and makes the test again otherwise.
    // The pass under way (1 and up; 0 between passes): a test made in an earlier pass, or none (an actor that joined
    // the targets during the pass), is never taken.
    static u32 sPassStamp;
    void testFrustum(ClippingJudge* pJudge, u32 stamp);
#endif

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
#if defined(NEBULA_PLAIN_PATHS)
    // testFrustum's results and the state each was made for: isJudgedToClip's for isClipped == mTestClipped, and, when
    // that one clips, isDrawnWhileClipped's frustum part for mIsDrawnClipped == mTestDrawn (mTestedOut: it was made).
    u32 mTestStamp;
    bool mTestClipped;
    bool mTestClip;
    bool mTestDrawn;
    bool mTestedOut;
    bool mTestOut;
#endif
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
