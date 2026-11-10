#include "Game/MapObj/WoodBox.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/BenefitItemOneUp.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHit_f1, "exeHit.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHit_f2, "exeHit.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHit_f3, "exeHit.f3", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHit_f4, "exeHit.f4", 30.0f);

namespace NrvWoodBox {
    NEW_NERVE(WoodBoxNrvWait, WoodBox, Wait);
    NEW_NERVE(WoodBoxNrvHit, WoodBox, Hit);
    NEW_NERVE(WoodBoxNrvKilled, WoodBox, Killed);
};  // namespace NrvWoodBox

WoodBox::WoodBox(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< WoodBox >);
}

void WoodBox::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("WoodBox", nullptr, false);
    MR::connectToSceneMapObjStrongLight(this);
    MR::initLightCtrl(this);
    initSound(6, false);
    initNerve(GET_NERVE(WoodBox, WoodBoxNrvWait));
    initEffectKeeper(0, nullptr);

    f32 radius = cNum_init_f1 * mScale.x;
    initHitSensor(1);
    Vec3f sensorOffset(0.0f, radius, 0.0f);
    MR::addHitSensor(this, "body", ATYPE_PUNCH_BOX, 8, radius, sensorOffset);

    s32 coinCount = 1;
    s32 respawn = -1;
    s32 starPieceCount = 0;
    s32 playSolveSE = 0;
    s32 hasOneUp = 0;
    s32 hasPowerStar = 0;

    if (rIter.isValid()) {
        MR::getPlacementArg0NoInit(rIter, &coinCount);
        MR::getPlacementArg1NoInit(rIter, &respawn);
        MR::getPlacementArg2NoInit(rIter, &starPieceCount);
        MR::getPlacementArg3NoInit(rIter, &playSolveSE);
        MR::getPlacementArg4NoInit(rIter, &hasOneUp);
        MR::getPlacementArg7NoInit(rIter, &hasPowerStar);
    }

    calcAndSetBaseMtx();
    NebulaMtxCopy(getBaseMtx(), mBaseMtx);
    MR::addTransMtx(mBaseMtx, Vec3f(0.0f, cNum_init_f2, 0.0f));

    mHasPowerStar = hasPowerStar != 0;

    if (mHasPowerStar) {
        MR::declarePowerStar(this);
        mStarDemoModel = MR::createPowerStarDemoModel(this, "パワースターデモモデル", mBaseMtx);
        mStarDemoModel->makeActorDead();
    }

    mCoinCount = coinCount;
    mStarBitCount = starPieceCount;
    mHitPoint = 1;
    mIsNoRespawn = respawn != 0;
    mPlaySolveSE = playSolveSE == 1;

    mBreakModel = new ModelObj("壊れモデル", "WoodBoxBreak", getBaseMtx(), MR::DrawBufferType_NoSilhouettedMapObjStrongLight, -2, -2, false);
    mBreakModel->mScale = mScale;
    MR::initLightCtrl(mBreakModel);
    mBreakModel->kill();
    mFloorTouchTimer = 0;

    setNerve(GET_NERVE(WoodBox, WoodBoxNrvWait));
    HitSensor* pBodySensor = getSensor("body");
    MR::initCollisionParts(this, "WoodBox", pBodySensor, nullptr);

    MR::setGroupClipping(this, rIter, 32);
    MR::setClippingFar200m(this);
    MR::validateClipping(this);

    if (mCoinCount != 0) {
        MR::declareCoin(this, mCoinCount);
    }

    if (mStarBitCount != 0) {
        MR::declareStarPiece(this, mStarBitCount);
    }

    if (hasOneUp != 0) {
        mOneUp = MR::createKinokoOneUp();
        mOneUp->mIsThrownHigh = 1;
    } else {
        mOneUp = nullptr;
    }

    MR::useStageSwitchWriteDead(this, rIter);
    MR::useStageSwitchSleep(this, rIter);

    appear();
}

void WoodBox::control() {
    if (mFloorTouchTimer > 0) {
        mFloorTouchTimer--;
    }
}

void WoodBox::calcViewAndEntry() {
    // A box past its distance that the camera still sees stays drawn (ClippingActorInfo::isDrawnWhileClipped), so its
    // view matrices follow the camera then too: skipped, the box kept the place on the screen it had when it stopped
    // (Beach Bowl's two sea-floor boxes hung in the sky through the flight in).
    if (!MR::isDead(this) && (!MR::isClipped(this) || mFlag.mIsDrawnClipped)) {
        LiveActor::calcViewAndEntry();
    }
}

