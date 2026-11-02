#pragma once

#include "Game/Enemy/BegomanBaby.hpp"
#include "Game/LiveActor/LiveActor.hpp"

class ActorCameraInfo;
class PartsModel;
class MultiEventCamera;
class JumpGuarderBaby;

class JumpEmitter : public LiveActor {
public:
    JumpEmitter(const char*);

    virtual void kill();

    void initEventCamera(const JMapInfoIter&);
    void startEventCamera();
    void updateEventCamera();
    void endEventCamera();
    void updateRotate();

    MtxPtr mBodyJointMtx;
    TMtx34f _90;
    PartsModel* mHeadModel;
    TVec3f _C4;
    bool mIsEventCamera;
    ActorCameraInfo* mCameraInfo;
    MultiEventCamera* mMultEventCamera;
};

namespace MR {
    bool enableGroupAttack(LiveActor*, f32, f32);
};  // namespace MR

class JumpGuarder : public JumpEmitter {
public:
    JumpGuarder(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void control();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    /// @brief Filters out their own babies.
    bool isHit(const LiveActor*) const;

    bool enableAttack();
    void exeHide();
    void exeUp();
    void exeWait();
    void exeDown();
    void exeHopStart();
    void exeHopWait();
    void exeHopJump();
    void exeHopEnd();
    void exePreOpen();
    void addBaby(JumpGuarderBaby*);
    void exeOpen();
    void exeClose();
    void exeInter();

    /// @brief Allocated array of `mNumBabies` babies.
    JumpGuarderBaby* mBabies;

    s32 mNumBabies;
    s32 mBumpCooldown;
    JumpGuarderBaby* mActiveBabies[4];
    s32 mNumActiveBabies;
    s32 mNumCoins;
    f32 mBabyVelocity;
};

class JumpGuarderBaby : public BegomanBaby {
public:
    JumpGuarderBaby();
};
