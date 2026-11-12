#include "Game/Boss/KoopaFireStairs.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/KoopaBattleMapStair.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sFlyUpDirDegreeMax = 45.0f;
    const f32 sRotateSpeed = 15.0f;
    const f32 sArcHeight = 1500.0f;
    const f32 sHitEffectAhead = 100.0f;
};  // namespace

namespace NrvKoopaFireStairs {
    NEW_NERVE(KoopaFireStairsNrvFly, KoopaFireStairs, Fly);
    NEW_NERVE(KoopaFireStairsNrvBreak, KoopaFireStairs, Break);
};  // namespace NrvKoopaFireStairs

KoopaFireStairs::KoopaFireStairs(const char* pName, bool isKoopaJr)
    : LiveActor(pName), mIsKoopaJr(isKoopaJr), mStair(nullptr), mFront(0.0f, 0.0f, 1.0f), mStartPos(0.0f, 0.0f, 0.0f), mFlyUpDir(0.0f, 1.0f, 0.0f),
      mBreakModel(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< KoopaFireStairs >);
}

void KoopaFireStairs::init(const TableIter& /*rIter*/) {
    initModelManagerWithAnm("MeteorStrike", nullptr, false);
    MR::startBrk(this, "MeteorStrike");
    MR::connectToSceneEnemy(this);

    initHitSensor(1);
    MR::addHitSensorEnemyAttack(this, "Fire", 8, 100.0f, Vec3f(0.0f, 0.0f, 0.0f));
    initEffectKeeper(1, nullptr);
    MR::addEffectHitNormal(this, "Hit");
    MR::setEffectBaseScale(this, "Hit", 2.0f);

    initSound(4, false);
    MR::initShadowVolumeSphere(this, 60.0f);
    initNerve(GET_NERVE(KoopaFireStairs, KoopaFireStairsNrvFly));
    MR::invalidateClipping(this);

    mBreakModel = MR::createModelObjEnemy("クッパメテオ（壊れ）", "MeteorStrikeBreak", getBaseMtx());
    mBreakModel->kill();
    MR::invalidateClipping(mBreakModel);

    makeActorDead();
}

void KoopaFireStairs::appear() {
    LiveActor::appear();
    MR::showModel(this);
    MR::emitEffect(this, "MeteorStrike");
    MR::startSound(this, mIsKoopaJr ? "SE_BM_KOOPAJR_SHIP_METEORSHOT_F" : "SE_OJ_KOOPA_FIRE_SHOT");
    setNerve(GET_NERVE(KoopaFireStairs, KoopaFireStairsNrvFly));
}

void KoopaFireStairs::makeActorDead() {
    LiveActor::makeActorDead();
    mBreakModel->makeActorDead();
}

// Bowser's shot flies straight at the stair, arriving as it breaks. Bowser Jr.'s arcs over: with `pAxis`, the arc leans a
// random angle up to 45 degrees about it, toward the side of the stair's path.
void KoopaFireStairs::setInfo(const KoopaBattleMapStair* pStair, const Vec3f* pAxis) {
    mStair = pStair;
    mStartPos.set(mPosition);
    MR::calcGravity(this);

    if (pAxis != nullptr) {
        Vec3f toStair = mStair->mPosition - mStartPos;
        Vec3f toTarget = mStair->mTargetPos - mStartPos;
        Vec3f normal;
        normal.cross(toStair, toTarget);

        if (normal.dot(pStair->mGravity) > 0.0f) {
            MR::rotateVecDegree(&mFlyUpDir, *pAxis, MR::getRandom(0.0f, sFlyUpDirDegreeMax));
        } else {
            MR::rotateVecDegree(&mFlyUpDir, *pAxis, MR::getRandom(-sFlyUpDirDegreeMax, 0.0f));
        }
    }

    if (mIsKoopaJr) {
        mVelocity.zero();
        mFront.set(mStair->mTargetPos - mStartPos);
        MR::normalize(&mFront);
    } else {
        s32 breakTime = mStair->calcRemainTimeToBreak();
        Vec3f toTarget = mStair->mTargetPos - mPosition;
        f32 length = toTarget.length();

        mVelocity.set(toTarget);
        MR::normalize(&mVelocity);
        mFront.set(mVelocity);
        mVelocity.scale(length / (breakTime > 2 ? breakTime - 1 : 1));
    }
}

void KoopaFireStairs::exeFly() {
    if (mStair->isBreak()) {
        Vec3f hitPos(mVelocity);
        MR::normalizeOrZero(&hitPos);
        hitPos.scale(sHitEffectAhead);
        hitPos.add(mPosition);
        MR::emitEffectHit(this, hitPos, "Hit");
        MR::emitEffect(this, "MeteorStrikeBreak");
        mVelocity.zero();
        MR::startSound(this, "SE_OJ_KOOPA_FIRE_EXPLODE");
        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::shakeCameraWeak();
        MR::hideModel(this);
        setNerve(GET_NERVE(KoopaFireStairs, KoopaFireStairsNrvBreak));
        return;
    }

    if (mIsKoopaJr) {
        // Along the line by the stair's timer, plus a half sine up the arc; faces the way it moved.
        Vec3f prevPos(mPosition);
        f32 rate = mStair->calcTimeRate();
        mPosition = mStartPos * (1.0f - rate) + mStair->mTargetPos * rate;
        f32 arc = MR::sinDegree(180.0f * rate);
        mPosition.add(mFlyUpDir * arc * sArcHeight);

        Vec3f moved = mPosition - prevPos;
        if (!MR::isNearZero(moved)) {
            MR::normalize(&moved);
            mFront.set(moved);
        }

        MR::startLevelSound(this, "SE_BM_LV_KOOPAJR_SHIP_METEOR");
    }

    mRotation.x += sRotateSpeed;
    mRotation.x = MR::repeatDegree(mRotation.x);
}

void KoopaFireStairs::exeBreak() {
    if (MR::isFirstStep(this)) {
        mBreakModel->appear();
        MR::startAction(mBreakModel, "Break");
    }

    if (MR::isActionEnd(mBreakModel)) {
        kill();
    }
}

void KoopaFireStairs::calcAndSetBaseMtx() {
    Pos3f mtx;
    mtx.identity();
    MR::makeMtxFrontUpPos(&mtx, mFront, -mGravity, mPosition);
    MR::rotateMtxLocalXDegree(mtx, mRotation.x);
    MR::setBaseTRMtx(this, mtx);
}
