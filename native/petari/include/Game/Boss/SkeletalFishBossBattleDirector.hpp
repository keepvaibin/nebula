#pragma once

#include "Game/Util/Array.hpp"
#include "Game/LiveActor/ModelObj.hpp"

class SkeletalFishBoss;
class SubmarineVolcanoBigColumn;

class SkeletalFishBossBattleDirector : public NameObj {
public:
    SkeletalFishBossBattleDirector(SkeletalFishBoss*);

    virtual ~SkeletalFishBossBattleDirector();
    virtual void movement();

    void initiate();
    void startPowerUpDemo1();
    void startPowerUpDemo2();
    void playGuardAnim(const char*, s32);
    void tryColumnCollision(HitSensor*);
    void pauseOffCast();
    void endPowerUpDemo1();
    void endPowerUpDemo2();
    void killGuard();
    void appearBirdLouse();
    void killBirdLouse();

    SkeletalFishBoss* mFishBoss;
    MR::GrowTable< SubmarineVolcanoBigColumn*, 0x20 > mColumns;
    s32 _90;
    MR::GrowTable< LiveActor*, 0x10 > _94;
    s32 _D4;
    ModelObj* mGuardModels[4];
};
