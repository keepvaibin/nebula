#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class ModelObj;
class MogucchiHill;

class Mogucchi : public LiveActor {
public:
    Mogucchi(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void makeActorAppeared() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

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
    bool isHittableDown() const;
    void startScatter(HitSensor* pSender, HitSensor* pReceiver);

    MogucchiHill* mHill;
    ModelObj* mHole;
    Pos3f mHoleMtx;
    Vec3f mRailGravity;
    f32 mScatterUpSpeed;
    Vec3f mScatterDir;
    f32 mStrollSpeed;
    f32 mMaxStrollSpeed;
    bool mIsStoppedByP2;
};
