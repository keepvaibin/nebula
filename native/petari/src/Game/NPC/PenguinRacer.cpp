#include "Game/NPC/PenguinRacer.hpp"
#include "Game/LiveActor/Binder.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/Map/RaceRail.hpp"
#include "Game/Map/WaterInfo.hpp"
#include "Game/NPC/NPCActorItem.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/NPCUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 130.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initShadow_f1, "initShadow.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRaceGoal_f1, "exeRaceGoal.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRaceGoal_f2, "exeRaceGoal.f2", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRaceTurn_f1, "exeRaceTurn.f1", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateMove_f1, "updateMove.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateVelocity_f1, "updateVelocity.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_prepRacer_f1, "prepRacer.f1", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_prepRacer_f2, "prepRacer.f2", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRacer_f1, "updateRacer.f1", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRacer_f2, "updateRacer.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_resetRacer_f1, "resetRacer.f1", 80.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeRaceWait_attenuateVelocity_1, "exeRaceWait.attenuateVelocity.1", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRaceFall_attenuateVelocity_1, "exeRaceFall.attenuateVelocity.1", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRaceGoal_attenuateVelocity_1, "exeRaceGoal.attenuateVelocity.1", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRaceGoal_addVelocityToGravity_1, "exeRaceGoal.addVelocityToGravity.1", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateMove_addVelocityToGravity_1, "updateMove.addVelocityToGravity.1", 1.0f);

namespace {
    NEBULA_TUNABLE(f32, sGoalBrake, 0.005f);
    NEBULA_TUNABLE(f32, sFallBrake, 0.002f);
    NEBULA_TUNABLE(s32, sTurnFrame, 60);
    NEBULA_TUNABLE(s32, sFallFrame, 240);
    NEBULA_TUNABLE(f32, sSensorRadius, 80.0f);
    NEBULA_TUNABLE(f32, sNearClipping, 200.0f);
    NEBULA_TUNABLE(f32, sSwimSoundPlayDistance, 500.0f);
};  // namespace

namespace NrvPenguinRacer {
    NEW_NERVE(PenguinRacerNrvWait, PenguinRacer, Wait);
    NEW_NERVE(PenguinRacerNrvReaction, PenguinRacer, Reaction);
    NEW_NERVE(PenguinRacerNrvRacePrep, PenguinRacer, RaceWait);
    NEW_NERVE(PenguinRacerNrvRaceWalk, PenguinRacer, RaceWalk);
    NEW_NERVE(PenguinRacerNrvRaceSwim, PenguinRacer, RaceSwim);
    NEW_NERVE(PenguinRacerNrvRaceStoW, PenguinRacer, RaceStoW);
    NEW_NERVE(PenguinRacerNrvRaceWtoS, PenguinRacer, RaceWtoS);
    NEW_NERVE(PenguinRacerNrvRaceFall, PenguinRacer, RaceFall);
    NEW_NERVE(PenguinRacerNrvRaceGoal, PenguinRacer, RaceGoal);
    NEW_NERVE(PenguinRacerNrvRaceTurn, PenguinRacer, RaceTurn);
};  // namespace NrvPenguinRacer

PenguinRacer::PenguinRacer(const char* pName) : NPCActor(pName), mRaceDisabled(), mMoveSpeed(0.0f), mSpeed(1.0f), mRailPointNo() {
}

void PenguinRacer::init(const TableIter& rIter) {
    MR::initDefaultPose(this, rIter);
    initModel();

    MR::connectToSceneNpc(this);
    MR::initLightCtrl(this);

    initSensor();

    initSound(8, false);
    initShadow();
    initBind();
    initEffectKeeper(0, nullptr);
    MR::onCalcGravity(this);

    initColor(rIter);

    Vec3f target;
    target.set(0.0f);
    MR::initStarPointerTarget(this, cNum_init_f1, target);
    initEtc(rIter);

    mMsgCtrl = MR::createTalkCtrl(this, rIter, "PenguinRacer", Vec3f(0.0f, cNum_init_f2, 0.0f), nullptr);
    MR::onRootNodeAutomatic(mMsgCtrl);

    initNerve(GET_NERVE(PenguinRacer, PenguinRacerNrvWait));

    if (mRaceDisabled == 0) {
        initRailRider(rIter);
        RaceManagerFunction::entryRacerOthers(this);
    }

    MR::addToAttributeGroupSearchTurtle(this);
    mLodCtrl = MR::createLodCtrlNPC(this, rIter);
    makeActorAppeared();

    NPCActorItem item("PenguinRacer");
    MR::getNPCItemData(&item, 0);
    equipment(item, false);
}

