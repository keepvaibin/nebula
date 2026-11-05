#pragma once

#include "Game/Boss/KoopaBattleStairsBase.hpp"

class KoopaFireStairs;

class KoopaBattleStairsVs1 : public KoopaBattleStairsBase {
public:
    KoopaBattleStairsVs1(Koopa* pKoopa);

    virtual ~KoopaBattleStairsVs1();
    virtual s32 registerStair(KoopaBattleMapStair* pStair);

    void exeWaitDemo();
    void exeDemo();
    void exeWait();
    void exeAttackFire();
    void exeJumpToNextPosStart();
    void exeJumpToNextPosLoop();
    void exeJumpToNextPosEnd();

    bool tryAttackRequest();
    bool tryRequestedToMoveNextPos();

    KoopaBattleMapStair* mFarBattleMapStair;
    KoopaBattleMapStair* mNearBattleMapStair;
    KoopaFireStairs* mFarFireStairs;
    KoopaFireStairs* mNearFireStairs;
    KoopaBattleMapStair* _20;
    bool mCanJump;
    Vec3f mJumpPos0;
    Vec3f mJumpPos1;
    Vec3f mJumpPos2;
    s32 _4C;
    s32 _50;
    s32 _54;
    s32 mAvailableStairs;
    s32 mJumpIdx;
    Vec3f mOldPosition;
    Vec3f mNewPosition;
    Vec3f mNewDirection;
};
