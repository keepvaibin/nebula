#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FireBubble : public LiveActor {
public:
    FireBubble(const char* = "バブル");
    virtual void init(const TableIter& rIter);
    virtual void appear();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void initHitSensors(const char* const, const char* const);
    void appear(const Vec3f&, const Vec3f&, const Vec3f&);
    void initMapToolInfo(const TableIter& rIter);
    void updateChaseFrontVec(f32);
    void updateChaseFrontVecAndVelocity(f32);
    void updateGravity(f32);
    void calcReflectVelocity(Vec3f*) const;
    bool tryChaseEnd();
    bool tryReflect();
    void killOrWaitToApppear();

    void exeAppear();
    void exeWait();
    void exeChase();
    void exeReflect();
    void exeReflectLand();
    void exeReflectWait();
    void exeDown();
    void exeWaitToAppear();

    bool isMovingDown();  // fabricated

    bool mIsValidInfo;
    Vec3f mAppearPos;
    Vec3f mAppearFront;
    Vec3f mAppearVelocity;
    Vec3f mFront;
    s32 mActFrame;
    s32 mChaseCounter;
};
