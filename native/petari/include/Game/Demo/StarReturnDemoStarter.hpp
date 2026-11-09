#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class ReturnDemoRailMove;
class StageResultInformer;

class StarReturnDemoStarter : public LiveActor {
public:
    StarReturnDemoStarter(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;


    LiveActor* createSubModel(const char*, const char*, bool);
    void tryRegisterDemo(const char*, const TableIter&);
    void tryStartStageResult();
    void tryStartResultWait();

    void exeAppearWait();
    void exeMove();
    void exeFlyWaitPowerStar();
    void exeFlyWaitGrandStar();
    void exeFall();
    void exeLand();
    void exeWait();
    void exeStageResult();
    void exeFadeOut();
    void exeStageResultAfter();

private:
    ReturnDemoRailMove* mReturnDemoRailMove;
    StageResultInformer* mStageResultInformer;
    Pos3f mTransform;
    Vec3f mPosition;
    LiveActor* mPowerStar;
    LiveActor* mLuma;
    LiveActor* mHair;
    s32 mFrame;
};
