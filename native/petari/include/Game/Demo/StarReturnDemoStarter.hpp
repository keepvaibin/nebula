#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class NameObjArchiveListCollector;
class ReturnDemoRailMove;
class StageResultInformer;

class StarReturnDemoStarter : public LiveActor {
public:
    /// @brief Creates a new `StarReturnDemoStarter`.
    /// @param pName A pointer to the null-terminated name of the object.
    StarReturnDemoStarter(const char* pName);

    virtual void init(const JMapInfoIter&);
    virtual void appear();
    virtual void kill();
    virtual void control();

    static void makeArchiveList(NameObjArchiveListCollector*, const JMapInfoIter&);

    LiveActor* createSubModel(const char*, const char*, bool);
    void tryRegisterDemo(const char*, const JMapInfoIter&);
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
    TPos3f mTransform;
    TVec3f mPosition;
    LiveActor* mPowerStar;
    LiveActor* mLuma;
    LiveActor* mHair;
    s32 mFrame;
};
