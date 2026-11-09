#pragma once

#include "Game/Boss/KoopaBattleStairsBase.hpp"

class KoopaBattleStairsVs3 : public KoopaBattleStairsBase {
public:
    KoopaBattleStairsVs3(Koopa* pKoopa);

    virtual s32 registerStair(KoopaBattleMapStair* pStair) override;

    void exeWaitDemo();
    void exeDemo();
    void exeWait();
    void tryAttack();

    // Fire leaves from a fixed point in this battle, not from Koopa's mouth.
    Vec3f mFireStartPos;
};
