#pragma once

#include "Game/Boss/KoopaBattleStairsBase.hpp"

class KoopaFireStairs;

class KoopaBattleStairsVs1 : public KoopaBattleStairsBase {
public:
    // Koopa stands on one of three landings; a stair's level (Obj_arg1) names the landing it is fired from.
    static const s32 cLandingNum = 3;

    KoopaBattleStairsVs1(Koopa* pKoopa);

    virtual s32 registerStair(KoopaBattleMapStair* pStair) override;

    void exeWaitDemo();
    void exeDemo();
    void exeWait();
    void exeAttackFire();
    void exeJumpToNextPosStart();
    void exeJumpToNextPosLoop();
    void exeJumpToNextPosEnd();

    bool tryAttackRequest();
    bool tryRequestedToMoveNextPos();
    bool isAttackedAllStairs() const;

    KoopaBattleMapStair* mFarBattleMapStair;
    KoopaBattleMapStair* mNearBattleMapStair;
    // The stair whose fire is still to be emitted this attack.
    KoopaBattleMapStair* mFireStair;
    bool mIsJumpRequested;
    Vec3f mFireStartPos[cLandingNum];
    s32 mStairNum[cLandingNum];
    // Attack requests answered so far, over every landing.
    s32 mAttackCount;
    // The landing Koopa is on; from cLandingNum on, the demo centre.
    s32 mLandingIndex;
    Vec3f mOldPosition;
    Vec3f mNewPosition;
    Vec3f mNewDirection;
};
