#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class PowerStar;
class ReturnDemoRailMove;
class StageResultInformer;

class GrandStarReturnDemoStarter : public LiveActor {
public:
    GrandStarReturnDemoStarter(const char*);
    virtual void init(const TableIter&);
    virtual void appear();
    virtual void control();

    void updateRailMoveEndDir();
    void calcOffsetStarToCore(Vec3f*) const;
    void emitEffectRush();
    void updateRushStarPos(const Vec3f&, s32);
    void tryStartStageResult(const char*);

    void exeMove();
    void exeFlyWait();
    void exeRushToCore();
    void exeRevival();
    void exeStageResult();
    void exeFadeOut();
    void exeWaitDemoEnd();

    ReturnDemoRailMove* mReturnDemoRailMove;
    StageResultInformer* mStageResultInformer;
    Pos3f mPrevTransform;
    Pos3f mTransform;
    PowerStar* mPowerStar;
    Vec3f mDistanceToCore;
    Vec3f mPowerStarPosition;
    ActorCameraInfo* mActorCameraInfo;
};
