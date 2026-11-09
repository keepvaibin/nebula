#pragma once

#include "Game/LiveActor/ModelObj.hpp"

class ParabolicPath;

class MoguStone : public ModelObj {
public:
    MoguStone(const char*, const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

    virtual void doBehavior();
    virtual void startBreakSound();
    virtual void startThrowLevelSound();

    void exeTaken();
    void exeThrow();
    void exeFall();
    bool isTaken() const;
    void emit(bool isOnGravity, const Vec3f& rFrom, const Vec3f& rTo, f32 speed);
    void breakAndKill();

    Quat4f mPoseQuat;
    Quat4f mRotateQuat;
    Vec3f mThrowDir;
    f32 mThrowSpeed;
    bool mIsThrowHorizontal;
};

class ThrowingIce : public MoguStone {
public:
    ThrowingIce(const char*);

    virtual void init(const TableIter&) override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual void doBehavior() override;
    virtual void startBreakSound() override;
    virtual void startThrowLevelSound() override;

    void emitIce(const Vec3f& rFrom, const Vec3f& rTo, f32 speed, const Vec3f& rGravity);

    ParabolicPath* mPath;
};
