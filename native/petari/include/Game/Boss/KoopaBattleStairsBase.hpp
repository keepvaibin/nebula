#pragma once

#include "Game/System/NerveExecutor.hpp"
#include <nebula_geometry.hpp>

class HitSensor;
class Koopa;
class KoopaBattleMapStair;
class LiveActorGroup;

class KoopaBattleStairsBase : public NerveExecutor {
public:
    KoopaBattleStairsBase(Koopa* pKoopa);

    virtual s32 registerStair(KoopaBattleMapStair* pStair) = 0;

    void calcFireStartPos(Vec3f* pVec, const char* pDemoName) const;
    void update();
    void end();
    bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    s32 calcFireAttackStep(KoopaBattleMapStair* pBattleMapStair, f32 fireSpeed, s32 emitStep, const Vec3f& rFireStartPos) const;

    Koopa* mKoopa;
    LiveActorGroup* mStairsGroup;
};
