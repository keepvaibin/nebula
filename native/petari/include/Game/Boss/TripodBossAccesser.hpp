#pragma once

#include "Game/Util/Array.hpp"

#include "Game/Boss/TripodBoss.hpp"

class TripodBossAccesser : public NameObj {
public:
    TripodBossAccesser(const char* pName);

    TripodBoss* getTriPodBoss() const;
    void setTriPodBoss(TripodBoss*);
    void addTripodBossParts(LiveActor*);
    void showTripodBossParts();
    void hideTripodBossParts();
    void activeTripodBossParts();
    bool isTripoddBossParts(const NameObj*) const;

    static TripodBossAccesser* createSceneObj();

private:
    TripodBoss* mBoss;
    MR::GrowTable< LiveActor*, 256 > mParts;
    s32 mPartsNum;
};

namespace MR {
    TripodBossAccesser* getTripodBossAccesser();
    void addTripodBossParts(LiveActor*);
    void addTripodBossPartsMovement(NameObj*);
    bool isTripoddBossParts(const NameObj*);
    void showTripodBossParts();
    void hideTripodBossParts();
    void activeTripodBossParts();
    void requestMovementTripodBossParts();
    bool isCreatedTripodBoss();
    bool isStartedTripodBoss();
    bool isBrokenTripodBoss();
    bool isDemoTripodBoss();
    bool isStartDemoTripodBoss();
    bool isDamageDemoTripodBoss();
    bool isEndBreakDownDemoTripodBoss();
    bool isLeaveMarioNowTripodBoss();
    bool isRideMarioTripodBoss();
    bool tryDamageDemoTripodBoss();
    void requestEndDamageDemoTripodBoss();
    const void* getTripodBossGravityHost();
    void setTripodBossJointAttachBaseMatrix(const Pos3f&, s32);
    void requestStartTripodBossStepSequence(s32);
    void addTripodBossStepPoint(TripodBossStepPoint*);
    void getTripodBossJointMatrix(Pos3f*, s32);
    void getTripodBossAttachJointMatrix(Pos3f*, s32);
    void concatTripodBossAttachJointMatrix(Pos3f*, s32);
    bool isSteppingTripodBossLegID(s32);
    bool isSteppingTripodBossJointID(s32);
};  // namespace MR
