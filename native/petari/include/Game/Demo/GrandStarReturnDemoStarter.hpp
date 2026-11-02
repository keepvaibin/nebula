#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ActorCameraInfo;
class PowerStar;
class ReturnDemoRailMove;
class StageResultInformer;

class GrandStarReturnDemoStarter : public LiveActor {
public:
    GrandStarReturnDemoStarter(const char*);
    virtual ~GrandStarReturnDemoStarter();
    virtual void init(const JMapInfoIter&);
    virtual void appear();
    virtual void control();

    void updateRailMoveEndDir();
    void calcOffsetStarToCore(TVec3f*) const;
    void emitEffectRush();
    void updateRushStarPos(const TVec3f&, s32);
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
    TPos3f mPrevTransform;
    TPos3f mTransform;
    PowerStar* mPowerStar;
    TVec3f mDistanceToCore;
    TVec3f mPowerStarPosition;
    ActorCameraInfo* mActorCameraInfo;
};
