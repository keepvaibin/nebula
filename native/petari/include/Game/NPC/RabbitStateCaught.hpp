#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"
#include <nebula_geometry.hpp>

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

    Quat4f mCaughtStartMarioQuat;
    Vec3f mCaughtStartMarioTrans;
    TalkMessageCtrl* mTalkCtrl;
    ModelObj* mPowerStarModel;
    Mtx34f mCaughtLandMarioBaseMtx;
    bool mUsePowerStarModel;
};
