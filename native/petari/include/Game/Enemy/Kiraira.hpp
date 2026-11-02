#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class KirairaChain;
class MsgSharedGroup;

class Kiraira : public LiveActor {
public:
    Kiraira(const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void openEyes();
    void closeEyes();
    void drift();
    void driftOnRail();
    void explode();

    bool isDown() const;

    void exeWait();
    void exeFaceToMario();
    void exeFaceToMarioAndStare();
    void exeExplode();
    void exeBeExploded();
    void exeDead();
    void exeRecoverSign();
    void exeRecover();

private:
    f32 mRailCoordSpeed;
    TVec3f mRailVec;
    TVec3f mFront;
    f32 _A8;
    bool mIsRail;
    bool mIsForceDetonated;
    bool mEyesOpen;
    MsgSharedGroup* mSharedGroup;
    KirairaChain* mChain;
};
