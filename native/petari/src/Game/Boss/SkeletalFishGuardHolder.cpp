#include "Game/Boss/SkeletalFishGuardHolder.hpp"
#include "Game/Boss/SkeletalFishBoss.hpp"
#include "Game/Boss/SkeletalFishBossFunc.hpp"
#include "Game/Boss/SkeletalFishBossInfo.hpp"
#include "Game/Boss/SkeletalFishBossRail.hpp"
#include "Game/Boss/SkeletalFishGuard.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/System/LumaCore.hpp"

SkeletalFishGuardHolder::SkeletalFishGuardHolder(SkeletalFishBoss* pBoss, s32 numGuards, const char* pName) : NameObj(pName) {
    lumacore::setClass(this, lumacore::kClassId< SkeletalFishGuardHolder >);
    mFishBoss = pBoss;
    mBossRail = nullptr;
    mNumGuards = numGuards;
    mGuards = nullptr;
    mAppearRailCoord = 0.0f;
    mIsValid = 1;
    createAndInitGuard();
}

void SkeletalFishGuardHolder::movement() {
    if (mIsValid) {
        checkRailChangedAndReset();
        s32 appearableGuard = countAppearableGuard();
        if (appearableGuard > 0) {
            checkDistanceAndAppear(appearableGuard);
        }
    }
}

void SkeletalFishGuardHolder::forceAppearAll() {
    s32 appearableGuard = countAppearableGuard();

    for (s32 i = 0; i < mNumGuards; i++) {
        if (MR::isDead(mGuards[i])) {
            const s32 appearWaitStep = mFishBoss->getLevelStatus()->mStatusArray[i].mGuardWaitLevelID;
            SkeletalFishBossInfo::LevelStatus* pLevel = mFishBoss->getLevelStatus();
            SkeletalFishGuard* pGuard = mGuards[i];
            pGuard->mGuardOffset.set(pLevel->mStatusArray[i].mGuardPosLevel);
            pGuard->mAppearWaitStep = appearWaitStep;
            mGuards[i]->appearForce();

            if (--appearableGuard <= 0) {
                break;
            }
        }
    }
}

void SkeletalFishGuardHolder::forceKillAll() {
    for (s32 i = 0; i < mNumGuards; i++) {
        mGuards[i]->makeActorDead();
    }
}

void SkeletalFishGuardHolder::killAll() {
    for (s32 i = 0; i < mNumGuards; i++) {
        mGuards[i]->killNaturally();
    }
}

void SkeletalFishGuardHolder::notifyAttack(SkeletalFishGuard* pGuard) {
    for (s32 i = 0; i < mNumGuards; i++) {
        SkeletalFishGuard* guard = mGuards[i];

        if (guard != pGuard) {
            guard->waitAttack(120);
        }
    }
}

void SkeletalFishGuardHolder::invalidate() {
    mIsValid = 0;
}

void SkeletalFishGuardHolder::validate() {
    if (!mIsValid) {
        mIsValid = 1;
        mAppearRailCoord = MR::getRailCoord(mFishBoss->getCurrentBossRail());
    }
}

void SkeletalFishGuardHolder::checkRailChangedAndReset() {
    if (mBossRail != mFishBoss->getCurrentBossRail()) {
        mAppearRailCoord = MR::getRailCoord(mFishBoss->getCurrentBossRail());
        mBossRail = static_cast< SkeletalFishBossRail* >(mFishBoss->getCurrentBossRail());
    }
}

s32 SkeletalFishGuardHolder::countAppearableGuard() const {
    SkeletalFishBossInfo::LevelStatus* lvl = mFishBoss->mBossInfo->getLevelStatus(mFishBoss->mLevel);
    s32 guardCount = lvl->mGuardAppearNumLevel - countGuardAlive();

    if (guardCount < 0) {
        guardCount = 0;
    }

    return guardCount;
}

s32 SkeletalFishGuardHolder::countGuardAlive() const {
    s32 aliveGuard = 0;

    for (s32 i = 0; i < mNumGuards; i++) {
        if (!MR::isDead(mGuards[i])) {
            aliveGuard++;
        }
    }

    return aliveGuard;
}

void SkeletalFishGuardHolder::checkDistanceAndAppear(s32 numGuards) {
    f32 guardOffsLvl = mFishBoss->mBossInfo->getLevelStatus(mFishBoss->mLevel)->mGuardOffsLevel;
    f32 coord = MR::getRailCoord(mBossRail);

    if (coord < mAppearRailCoord) {
        coord = coord + MR::getRailTotalLength(mBossRail);
    }

    for (s32 i = 0; i < MR::getRailPointNum(mBossRail); i++) {
        if (mBossRail->isFillUpPoint(i)) {
            f32 guardCoord = MR::repeat(MR::getRailPointCoord(mBossRail, i) - guardOffsLvl, 0.0f, MR::getRailTotalLength(mBossRail));

            if (guardCoord < mAppearRailCoord) {
                guardCoord = guardCoord + MR::getRailTotalLength(mBossRail);
            }

            // The boss passed this point's guard spot since the last check.
            if (guardCoord < coord) {
                appearGuard(numGuards, i);
                break;
            }
        }
    }

    mAppearRailCoord = MR::getRailCoord(mBossRail);
}

void SkeletalFishGuardHolder::appearGuard(s32 numGuards, s32 startGuard) {
    SkeletalFishGuard* guard;
    SkeletalFishBossInfo::LevelStatus* level;
    s32 pos = mBossRail->getFillUpNamePos(startGuard);
    s32 waitID;

    Vec3f guardPos;
    if (pos >= 0 && SkeletalFishBossFunc::copyGuardPos(&guardPos, pos)) {
        for (s32 i = 0; i < mNumGuards; i++) {
            if (MR::isDead(mGuards[i])) {
                mGuards[i]->mPosition.set(guardPos);
                waitID = mFishBoss->getLevelStatus()->mStatusArray[i].mGuardWaitLevelID;
                level = mFishBoss->getLevelStatus();
                guard = mGuards[i];
                guard->mGuardOffset.set(level->mStatusArray[i].mGuardPosLevel);
                guard->mAppearWaitStep = waitID;
                mGuards[i]->appearNaturally();

                if (--numGuards <= 0) {
                    break;
                }
            }
        }
    }
}

void SkeletalFishGuardHolder::createAndInitGuard() {
    mGuards = new SkeletalFishGuard*[mNumGuards];

    for (s32 i = 0; i < mNumGuards; i++) {
        mGuards[i] = new SkeletalFishGuard(mFishBoss, "スカルシャークガード");
        mGuards[i]->initWithoutIter();
    }
}
