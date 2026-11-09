#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SpinningBox : public LiveActor {
public:
    SpinningBox(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void exeFall();
    void exeWait();
    void endWait();
    void exeSliding();
    void endSliding();
    void exeSpinning();
    void endSpinning();
    void exePointed();
    void endPointed();

    void calcPlanarProjectedVec(Vec3f*, const Vec3f&, const Vec3f&);
    void calcHitDirection(const Vec3f&);
    void generateIceBox(HitSensor* pSender, HitSensor* pReceiver);
    void hitReflection(HitSensor* pSender, HitSensor* pReceiver);

    f32 getScale() const {
        return 1.0f;
    }

    Quat4f mRotate;
    f32 mRotateSpeed;
    f32 mSlideSpeed;
    Vec3f mSlideDir;
    bool mIsOnGround;
    bool mIsIceBox;
    f32 mSpinAngle;  // NOTE: This keeps track of when the spin sfx should be emitted again
};
