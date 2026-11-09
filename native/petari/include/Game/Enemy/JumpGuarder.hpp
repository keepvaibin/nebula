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

    void initEventCamera(const TableIter&);
    void startEventCamera();
    void updateEventCamera();
    void endEventCamera();
    void updateRotate();

    MtxPtr mBodyJointMtx;
    Mtx34f mHeadMtx;
    PartsModel* mHeadModel;
    Vec3f mCameraCenter;
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

    virtual void init(const TableIter&);
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

    JumpGuarderBaby* mBabies;  // mNumBabies of them, and at least 4

    s32 mNumBabies;
    s32 mBumpCooldown;
    JumpGuarderBaby** mActiveBabies;  // as much room as mBabies
    s32 mNumActiveBabies;
    s32 mNumCoins;
    f32 mBabyVelocity;
};

class JumpGuarderBaby : public BegomanBaby {
public:
    JumpGuarderBaby();
};
