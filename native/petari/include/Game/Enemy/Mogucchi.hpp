#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class ModelObj;
class MogucchiHill;

class Mogucchi : public LiveActor {
public:
    Mogucchi(const char*);
    virtual ~Mogucchi();

    virtual void init(const TableIter& rIter);
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
    void calcAttackDir(Vec3f*, const Vec3f&, const Vec3f&) const;
    bool receiveAttackBySpinSensor(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    bool receiveAttackByBodySensor(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    void calcScatterVec(const Vec3f&, const Vec3f&);

    MogucchiHill* mHill;
    ModelObj* mHole;
    Pos3f mNewHolePos;
    Vec3f mRailGravity;
    bool mGrounded;
    f32 mScatterPropulsionSpeed;
    Vec3f mScatterNormal;
    f32 mStrollSpeed;
    f32 mMaxStrollSpeed;
    bool mIsStoppedByP2;
};
