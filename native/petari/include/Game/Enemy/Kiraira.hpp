#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class KirairaChain;
class MsgSharedGroup;

class Kiraira : public LiveActor {
public:
    Kiraira(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

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
    Vec3f mRailVec;
    Vec3f mFront;
    f32 mDriftStep;
    bool mIsRail;
    bool mIsForceDetonated;
    bool mEyesOpen;
    MsgSharedGroup* mSharedGroup;
    KirairaChain* mChain;
};
