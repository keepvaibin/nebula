#include "Game/MapObj/SpiderCoin.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/MapObj/SpiderThread.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <nebula_geometry.hpp>
#include <nebula_math.h>
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sCoinSpeed = 20.0f;
    const s32 sStepToTouch2nd = 5;
    static const s32 sStepToucnAndApart = NebulaFrameClock::stepsOfMilliseconds(500);
    const f32 sRotateSpeedY = 30.0f;
    const f32 sPointerSpeedMin = 5.0f;
    const f32 sPointerSpeedDotToKill = -20.0f;
    const f32 sModelOffset = 50.0f;
    const f32 sCocoonOffsetY = 20.0f;
    const f32 sCocoonOffsetZ = 20.0f;
};  // namespace

namespace NrvSpiderCoin {
    NEW_NERVE(SpiderCoinNrvWait, SpiderCoin, Wait);
    NEW_NERVE(SpiderCoinNrvTouch, SpiderCoin, Touch);
    NEW_NERVE(SpiderCoinNrvTouchAndApart, SpiderCoin, TouchAndApart);
};  // namespace NrvSpiderCoin

SpiderCoin::SpiderCoin(const char* pName)
    : LiveActor(pName), mPointPos(nullptr), mNeutralPos(nullptr), mUp(nullptr), mThreadZ(0.0f), mPad0Velocity(0.0f, 0.0f), mPad1Velocity(0.0f, 0.0f),
      mCocoonModel(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< SpiderCoin >);
    mBaseMtx.identity();
}

void SpiderCoin::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    mThreadZ = mPosition.z;
    initModelManagerWithAnm("Coin", nullptr, false);
    MR::connectToSceneItemStrongLight(this);

    initHitSensor(1);
    MR::addHitSensorCallbackMapObj(this, "body", 8, 50.0f);

    initEffectKeeper(8, nullptr);
    initNerve(GET_NERVE(SpiderCoin, SpiderCoinNrvWait));
    MR::initStarPointerTarget(this, 150.0f, Vec3f(0.0f, 80.0f, 0.0f));

    MR::declareCoin(this, 1);
    mCocoonModel = new PartsModel(this, "まゆ", "SpaceCocoon", mBaseMtx, MR::DrawBufferType_NoSilhouettedMapObj, false);
    mCocoonModel->mScale.set(0.3f);
    mCocoonModel->initWithoutIter();

    MR::registerDemoSimpleCastAll(this);
    MR::registerDemoSimpleCastAll(mCocoonModel);
    makeActorAppeared();
}

// The coin hangs on the spider web its boss sets up (TombSpiderEnvironment). In a scenario without the boss there is
// no web: they would hang in empty space and bind to the missing web (a crash in Space Junk's first star). With no web
// after placement, the coin and its cocoon leave the scene, as unseen.
void SpiderCoin::initAfterPlacement() {
    if (MR::isExistSceneObj(SceneObj_SpiderThread)) {
        return;
    }
    makeActorDead();
    if (mCocoonModel != nullptr) {
        mCocoonModel->makeActorDead();
    }
}

void SpiderCoin::kill() {
    LiveActor::kill();
    // A coin killed before it bound to the web (no up vector yet) pops with no sideways push.
    Vec3f vel(mUp != nullptr ? -mUp->x : 0.0f, mUp != nullptr ? -mUp->y : 0.0f, 0.0f);
    MR::normalizeOrZero(&vel);
    vel *= ::sCoinSpeed;
    Vec3f pos(mPosition.x, mPosition.y, mThreadZ);

    MR::appearCoinToVelocity(this, pos, vel, 1);
    MR::emitEffect(mCocoonModel, "Stick");
}