void PenguinRacer::initModel() {
    initModelManagerWithAnm("Penguin", nullptr, false);
}

void PenguinRacer::initSensor() {
    initHitSensor(1);
    MR::addHitSensorPush(this, "push", 8, ::sSensorRadius, Vec3f(0.0f, ::sSensorRadius, 0.0f));
}

void PenguinRacer::initShadow() {
    MR::initShadowVolumeSphere(this, cNum_initShadow_f1);
}

void PenguinRacer::initBind() {
    initBinder(::sSensorRadius, ::sSensorRadius, 0);
}

void PenguinRacer::initColor(const TableIter& rIter) {
    s32 color = 0;
    MR::getPlacementArg7NoInit(rIter, &color);

    MR::startBrk(this, "ColorChange");
    MR::setBrkFrameAndStop(this, color);
}

void PenguinRacer::initEtc(const TableIter& rIter) {
    mParam.setMoveAction("RacerWait", "Turn");
    mParam.setTalkAction("RacerTalk", "TalkTurn");
    setDefaults();
    MR::getPlacementArg0NoInit(rIter, &mRaceDisabled);
}

bool PenguinRacer::tryReaction() {
    return MR::tryStartReactionAndPushNerve(this, GET_NERVE(PenguinRacer, PenguinRacerNrvReaction));
}

void PenguinRacer::exeReaction() {
    if (mIsTrampledStart) {
        MR::startSound(this, "SE_SM_PENGUIN_TRAMPLED");
        MR::startSound(this, "SE_SV_PENGUIN_S_TRAMPLED");
    }

    if (isPointingSe()) {
        MR::startSound(this, "SE_SV_PENGUIN_S_POINTED_2P");
        MR::startPointerHitSound();
    }

    if (mIsSpinAttackedStart) {
        MR::startSound(this, "SE_SV_PENGUIN_S_SPIN_HIT");
    }

    if (mIsReactionStart) {
        MR::startSound(this, "SE_SV_PENGUIN_S_STAR_PIECE");
    }

    if (MR::tryStartReactionAndPopNerve(this)) {
        return;
    }
}

bool PenguinRacer::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if ((MR::isMsgJetTurtleAttack(msg) || MR::isMsgStarPieceReflect(msg)) && isInRace()) {
        setNerve(GET_NERVE(PenguinRacer, PenguinRacerNrvRaceFall));
        return true;
    }

    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return isInRace();
    }

    return NPCActor::receiveMsgPlayerAttack(msg, pSender, pReceiver);
}

void PenguinRacer::control() {
    if (mLodCtrl != nullptr) {
        if (MR::calcCameraDistanceZ(mPosition) < ::sNearClipping) {
            if (!MR::isHiddenModel(this)) {
                mLodCtrl->invalidate();
                MR::hideModelAndOnCalcAnim(this);
                if (mGoods0 != nullptr) {
                    MR::hideModel(mGoods0);
                }
                if (mGoods1 != nullptr) {
                    MR::hideModel(mGoods1);
                }
            }
        } else {
            if (MR::isHiddenModel(this)) {
                MR::showModel(this);
                if (mGoods0 != nullptr) {
                    MR::showModel(mGoods0);
                }
                if (mGoods1 != nullptr) {
                    MR::showModel(mGoods1);
                }
                mLodCtrl->validate();
            }
        }
    }
    NPCActor::control();
}

void PenguinRacer::exeWait() {
    MR::startLevelSound(this, "SE_SV_LV_PENGUIN_S_CHAT");

    if (!tryReaction()) {
        MR::tryTalkNearPlayerAndStartTalkAction(this);
    }
}

