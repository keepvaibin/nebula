#pragma once

#include "Game/LiveActor/ModelObj.hpp"

class ParabolicPath;

class MoguStone : public ModelObj {
public:
    MoguStone(const char*, const char*);

    virtual void init(const JMapInfoIter& rIter);
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
    bool isTaken();
    void emit(bool, const Vec3f&, const Vec3f&, f32);

    Quat4f _90;
    Quat4f _A0;
    Vec3f _B0;
    f32 _BC;
    bool _C0;
};

class ThrowingIce : public MoguStone {
public:
    ThrowingIce(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual void doBehavior();
    virtual void startBreakSound();
    virtual void startThrowLevelSound();

    void emitIce(const Vec3f&, const Vec3f&, f32, const Vec3f&);

    ParabolicPath* mPath;
};
