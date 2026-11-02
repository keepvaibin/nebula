#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class LiveActor;
class ModelObj;
class TalkMessageCtrl;

class RabbitStateCaught : public ActorStateBase< LiveActor > {
public:
    RabbitStateCaught(LiveActor* pHost, TalkMessageCtrl* pTalkCtrl);

    virtual void init();
    virtual void appear();
    virtual void control();

    void unusePowerStarModel();
    void exeTryStartDemo();
    void exeCaught();
    void exeCaughtLand();
    void exeCaughtEvent();
    void setCaughtStartMarioPose();
    void blendBaseMatrixToMario(f32) const;

    TQuat4f mCaughtStartMarioQuat;
    TVec3f mCaughtStartMarioTrans;
    TalkMessageCtrl* mTalkCtrl;
    ModelObj* mPowerStarModel;
    TMtx34f mCaughtLandMarioBaseMtx;
    bool mUsePowerStarModel;
};
