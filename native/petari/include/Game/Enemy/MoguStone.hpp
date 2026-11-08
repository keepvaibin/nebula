#pragma once

#include "Game/LiveActor/ModelObj.hpp"

class ParabolicPath;

class MoguStone : public ModelObj {
public:
    MoguStone(const char*, const char*);

    virtual void init(const TableIter& rIter);
    virtual void appear();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

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

    virtual void init(const TableIter&);
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual void doBehavior();
    virtual void startBreakSound();
    virtual void startThrowLevelSound();

    void emitIce(const Vec3f& rFrom, const Vec3f& rTo, f32 speed, const Vec3f& rGravity);

    ParabolicPath* mPath;
};
