#pragma once

#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/JointController.hpp"

class AnimScaleController;
class WalkerStateBindStarPointer;

class SearchBeamer : public LiveActor {
public:
    SearchBeamer(const char*);

    virtual void init(const JMapInfoIter&);
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
    void exeDPDSwoon();
    void endDPDSwoon();
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
    bool tryDPDSwoon();
    void endNonActive();

    AnimScaleController* mScaleController;
    WalkerStateBindStarPointer* mBindStarPointer;
    Vec3f _94;
    Vec3f _A0;
    f32 _AC;
    f32 _B0;
    JointController* mPropellerJointCtrl;
    Mtx34f _B8;
    Mtx34f _E8;
    JointController* mBeanStartJointCtrl;
    JointController* mBeamEndJointCtrl;
    Vec3f mBeamStart;
    Vec3f mBeamEnd;
    f32 _138;
    f32 _13C;
    f32 _140;
};