void PenguinRacer::exeRaceWait() {
    if (MR::isFirstStep(this)) {
        if (MR::isInWater(mPosition)) {
            MR::startAction(this, "RacerSwimWaitSurface");
        } else {
            MR::startAction(this, "RacerWait");
        }
        MR::setBckFrameAtRandom(this);
    }
    MR::attenuateVelocity(this, cNum_exeRaceWait_attenuateVelocity_1);
}

void PenguinRacer::exeRaceWalk() {
    if (MR::isFirstStep(this)) {
        if (!MR::isBckPlaying(this, "RacerLandSwim")) {
            MR::startAction(this, "RacerLandSwim");
            MR::setBckFrameAtRandom(this);
        }
    }

    MR::startLevelSound(this, "SE_SM_LV_PENGUIN_SLIDE");

    if (MR::isNearPlayer(this, ::sSwimSoundPlayDistance)) {
        MR::startLevelSound(this, "SE_SV_LV_PENGUIN_S_RACE_SWIM");
    }

    updateMove();
}

void PenguinRacer::exeRaceSwim() {
    if (MR::isFirstStep(this)) {
        if (!MR::isActionStart(this, "RacerSwim")) {
            MR::startAction(this, "RacerSwim");
            MR::setBckFrameAtRandom(this);
        }
    }

    if (MR::isNearPlayer(this, ::sSwimSoundPlayDistance)) {
        MR::startLevelSound(this, "SE_SV_LV_PENGUIN_S_RACE_SWIM");
    }

    updateMove();
}

void PenguinRacer::exeRaceStoW() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "RacerLandSwimSlow");
        MR::startSound(this, "SE_SM_PENGUIN_WATER_OUT");
    }

    updateMove();

    if (!isNerve(GET_NERVE(PenguinRacer, PenguinRacerNrvRaceStoW))) {
        MR::emitEffect(this, "WaterColumn");
    }
}

void PenguinRacer::exeRaceWtoS() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "RacerSwimSlow");
        MR::startSound(this, "SE_SM_PENGUIN_WATER_IN");
    }

    updateMove();

    if (!isNerve(GET_NERVE(PenguinRacer, PenguinRacerNrvRaceWtoS))) {
        MR::emitEffect(this, "WaterColumn");
    }
}

void PenguinRacer::exeRaceFall() {
    if (MR::isFirstStep(this)) {
        if (MR::isActionStart(this, "RacerSwim")) {
        }
        MR::startAction(this, "RacerSwimDamage");
        MR::startSound(this, "SE_SV_PENGUIN_S_DAMAGE_SHELL");
    }

    MR::startLevelSound(this, "SE_SM_LV_PENGUIN_S_SWOON");

    mSpeed -= ::sFallBrake;
    if (mSpeed < 0.0f) {
        mSpeed = 0.0f;
    }
    MR::attenuateVelocity(this, cNum_exeRaceFall_attenuateVelocity_1);

    updateMove();

    if (getNerveStep() >= ::sFallFrame) {
        mRailPointNo = MR::getCurrentRailPointNo(this);
        mMoveSpeed = getMoveSpeed();
        MR::startSound(this, "SE_SV_PENGUIN_S_HEAT_UP");
        setNerve(getMoveNerve());
    }
}

void PenguinRacer::exeRaceGoal() {
    mSpeed -= ::sGoalBrake;
    if (mSpeed < 0.0f) {
        mSpeed = 0.0f;
    }

    MR::attenuateVelocity(this, cNum_exeRaceGoal_attenuateVelocity_1);

    if (mVelocity.length() > cNum_exeRaceGoal_f1) {
        updateVelocity();

        if (getMoveNerve() != GET_NERVE(PenguinRacer, PenguinRacerNrvRaceSwim) && !MR::isBindedGround(this)) {
            MR::addVelocityToGravity(this, cNum_exeRaceGoal_addVelocityToGravity_1);
        }

        if (getMoveNerve() != GET_NERVE(PenguinRacer, PenguinRacerNrvRaceSwim)) {
            MR::onBind(this);
        }

        MR::decidePose(this, -mGravity, MR::getRailDirection(this), mPosition, 1.0f, cNum_exeRaceGoal_f2, 1.0f);
    } else {
        mVelocity.zero();
        setNerve(GET_NERVE(PenguinRacer, PenguinRacerNrvRaceTurn));
    }
}

