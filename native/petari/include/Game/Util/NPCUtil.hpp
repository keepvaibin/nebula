#pragma once

#include "Game/System/NerveExecutor.hpp"
#include <nebula_geometry.hpp>

class TableIter;
class LiveActor;
class ModelObj;
class NPCActor;
class NPCActorItem;
class PartsModel;
class TalkMessageCtrl;

namespace MR {
    bool getNPCItemData(NPCActorItem*, s32);
    bool isNPCItemFileExist(const char*);
    void initDefaultPose(NPCActor*, const TableIter&);
    void turnPlayerToActor(const LiveActor*, f32);
    void decidePose(NPCActor*, const Vec3f&, const Vec3f&, const Vec3f&, f32, f32, f32);
    void setNPCActorPos(NPCActor*, const char*);
    void setNPCActorPos(NPCActor*, const Vec3f&);
    void setNPCActorPose(NPCActor*, const Vec3f&, const Vec3f&, const Vec3f&);
    void followRailPose(NPCActor*, f32, f32);
    void followRailPoseOnGround(NPCActor*, f32);
    void followRailPoseOnGround(NPCActor*, const LiveActor*, f32);
    void setDefaultPose(NPCActor*);
    bool convertPosOnGround(Vec3f*, const Vec3f&);
    void timeKeepDemoFadeIn();
    void timeKeepDemoFadeOut();
    void startNPCTalkCamera(const TalkMessageCtrl*, MtxPtr, f32, s32);
    void startNPCTalkCamera(const TalkMessageCtrl*, MtxPtr, MtxPtr, f32, s32);
    void endNPCTalkCamera(bool, s32 = -1);
    void initDefaultPosAndQuat(NPCActor*, const TableIter&);
    PartsModel* createNPCGoods(LiveActor*, const char*, const char*);
    PartsModel* createIndirectNPCGoods(LiveActor*, const char*, const char*);
    bool calcPlayerFaceStareVector(Vec3f*, MtxPtr, MtxPtr);
    bool calcPlayerFaceStarePos(Vec3f*, MtxPtr, MtxPtr);
    bool isActionContinuous(const LiveActor*);
    bool isActionLoopedOrStopped(const LiveActor*);
    void invalidateLodCtrl(const NPCActor*);
    void startMoveAction(NPCActor*);
    bool tryStartTalkAction(NPCActor*);
    bool tryStartMoveTalkAction(NPCActor*);
    bool tryStartTurnAction(NPCActor*);
    bool tryStartMoveTurnAction(NPCActor*);
    bool tryStartReaction(NPCActor*);
    bool tryTalkNearPlayerAndStartTalkAction(NPCActor*);
    bool tryTalkNearPlayerAndStartMoveTalkAction(NPCActor*);
    bool tryTalkNearPlayerAtEndAndStartTalkAction(NPCActor*);
    bool tryTalkNearPlayerAtEndAndStartMoveTalkAction(NPCActor*);
    bool tryTalkForceAndStartMoveTalkAction(NPCActor*);
    bool tryTalkForceAtEndAndStartTalkAction(NPCActor*);
    bool tryStartReactionAndPushNerve(NPCActor*, const Nerve*);
    bool tryStartReactionAndPopNerve(NPCActor*);
    bool tryChangeTalkActionRandom(NPCActor*, const char*, const char*, const char*);
    f32 calcFloatOffset(const NPCActor*, f32, f32);
    void calcAndSetFloatBaseMtx(NPCActor*, f32);
};  // namespace MR

class TakeOutStar : public NerveExecutor {
public:
    TakeOutStar(NPCActor*, const char*, const char*, const Nerve*);

    bool takeOut();
    bool isFirstStep();
    bool isLastStep();
    void exeAnim();
    void exeDemo();
    void exeTerm();

    ModelObj* mStarModel;
    NPCActor* mActor;
    const Nerve* mNerve;
    const char* mActionName;
    const char* mAnimName;
    // The anim step from which the star's presence sound loops.
    s32 mAppearSeStep;
};

class FadeStarter : public NerveExecutor {
public:
    FadeStarter(NPCActor*, s32);

    bool update();
    void exeFade();
    void exeTerm();

    NPCActor* mActor;
    const Nerve* mSavedNerve;
    s32 mFadeTime;
};

class DemoStarter : public NerveExecutor {
public:
    DemoStarter(NPCActor*);

    bool update();
    void start();
    void exeInit();
    void exeFade();
    void exeWait();
    void exeTerm();

    NPCActor* mActor;
};
