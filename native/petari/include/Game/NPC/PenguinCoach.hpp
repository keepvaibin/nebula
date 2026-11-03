#pragma once

#include "Game/Map/RaceManager.hpp"
#include "Game/NPC/NPCActor.hpp"

class ActorCameraInfo;
class LiveActorGroup;
class SurfRayTutorial;
class TakeOutStar;
class TurnJointCtrl;

class PenguinCoach : public NPCActor, public AbstractAudience {
public:
    enum Behavior { Behavior_Default = 0, Behavior_Star = 1, Behavior_Tutorial = 2, Behavior_Race = 3 };
    PenguinCoach(const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual void prepAudience();
    virtual void resetAudience();

    bool branchFunc(u32);
    bool eventFunc(u32);

    void exeTakeOutStar();
    void exeRace();
    void exeReaction();
    void exePrep();
    void exeWait();
    void exeFade();
    void exeTutorial();
    void exePraise();

    void setTutorialPos();
    void setFinishPos();
    void setPlayerNoSelectionPos();

    bool mIsRaceComplete;
    TakeOutStar* mTakeOutStar;
    s32 mBehavior;
    SurfRayTutorial* mTutorial;
    LiveActor* mActor;
    ActorCameraInfo* mCameraInfo;
    TurnJointCtrl* mTurnJointCtrl;
};