bool WoodBox::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (mHitPoint == 0) {
        return false;
    }

    if (MR::isMsgPlayerUpperPunch(msg)) {
        doHit(pSender, pReceiver);
        pSender->receiveMessage(ACTMES_REFLECT_V, pReceiver);
        return true;
    }

    if (MR::isMsgFireBallAttack(msg)) {
        doHit(pSender, pReceiver);
        return false;
    }

    if (MR::isMsgPlayerHitAll(msg)) {
        doHit(pSender, pReceiver);
        return true;
    }

    if (msg == ACTMES_PLAYER_HIP_DROP || msg == ACTMES_PLAYER_HIP_DROP_FLOOR) {
        if (mFloorTouchTimer != 0) {
            doHit(pSender, pReceiver);
            return true;
        }

        return true;
    }

    return false;
}

bool WoodBox::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (msg == ACTMES_IS_PUNCH_ENABLE) {
        return mHitPoint != 0;
    }

    if (msg == ACTMES_IS_BROKEN) {
        return mHitPoint == 0;
    }

    if (msg == ACTMES_IS_RUSH_ENABLE && mHitPoint != 0) {
        return true;
    }

    if (msg == ACTMES_FLOOR_TOUCH) {
        mFloorTouchTimer = 2;
    }

    if (msg == ACTMES_IS_OPD_SITE_IN && mHitPoint != 0) {
        return true;
    }

    if ((msg == ACTMES_OPD_SHOOT || msg - ACTMES_BALL_DASH_WALL <= 1) && mHitPoint != 0) {
        doHit(pSender, pReceiver);
        return true;
    }

    if (mHitPoint != 0 && (msg - ACTMES_TURNIP_ATTACK <= 1)) {
        doHit(pSender, pReceiver);
        return true;
    }

    return false;
}

bool WoodBox::receiveMsgEnemyAttack(u32 /*msg*/, HitSensor* pSender, HitSensor* pReceiver) {
    if (mHitPoint != 0) {
        doHit(pSender, pReceiver);
        return true;
    }

    return false;
}

void WoodBox::kill() {
    if (!mIsNoRespawn) {
        LiveActor::kill();
    } else {
        LiveActor::setNerve(GET_NERVE(WoodBox, WoodBoxNrvKilled));
        MR::invalidateHitSensors(this);
        MR::invalidateCollisionParts(this);
    }

    mBreakModel->kill();

    if (mHasPowerStar) {
        mStarDemoModel->makeActorDead();
        MR::requestAppearPowerStar(this, mPosition);
    }
}

void WoodBox::exeWait() {
}
void WoodBox::exeHit() {
    if (LiveActor::getNerveStep() == 0) {
        LiveActor::getSensor("body")->invalidate();

        if (mHasPowerStar) {
            mStarDemoModel->makeActorAppeared();
        }
    }

    if (LiveActor::getNerveStep() == 15) {
        if (mCoinCount != 0) {
            MR::appearCoinPop(this, mPosition, mCoinCount);
        }

        if (mStarBitCount != 0) {
            MR::appearStarPiece(this, mPosition, mStarBitCount, cNum_exeHit_f1, cNum_exeHit_f2, false);
            MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
        }

        if (mOneUp != nullptr) {
            Vec3f upVec;
            MR::calcUpVec(&upVec, this);
            Mtx spawnMtx;
            NebulaMtxCopy(getBaseMtx(), spawnMtx);

            MR::addTransMtx(spawnMtx, upVec * cNum_exeHit_f3);

            MR::appearKinokoOneUpPop(mOneUp, spawnMtx, cNum_exeHit_f4);
        }

        if (MR::isValidSwitchDead(this)) {
            MR::onSwitchDead(this);
        }

        if (mPlaySolveSE) {
            MR::startSystemSE("SE_SY_READ_RIDDLE_S");
        }
    }

    if (MR::isBckStopped(mBreakModel)) {
        kill();
    }
}
void WoodBox::exeKilled() {
}

void WoodBox::doHit(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (mHitPoint != 0) {
        if (mHitPoint != 0) {
            mHitPoint--;
        }

        if (mHitPoint == 0) {
            MR::invalidateCollisionParts(this);
        }

        if (MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
            MR::startSound(this, "SE_OJ_WOOD_BOX_BREAK_W");
        } else {
            MR::startSound(this, "SE_OJ_WOOD_BOX_BREAK");
        }

        if (!MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
            MR::startSound(this, "SE_EM_EXPLODE_S");
        }

        mBreakModel->appear();
        MR::startBck(mBreakModel, "Break");

        if (MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
            MR::emitEffect(mBreakModel, "BreakWater");
        } else {
            MR::emitEffect(mBreakModel, "Break");
        }

        if (mIsNoRespawn) {
            MR::startBva(this, "WoodBox");
        } else {
            MR::hideModel(this);
        }

        setNerve(GET_NERVE(WoodBox, WoodBoxNrvHit));
    }
}