void SpiderCoin::exeWait() {
    if (MR::isFirstStep(this)) {
        if (mPointPos == nullptr) {
            MR::startActorBindToSpiderThread(this, &mPointPos, &mNeutralPos, &mUp, mPosition, Vec3f(0.0f, 0.0f, 0.0f));
        }
        mRotation.y = 0.0f;
    }
    mPosition.set(*mPointPos);

    if (MR::tryStarPointerCheck(this, true, "弱")) {
        setNerve(GET_NERVE(SpiderCoin, SpiderCoinNrvTouch));
    }
}

void SpiderCoin::exeTouch() {
    if (MR::isFirstStep(this)) {
        mPad0Velocity.y = 0.0f;
        mPad0Velocity.x = 0.0f;
        mPad1Velocity.y = 0.0f;
        mPad1Velocity.x = 0.0f;
    }

    mPosition.set(*mPointPos);
    mRotation.y += ::sRotateSpeedY;
    mRotation.y = MR::repeatDegree(mRotation.y);
    if (MR::isPressedJump(PLAYER_1) || MR::isPressedJump(PLAYER_2)) {
        kill();
        return;
    }

    if (!tryRub(PLAYER_1, &mPad0Velocity) && !tryRub(PLAYER_2, &mPad1Velocity) && !MR::isStarPointerPointing(this, PLAYER_1, true, "弱") &&
        !MR::isStarPointerPointing(this, PLAYER_2, true, "弱")) {
        setNerve(GET_NERVE(SpiderCoin, SpiderCoinNrvTouchAndApart));
    }
}

void SpiderCoin::exeTouchAndApart() {
    mPosition.set(*mPointPos);

    mRotation.y +=
        MR::getEaseOutValue((::sStepToucnAndApart - getNerveStep()) / static_cast< f32 >(::sStepToucnAndApart), 0.0f, 1.0f, 1.0f) * ::sRotateSpeedY;
    MR::repeatDegree(&mRotation.y);

    if (MR::isGreaterStep(this, ::sStepToTouch2nd) && MR::isStarPointerPointing(this, PLAYER_1, true, "弱")) {
        kill();
        return;
    }

    if (MR::isStep(this, ::sStepToucnAndApart)) {
        setNerve(GET_NERVE(SpiderCoin, SpiderCoinNrvWait));
    }
}

void SpiderCoin::calcAndSetBaseMtx() {
    if (mUp != nullptr) {
        Vec3f up(*mUp * -1.0f);
        MR::normalize(&up);

        Vec3f front(0.0f, 0.0f, 1.0f);
        Vec3f side = up.cross(front);
        MR::normalize(&side);
        front.cross(side, up);
        MR::normalize(&front);

        Pos3f mtx;
        mtx.identity();
        mtx.setXYZDir(side, up, front);

        Rot3f rotMtx;
        rotMtx.identity();
        MR::makeMtxRotateY(rotMtx, mRotation.y);
        MR::multMtx(mtx, rotMtx, mtx);

        Vec3f pos(up);
        pos.scale(-::sModelOffset);
        pos.add(mPosition);
        mtx.setTrans(pos);
        mBaseMtx.set(mtx);

        pos.set(up);
        pos.scale(-::sCocoonOffsetY);
        pos.add(mPosition);
        pos.add(front * ::sCocoonOffsetZ);
        mBaseMtx.setTrans(pos);
        MR::setBaseTRMtx(this, mtx);
    }
}

bool SpiderCoin::tryRub(s32 padChannel, Vec2f* pVel) {
    if (!MR::isStarPointerPointing(this, padChannel, true, "弱") || MR::getStarPointerScreenSpeed(padChannel) < ::sPointerSpeedMin) {
        return false;
    }

    Vec2f* vel = MR::getStarPointerScreenVelocity(padChannel);
    if (vel->x * pVel->x + vel->y * pVel->y < ::sPointerSpeedDotToKill) {
        kill();
        return true;
    }

    Vec2f* vel2 = MR::getStarPointerScreenVelocity(padChannel);
    pVel->x = vel2->x;
    pVel->y = vel2->y;
    return false;
}
