#pragma once

#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/JointController.hpp"

class AnimScaleController;
class WalkerStateBindStarPointer;

class SearchBeamer : public LiveActor {
public:
    SearchBeamer(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void updateHitSensor(HitSensor* pSensor) override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

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
    bool calcJointPropeller(Pos3f* pPos, const JointControllerInfo& rInfo);
    bool calcJointBeamStart(Pos3f* pPos, const JointControllerInfo& rInfo);
    bool calcJointBeamEnd(Pos3f* pPos, const JointControllerInfo& rInfo);
    void updatePropeller();
    void updateBeamEffect(bool isHitMap);
    void updateBeamShadow();
    void initBeamPos();
    void stopBeam();
    void reformDirection(bool isFast);
    void bowToPlayer();
    bool checkBeamDestination(Vec3f* pHitPos, Vec3f dir) const;
    bool isPlayerInTerritory() const;
    bool isStopNerve() const;
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
    JointController* mBeamStartJointCtrl;
    JointController* mBeamEndJointCtrl;
    Vec3f mBeamStart;
    Vec3f mBeamEnd;
    f32 mBeamAimSpeed;     // Obj_arg0
    f32 mTerritoryRadius;  // Obj_arg1
    f32 mBeamLength;
};
