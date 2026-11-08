#pragma once

#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/JointController.hpp"

class AnimScaleController;
class WalkerStateBindStarPointer;

class SearchBeamer : public LiveActor {
public:
    SearchBeamer(const char*);

    virtual void init(const TableIter&);
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void updateHitSensor(HitSensor*);
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void exeNonActive();
    void exeCloseWaitFar();
    void exeCloseSearch();
    void exeCloseWaitNear();
    void exeOpenMouth();
    void exeBeamPrepare();
    void exeBeamStart();
    void exeBeamAim();
    void exeCloseMouth();
    void exeStopStart();
    void exeStop();
    void exeRecover();
    void exePointerSwoon();
    void endPointerSwoon();
    void exeStopForPlayerOff();
    bool calcJointPropeller(Pos3f*, const JointControllerInfo&);
    bool calcJointBeamStart(Pos3f*, const JointControllerInfo&);
    bool calcJointBeamEnd(Pos3f*, const JointControllerInfo&);
    void updatePropeller();
    void updateBeamEffect(bool);
    void updateBeamShadow();
    void initBeamPos();
    void reformDirection(bool);
    void bowToPlayer();
    bool checkBeamDistiny(Vec3f*, Vec3f) const;
    bool isPlayerInTerritory() const;
    bool tryNonActive();
    bool tryStopStart();
    bool tryPointerSwoon();
    void endNonActive();

    AnimScaleController* mScaleController;
    WalkerStateBindStarPointer* mBindStarPointer;
    Vec3f mFront;
    Vec3f mUp;
    f32 mPropellerAngle;
    f32 mPropellerSpeed;
    JointController* mPropellerJointCtrl;
    Mtx34f mCollisionMtx;
    JointController* mBeanStartJointCtrl;
    JointController* mBeamEndJointCtrl;
    Vec3f mBeamStart;
    Vec3f mBeamEnd;
    f32 mBeamAimSpeed;
    f32 mTerritoryRadius;
    f32 mBeamLength;
};
