#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class JUTTexture;
class SpiderThreadHangInfo;
class SpiderThreadMainPoint;
class SpiderThreadPart;
class SpiderThreadPoint;
class SpiderThreadRadialLine;
class SpiderThreadWindCtrl;

struct CutPointEntry {
    s32 mRadialIdx;
    s32 mPointIdx;
};

class SpiderThread : public LiveActor {
public:
    SpiderThread(const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void movement();
    virtual void draw() const;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void initThread(const TVec3f&);
    void startActorBind(LiveActor*, const TVec3f**, const TVec3f**, const TVec3f**, const TVec3f&, const TVec3f&, s32);
    bool touchActor(const TVec3f&, const TVec3f&);
    void tryPush(const TVec3f&, f32);
    void stopAllPartsPoint();
    void cutSpiderThread();
    void initMainThreads();
    void connectMainPoint(SpiderThreadPoint*, SpiderThreadPoint*, bool);
    void updatePointerInfo();
    void updateHangedPoint();
    void tryTouchPoint(s32);
    void findNearestPointPos(const TVec3f**, const TVec3f**, s32*, s32*, const TVec3f&) const;
    SpiderThreadHangInfo* findHangInfo(LiveActor*) const;
    void initDraw() const;

    SpiderThreadRadialLine* getMainRadialLine(int idx) const {
        return mRadialLines[idx];
    }

    SpiderThreadPart* getRadialPart(int idx) const {
        return mRadialParts[idx];
    }

    SpiderThreadPart* getCirclePart(int idx) const {
        return mCircleParts[idx];
    }

    SpiderThreadHangInfo* getActorHangInfo(int idx) const {
        return mHangInfos[idx];
    }

    SpiderThreadMainPoint* mMainPoint;
    SpiderThreadHangInfo** mHangInfos;
    s32 mNumHangInfos;
    SpiderThreadHangInfo* mShootHangInfo;
    s32 mNumRadialLines;
    SpiderThreadRadialLine** mRadialLines;
    SpiderThreadMainPoint** mAnchorPoints;
    s32 mRadialPartsBufferSize;
    s32 mNumRadialParts;
    SpiderThreadPart** mRadialParts;
    s32 mCirclePartsBufferSize;
    s32 mNumCircleParts;
    SpiderThreadPart** mCircleParts;
    s32 mCutPointsBufferSize;
    s32 mNumCutPoints;
    SpiderThreadMainPoint** mCutPoints;
    TVec3f mFront;
    SpiderThreadWindCtrl* mWindCtrl;
    bool mIsBloomOn;
    JUTTexture* mThreadTexture;
    JUTTexture* mIndirectTexture;
};

namespace MR {
    void initSpiderThread(const TVec3f&);
    void appearSpiderThread();
    void startSpiderThreadBattle();
    void startSpiderThreadChance();
    void startSpiderThreadBattleEnd();
    void pauseOffSpiderThread();
    f32 getSpiderThreadPosZ();
    void startActorBindToSpiderThread(LiveActor*, const TVec3f**, const TVec3f**, const TVec3f**, const TVec3f&, const TVec3f&);
    bool sendMsgToSpiderThread(u32, HitSensor*);
    bool touchActorToSpiderThread(const TVec3f&, const TVec3f&);
    void tryPushSpiderThread(const TVec3f&, f32);
    void onSpiderThreadBloom();
    void drawSpiderThreadBloom();
};  // namespace MR
