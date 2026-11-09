#pragma once

#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/LiveActor.hpp"

class GameTexture;
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

    virtual void init(const TableIter& rIter) override;
    virtual void movement() override;
    virtual void draw() const override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void initThread(const Vec3f&);
    void startActorBind(LiveActor*, const Vec3f**, const Vec3f**, const Vec3f**, const Vec3f&, const Vec3f&, s32);
    bool touchActor(const Vec3f&, const Vec3f&);
    void tryPush(const Vec3f&, f32);
    void stopAllPartsPoint();
    void cutSpiderThread();
    void initMainThreads();
    void connectMainPoint(SpiderThreadPoint*, SpiderThreadPoint*, bool);
    void updatePointerInfo();
    void updateHangedPoint();
    void tryTouchPoint(s32);
    void findNearestPointPos(const Vec3f**, const Vec3f**, s32*, s32*, const Vec3f&) const;
    SpiderThreadHangInfo* findHangInfo(LiveActor*) const;
    SiteMaterial loadMaterial() const;

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
    Vec3f mFront;
    SpiderThreadWindCtrl* mWindCtrl;
    bool mIsBloomOn;
    GameTexture* mThreadTexture;
    GameTexture* mIndirectTexture;
};

namespace MR {
    void initSpiderThread(const Vec3f&);
    void appearSpiderThread();
    void startSpiderThreadBattle();
    void startSpiderThreadChance();
    void startSpiderThreadBattleEnd();
    void pauseOffSpiderThread();
    f32 getSpiderThreadPosZ();
    void startActorBindToSpiderThread(LiveActor*, const Vec3f**, const Vec3f**, const Vec3f**, const Vec3f&, const Vec3f&);
    bool sendMsgToSpiderThread(u32, HitSensor*);
    bool touchActorToSpiderThread(const Vec3f&, const Vec3f&);
    void tryPushSpiderThread(const Vec3f&, f32);
    void onSpiderThreadBloom();
    void drawSpiderThreadBloom();
};  // namespace MR