void PenguinRacer::exeRaceTurn() {
    if (MR::isFirstStep(this)) {
        if (MR::isInWater(mPosition)) {
            MR::startAction(this, "RacerSwimWaitSurface");
        } else {
            MR::startAction(this, "RacerWait");
        }
    }

    if (getNerveStep() < ::sTurnFrame) {
        mRotation.y = MR::repeatDegree(mRotation.y + cNum_exeRaceTurn_f1);
    }
}

void PenguinRacer::updateMove() {
    updateVelocity();

    if (getMoveNerve() != GET_NERVE(PenguinRacer, PenguinRacerNrvRaceSwim) && !MR::isBindedGround(this)) {
        MR::addVelocityToGravity(this, cNum_updateMove_addVelocityToGravity_1);
    }

    if (getMoveNerve() != GET_NERVE(PenguinRacer, PenguinRacerNrvRaceSwim)) {
        MR::onBind(this);
    }

    updateRail();

    MR::decidePose(this, -mGravity, MR::getRailDirection(this), mPosition, 1.0f, cNum_updateMove_f1, 1.0f);
}

void PenguinRacer::updateVelocity() {
    MR::moveCoordToNearestPos(this, mPosition);
    Vec3f nextPointPos;
    MR::calcRailPointPos(&nextPointPos, this, MR::getNextRailPointNo(this));

    Vec3f toTarget = nextPointPos - mPosition;
    MR::normalizeOrZero(&toTarget);

    mVelocity.add(toTarget * mSpeed);

    Vec3f velV, velH;
    Vec3f grav = mGravity;

    velV.zero();
    velH.zero();

    if (getMoveNerve() != GET_NERVE(PenguinRacer, PenguinRacerNrvRaceSwim)) {
        MR::normalizeOrZero(&grav);
        const Vec3f& v = mVelocity;
        velV.scale(grav.dot(v), grav);
        velH.sub(v, velV);
    } else {
        velV.zero();
        velH.set(mVelocity);
    }

    f32 speedMaxV = mMoveSpeed;
    f32 speedMaxH = mMoveSpeed;

    if (getMoveNerve() != GET_NERVE(PenguinRacer, PenguinRacerNrvRaceSwim)) {
        speedMaxH = mMoveSpeed;
        speedMaxV = mMoveSpeed * cNum_updateVelocity_f1;
    }

    if (velV.squared() > speedMaxV * speedMaxV) {
        velV.setLength(speedMaxV);
    }

    if (velH.squared() > speedMaxH * speedMaxH) {
        velH.setLength(speedMaxH);
    }

    mVelocity.set(velV + velH);
}

void PenguinRacer::updateRail() {
    if (MR::getCurrentRailPointNo(this) <= mRailPointNo) {
        return;
    }

    mRailPointNo = MR::getCurrentRailPointNo(this);
    mMoveSpeed = getMoveSpeed();

    if (isNerve(GET_NERVE(PenguinRacer, PenguinRacerNrvRaceFall))) {
        return;
    }

    const Nerve* moveNerve = getMoveNerve();
    if (!isNerve(moveNerve)) {
        setNerve(moveNerve);
    }
}

const Nerve* PenguinRacer::getMoveNerve() const {
    // Note: 0 means rail section is a "swim" section, non-0 means section is a "walk" section

    s32 curr = 0;
    s32 next = 0;
    MR::getCurrentRailPointArg1NoInit(this, &curr);
    MR::getNextRailPointArg1NoInit(this, &next);

    if (curr != 0 && next != 0) {
        return GET_NERVE(PenguinRacer, PenguinRacerNrvRaceWalk);
    } else if (curr == 0 && next == 0) {
        return GET_NERVE(PenguinRacer, PenguinRacerNrvRaceSwim);
    } else if (curr != 0 && next == 0) {
        return GET_NERVE(PenguinRacer, PenguinRacerNrvRaceWtoS);
    } else {  // (curr == 0 && next != 0)
        return GET_NERVE(PenguinRacer, PenguinRacerNrvRaceStoW);
    }
}

