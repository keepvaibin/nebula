#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ModelObj;
class MogucchiHill;

class Mogucchi : public LiveActor {
public:
    Mogucchi(const char*);
    virtual ~Mogucchi();

    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void makeActorAppeared();
    virtual void kill();
    virtual void control();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeStroll();
    void endStroll();
    void exeAppearDown();
    void exeDive();
    void exeScatter();
    void exeDie();
    void exeDown();
    void initSensor();
    void createMogucchiHill();
    void createHole();
    void updatePosition();
    void updateReferenceMtx();
    bool checkHipDrop() const;
    void updateStrollSpeed();
    void reflectStarPointer2P();
    void makeEulerRotation();
    void calcAttackDir(TVec3f*, const TVec3f&, const TVec3f&) const;
    bool receiveAttackBySpinSensor(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    bool receiveAttackByBodySensor(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    void calcScatterVec(const TVec3f&, const TVec3f&);

    MogucchiHill* mHill;
    ModelObj* mHole;
    TPos3f mNewHolePos;
    TVec3f mRailGravity;
    bool mGrounded;
    f32 mScatterPropulsionSpeed;
    TVec3f mScatterNormal;
    f32 _E4;
    f32 mStrollSpeed;
    f32 mMaxStrollSpeed;
    bool mIsStoppedByP2;
};
