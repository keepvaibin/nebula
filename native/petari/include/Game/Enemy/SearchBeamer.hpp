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
    bool calcJointPropeller(TPos3f*, const JointControllerInfo&);
    bool calcJointBeamStart(TPos3f*, const JointControllerInfo&);
    bool calcJointBeamEnd(TPos3f*, const JointControllerInfo&);
    void updatePropeller();
    void updateBeamEffect(bool);
    void updateBeamShadow();
    void initBeamPos();
    void reformDirection(bool);
    void bowToPlayer();
    bool checkBeamDistiny(TVec3f*, TVec3f) const;
    bool isPlayerInTerritory() const;
    bool tryNonActive();
    bool tryStopStart();
    bool tryDPDSwoon();
    void endNonActive();

    AnimScaleController* mScaleController;
    WalkerStateBindStarPointer* mBindStarPointer;
    TVec3f _94;
    TVec3f _A0;
    f32 _AC;
    f32 _B0;
    JointController* mPropellerJointCtrl;
    TMtx34f _B8;
    TMtx34f _E8;
    JointController* mBeanStartJointCtrl;
    JointController* mBeamEndJointCtrl;
    TVec3f mBeamStart;
    TVec3f mBeamEnd;
    f32 _138;
    f32 _13C;
    f32 _140;
};