f32 PenguinRacer::getMoveSpeed() const {
    f32 moveSpeed = mMoveSpeed;
    MR::getCurrentRailPointArg0NoInit(this, &moveSpeed);
    return moveSpeed;
}

bool PenguinRacer::isInRace() const {
    if (isNerve(GET_NERVE(PenguinRacer, PenguinRacerNrvRaceWalk)) || isNerve(GET_NERVE(PenguinRacer, PenguinRacerNrvRaceSwim)) ||
        isNerve(GET_NERVE(PenguinRacer, PenguinRacerNrvRaceStoW)) || isNerve(GET_NERVE(PenguinRacer, PenguinRacerNrvRaceWtoS))) {
        return true;
    }
    return false;
}

void PenguinRacer::initRacer() {
    AbstractRacer::initRacer();
}

void PenguinRacer::prepRacer(const RaceManager* /*pRaceManager*/) {
    MR::invalidateClipping(this);
    mBinder->mOffsetY = cNum_prepRacer_f1;

    MR::moveCoordToStartPos(this);
    MR::moveTransToCurrentRailPos(this);
    mRailPointNo = 0;
    mMoveSpeed = getMoveSpeed();
    mSpeed = 1.0f;

    MR::decidePose(this, -mGravity, MR::getRailDirection(this), mPosition, 1.0f, 1.0f, 1.0f);

    if (MR::isInWater(mPosition)) {
        MR::startAction(this, "RacerSwimSurfaceSoon");
    } else {
        Vec3f pos;
        Triangle triangle;
        if (MR::getFirstPolyOnLineToMap(&pos, &triangle, mPosition, mGravity * cNum_prepRacer_f2)) {
            mPosition.set(pos);
        }
        MR::startAction(this, "RacerWalkSoon");
    }

    MR::setBckFrameAtRandom(this);
    MR::resetPosition(this);
    MR::requestMovementOn(this);
    if (mGoods0 != nullptr) {
        MR::requestMovementOn(mGoods0);
    }
    if (mGoods1 != nullptr) {
        MR::requestMovementOn(mGoods1);
    }

    setNerve(GET_NERVE(PenguinRacer, PenguinRacerNrvRacePrep));
}

void PenguinRacer::startRacer() {
    setNerve(getMoveNerve());
}

bool PenguinRacer::updateRacer(const RaceManager* pRaceManager) {
    f32 coord = MR::calcNearestRailCoord(pRaceManager->mPlayerRacer, mPosition);
    f32 length = MR::getRailTotalLength(pRaceManager->mPlayerRacer);
    mPrevPosition.set(mCurrPosition);
    mCurrPosition.set(mPosition);
    if (MR::fabs(mRailCoord - coord) < cNum_updateRacer_f1) {
        mRailCoord = coord;
    }

    if (MR::fabs(mRailCoord - length) < cNum_updateRacer_f2) {
        return pRaceManager->isGoal(this);
    }

    return false;
}

bool PenguinRacer::goalRacer() {
    setNerve(GET_NERVE(PenguinRacer, PenguinRacerNrvRaceGoal));
    return false;
}

void PenguinRacer::loseRacer() {
    setNerve(GET_NERVE(PenguinRacer, PenguinRacerNrvRacePrep));
}

void PenguinRacer::resetRacer(const RaceManager* /*pRaceManager*/) {
    MR::onBind(this);
    mBinder->mOffsetY = cNum_resetRacer_f1;
    MR::setDefaultPose(this);
    mVelocity.zero();
    MR::calcGravity(this);

    if (MR::isInWater(mPosition)) {
        MR::startAction(this, "RacerSwimWaitSoon");
    } else {
        MR::startAction(this, "RacerWaitSoon");
    }

    MR::deleteEffectAll(this);
    setNerve(GET_NERVE(PenguinRacer, PenguinRacerNrvWait));
}

void PenguinRacer::exitRacer() {
    MR::validateClipping(this);
}
