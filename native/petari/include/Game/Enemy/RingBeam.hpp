#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ShadowVolumeDrawer.hpp"

class ModelObj;
class RingBeamShadowDrawer;

class RingBeam : public LiveActor {
public:
    RingBeam(const char* pName, LiveActor* pHost, bool isFollowGravity, bool hasShadow);

    virtual void init(const TableIter& rIter);
    virtual void appear();
    virtual void kill();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void setSpeed(f32 speed);
    void setLife(s32 life);
    void setRadius(f32 radius);
    void initPos(const LiveActor* pActor);
    void startBrk(const char* pAnimName);
    void exeSpread();

    LiveActor* mHost;
    ModelObj* mBloomModel;
    f32 mSpeed;
    s32 mLife;
    bool mIsFollowGravity;
    bool mHasShadow;
    RingBeamShadowDrawer* mShadowDrawer;
    ModelObj* mShadowModel;
    Vec3f mFront;
    Vec3f mBeamGravity;
    Vec3f mEdgePos;  // the ring's leading edge point; the radius is its distance from the host's axis
    Vec3f mSoundPos;
};

class RingBeamShadowDrawer : public ShadowVolumeDrawer {
public:
    RingBeamShadowDrawer(const LiveActor*);

    virtual ~RingBeamShadowDrawer() {};
    virtual void loadModelDrawMtx(TypedDraw& rDraw) const;
    virtual void drawShape(TypedDraw& rDraw) const;
    virtual bool isDraw() const;

    const LiveActor* mHost;
    f32 mRadius;
};
