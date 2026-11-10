#include "Game/Enemy/Snakehead.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/BenefitItemOneUp.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 140.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 250.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f7, "init.f7", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f8, "init.f8", 250.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f9, "init.f9", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f10, "init.f10", 250.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f11, "init.f11", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f12, "init.f12", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f13, "init.f13", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f14, "init.f14", 480.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f15, "init.f15", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f16, "init.f16", 680.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f17, "init.f17", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f18, "init.f18", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f19, "init.f19", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initAfterPlacement_f1, "initAfterPlacement.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initAfterPlacement_f2, "initAfterPlacement.f2", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_kill_f1, "kill.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_kill_f2, "kill.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_kill_f3, "kill.f3", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f2, "control.f2", 480.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f3, "control.f3", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWaylay_f1, "exeWaylay.f1", 2.0f);

namespace {
    struct SnakeheadData {
        s32 mWaitStep;
        s32 mRestStep;
        f32 mGoHomeSpeed;
        const char* mWaylayBck;
        const char* mWaitBck;
        const char* mForwardBck;
        const char* mForwardEffect;
        const char* mBackBck;
    };

    NEBULA_TUNABLE(f32, sDefaultForwardSpeed, 15.0f);
    NEBULA_TUNABLE(f32, sDefaultBackSpeed, 10.0f);
    const f32 sGoHomeSpeedWaylay = 20.0f;
    const f32 sGoHomeSpeedMove = 50.0f;
    const s32 sStepForWaitBig = NebulaFrameClock::stepsOfMilliseconds(500);
    const s32 sStepForRestBig = 100;
    const s32 sStepForWaitSmall = NebulaFrameClock::stepsOfSeconds(1);
    const s32 sStepForRestSmall = NebulaFrameClock::stepsOfSeconds(2);
    NEBULA_TUNABLE(f32, sStartCoord, 300.0f);  // Home: this far along the rail.
    NEBULA_TUNABLE(f32, sPushRadiusSmall, 100.0f);
    NEBULA_TUNABLE(f32, sPushRadiusBig, 300.0f);
    NEBULA_TUNABLE(f32, sNearHomeSlowDown, 1.5f);

    // By SnakeheadType.
    const SnakeheadData sSnakeheadDataTable[] = {
        {sStepForWaitBig, sStepForRestBig, sGoHomeSpeedWaylay, "StraightAppear", "StraightWait", "StraightForward", "StraightForwardSmoke", "StraightBack"},
        {sStepForWaitSmall, sStepForRestSmall, sGoHomeSpeedWaylay, "StraightAppear", "Wait", "StraightForward", nullptr, "StraightBack"},
        {sStepForWaitBig, sStepForRestBig, sGoHomeSpeedMove, nullptr, "Wait", "Forward", "ForwardSmoke", "Back"},
        {sStepForWaitSmall, sStepForRestSmall, sGoHomeSpeedMove, nullptr, "Wait", "Forward", nullptr, "Back"}};

    const SnakeheadData& getParam(SnakeheadType type) {
        return sSnakeheadDataTable[type];
    }
};  // namespace

namespace NrvSnakehead {
    NEW_NERVE(SnakeheadNrvWaylay, Snakehead, Waylay);
    NEW_NERVE(SnakeheadNrvWait, Snakehead, Wait);
    NEW_NERVE(SnakeheadNrvMoveForward, Snakehead, MoveForward);
    NEW_NERVE(SnakeheadNrvRest, Snakehead, Rest);
    NEW_NERVE(SnakeheadNrvMoveBack, Snakehead, MoveBack);
    NEW_NERVE(SnakeheadNrvTurtleDown, Snakehead, TurtleDown);
    NEW_NERVE_ONEND(SnakeheadNrvPointerSwoon, Snakehead, PointerSwoon, PointerSwoon);
};  // namespace NrvSnakehead

Snakehead::Snakehead(const char* pName)
    : LiveActor(pName), mController(nullptr), mStarPointer(nullptr), mSmokePos(0.0f, 0.0f, 0.0f), mForwardSpeed(sDefaultForwardSpeed),
      mBackwardSpeed(sDefaultBackSpeed), mActivationRadius(1000.0f), mRailCenter(0.0f, 0.0f, 0.0f), mType(SnakeheadType_BigWait), mItem(nullptr) {
    mBodyMtx.identity();
}

// Args: 0 forward speed, 1 back speed, 2 the player's distance from the rail that wakes it, 3 = 1 to swim back and forth
// without waiting for the player (a waylaying one hides until the player comes near).
void Snakehead::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    const char* objectName;
    MR::getObjectName(&objectName, rIter);
    initModelManagerWithAnm(objectName, nullptr, false);

    MR::getPlacementArg0NoInit(rIter, &mForwardSpeed);
    MR::getPlacementArg1NoInit(rIter, &mBackwardSpeed);
    MR::getPlacementArg2NoInit(rIter, &mActivationRadius);

    s32 moveType = 0;
    MR::getPlacementArg3NoInit(rIter, &moveType);
    // Only 1 moves: another value would index past the table.
    mType = static_cast< SnakeheadType >(MR::isEqualString(objectName, "SnakeheadSmall") + (moveType == 1 ? 2 : 0));

    MR::connectToSceneEnemy(this);

    if (isSmall()) {
        initHitSensor(2);
        MR::addHitSensorAtJointEnemy(this, "head", "Head", 8, cNum_init_f1, Vec3f(cNum_init_f2, -cNum_init_f3, 0.0f));
        MR::addHitSensorAtJointEnemy(this, "body", "Body03", 8, cNum_init_f4, Vec3f(0.0f, 0.0f, 0.0f));
    } else {
        initHitSensor(3);
        MR::addHitSensorAtJointEnemy(this, "upper_jaw", "UpperJaw", 8, cNum_init_f5, Vec3f(cNum_init_f6, 0.0f, 0.0f));
        MR::addHitSensorAtJointEnemy(this, "lower_jaw", "LowerJaw", 8, cNum_init_f7, Vec3f(cNum_init_f8, 0.0f, 0.0f));
        MR::addHitSensorAtJointEnemy(this, "body", "Body03", 8, cNum_init_f9, Vec3f(0.0f, 0.0f, 0.0f));
    }

    initEffectKeeper(1, nullptr);

    const char* smokeName = getParam(mType).mForwardEffect;
    if (smokeName != nullptr) {
        mSmokePos = mPosition;
        MR::setEffectHostSRT(this, smokeName, &mSmokePos, nullptr, nullptr);
    }

    initSound(4, false);

    if (isSmall()) {
        MR::initStarPointerTarget(this, cNum_init_f10, Vec3f(0.0f, 0.0f, cNum_init_f11));
    } else {
        MR::initStarPointerTarget(this, cNum_init_f12, Vec3f(0.0f, 0.0f, cNum_init_f13));
    }

    mController = new AnimScaleController(nullptr);
    mController->setParamTight();
    mStarPointer = new WalkerStateBindStarPointer(this, mController);

    mBodyMtx.set(MR::getJointMtx(this, "Body04"));

    if (MR::isEqualString(objectName, "Snakehead")) {
        MR::initShadowController(this, 2);
        MR::addShadowVolumeFlatModel(this, "Head", "SnakeheadShadow", mBodyMtx);
        MR::addShadowVolumeBox(this, "Body", Vec3f(cNum_init_f14, cNum_init_f15, cNum_init_f16));
    } else {
        MR::initShadowVolumeFlatModel(this, "SnakeheadSmallShadow", mBodyMtx);
        MR::setShadowDropLength(this, "SnakeheadSmallShadow", cNum_init_f17);
    }

    MR::initLightCtrl(this);

    initRailRider(rIter);
    MR::initAndSetRailClipping(&mRailCenter, this, cNum_init_f18, cNum_init_f19);
    MR::setRailCoord(this, sStartCoord);
    MR::moveTransToCurrentRailPos(this);

    MR::addToAttributeGroupSearchTurtle(this);

    if (isSmall()) {
        MR::declareStarPiece(this, 9);
    } else {
        mItem = MR::createKinokoOneUp();
    }

    if (isMove()) {
        initNerve(GET_NERVE(Snakehead, SnakeheadNrvWait));
    } else {
        initNerve(GET_NERVE(Snakehead, SnakeheadNrvWaylay));
        MR::hideModel(this);
        MR::invalidateShadow(this, nullptr);
    }

    makeActorAppeared();
}

// The forward smoke sits on the ground under where the head will be at the rail's end.
void Snakehead::initAfterPlacement() {
    Vec3f headAtEnd;
    MR::copyJointPos(this, "Head", &headAtEnd);
    headAtEnd -= MR::getRailPointPosStart(this);
    headAtEnd += MR::getRailPointPosEnd(this);

    Vec3f rayStart;
    rayStart.scaleAdd(-cNum_initAfterPlacement_f1, mGravity, headAtEnd);
    MR::getFirstPolyOnLineToMapExceptSensor(&mSmokePos, nullptr, rayStart, mGravity * cNum_initAfterPlacement_f2, getSensor("body"));

    mBodyMtx.set(MR::getJointMtx(this, "Body04"));
}

void Snakehead::kill() {
    MR::startSound(this, "SE_EM_EXPLODE_UNDER_WATER");
    MR::emitEffect(this, "Death");

    if (isSmall()) {
        Vec3f headPos;
        MR::copyJointPos(this, "Head", &headPos);
        MR::appearStarPiece(this, headPos, 9, cNum_kill_f1, cNum_kill_f2, false);
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST_W");
    } else {
        Vec3f antennaPos;
        MR::copyJointPos(this, "Antenna", &antennaPos);

        Pos3f itemMtx;
        itemMtx.set(getBaseMtx());
        itemMtx.setTrans(antennaPos);
        MR::appearKinokoOneUpPop(mItem, itemMtx, cNum_kill_f3);
    }

    LiveActor::kill();
}

void Snakehead::control() {
    mController->updateNerve();
    tryPointerSwoon();

    Vec3f bodyPos;
    MR::copyJointPos(this, "Body04", &bodyPos);
    mBodyMtx.setTrans(bodyPos);

    // The big one's body shadow box spans Body01 to Body04.
    if (!isSmall()) {
        Vec3f body01Pos;
        MR::copyJointPos(this, "Body01", &body01Pos);

        f32 length = bodyPos.distance(body01Pos);
        Vec3f bodyMid;
        bodyMid.add(bodyPos, body01Pos);
        bodyMid.scale(cNum_control_f1);
        MR::setShadowDropPosition(this, "Body", bodyMid);
        MR::setShadowVolumeBoxSize(this, "Body", Vec3f(cNum_control_f2, cNum_control_f3, length));
    }
}

void Snakehead::calcAndSetBaseMtx() {
    Vec3f endPntDir;
    MR::calcRailEndPointDirection(&endPntDir, this);

    Pos3f frontUp;
    MR::makeMtxFrontUpPos(&frontUp, endPntDir, -mGravity, mPosition);
    MR::setBaseTRMtx(this, frontUp);

    Vec3f scale = mController->mScale * mScale;
    MR::setBaseScale(this, scale);
}

// Hidden on frame 0 until the player is near, then plays the appear animation (shadow on from frame 2) and lunges.
void Snakehead::exeWaylay() {
    if (MR::isFirstStep(this)) {
        choiceAndStartBck(Bck_Waylay);
        MR::setBckFrameAndStop(this, 0.0f);
        mBodyMtx.set(MR::getJointMtx(this, "Body04"));
    }

    if (isNearPlayerFromRail() && MR::getBckFrame(this) == 0.0f) {
        choiceAndStartBck(Bck_Waylay);
        MR::setBckFrame(this, 1.0f);
        MR::showModel(this);
    }

    if (MR::getBckFrame(this) == cNum_exeWaylay_f1) {
        MR::validateShadow(this, nullptr);
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Snakehead, SnakeheadNrvMoveForward));
    }
}

void Snakehead::exeWait() {
    if (MR::isFirstStep(this)) {
        choiceAndStartBck(Bck_Wait);
    }

    if (MR::isGreaterEqualStep(this, getParam(mType).mWaitStep) && (isMove() || isNearPlayerFromRail())) {
        setNerve(GET_NERVE(Snakehead, SnakeheadNrvMoveForward));
    }
}

void Snakehead::exeMoveForward() {
    if (MR::isFirstStep(this)) {
        choiceAndStartBck(Bck_Forward);
        MR::setRailDirectionToEnd(this);
        MR::startSound(this, "SE_EM_SNAKEHEAD_FORWARD");
    }

    if (!isMove() && MR::isRailReachedNearGoal(this, sStartCoord)) {
        MR::moveCoordAndFollowTrans(this, mForwardSpeed / sNearHomeSlowDown);
    } else {
        MR::moveCoordAndFollowTrans(this, mForwardSpeed);
    }

    if (MR::isRailReachedGoal(this)) {
        setNerve(GET_NERVE(Snakehead, SnakeheadNrvRest));
    }
}

void Snakehead::exeRest() {
    if (MR::isStep(this, getParam(mType).mRestStep)) {
        setNerve(GET_NERVE(Snakehead, SnakeheadNrvMoveBack));
    }
}

void Snakehead::exeMoveBack() {
    if (MR::isFirstStep(this)) {
        choiceAndStartBck(Bck_Back);
        MR::setRailDirectionToStart(this);
        MR::startSound(this, "SE_EM_SNAKEHEAD_BACKWARD");
    }

    MR::moveCoordAndFollowTrans(this, mBackwardSpeed);

    if (MR::isRailReachedNearGoal(this, sStartCoord)) {
        MR::setRailCoord(this, sStartCoord);
        MR::moveTransToCurrentRailPos(this);
        setNerve(GET_NERVE(Snakehead, SnakeheadNrvWait));
    }
}

// Hit by a shell: pulls back home while the damage animation plays, then dies.
void Snakehead::exeTurtleDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Damage");
        MR::setRailDirectionToStart(this);
        MR::startSound(this, "SE_EV_SNAKEHEAD_DAMAGE");
    }

    f32 goHomeSpeed = getParam(mType).mGoHomeSpeed;
    if (MR::isRailReachedNearGoal(this, sStartCoord + goHomeSpeed)) {
        MR::setRailCoord(this, sStartCoord);
        MR::moveTransToCurrentRailPos(this);
    } else {
        MR::moveCoordAndFollowTrans(this, goHomeSpeed);
    }

    if (MR::isBckStopped(this)) {
        kill();
    }
}

// The head sensors attack (or push when swooning); the body attacks or pushes as a capsule along the rail: the push is
// sent from the nearest point on the rail with the capsule's radius.
void Snakehead::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(Snakehead, SnakeheadNrvTurtleDown)) || !MR::isSensorEnemy(pSender)) {
        return;
    }

    if (!MR::isSensorPlayerOrRide(pReceiver) && !MR::isSensorEnemy(pReceiver)) {
        return;
    }

    HitSensor* pBody = getSensor("body");
    if (pSender != pBody) {
        if (isNerve(GET_NERVE(Snakehead, SnakeheadNrvPointerSwoon)) || !MR::sendMsgEnemyAttackStrong(pReceiver, pSender)) {
            MR::sendMsgPush(pReceiver, pSender);
        }

        return;
    }

    Vec3f footOnRail;
    MR::calcPerpendicFootToLineInside(&footOnRail, pReceiver->mPosition, MR::getRailPointPosStart(this), MR::getRailPointPosEnd(this));

    Vec3f fromRail;
    fromRail.sub(pReceiver->mPosition, footOnRail);

    f32 pushRadius = isSmall() ? sPushRadiusSmall : sPushRadiusBig;
    if (fromRail.length() > pushRadius) {
        return;
    }

    if (!isNerve(GET_NERVE(Snakehead, SnakeheadNrvPointerSwoon)) && MR::sendMsgEnemyAttackStrong(pReceiver, pSender)) {
        return;
    }

    Vec3f bodyPos(pBody->mPosition);
    f32 bodyRadius = pBody->mRadius;
    pBody->mPosition.set(footOnRail);
    pBody->mRadius = pushRadius;
    MR::sendMsgPush(pReceiver, pSender);
    pBody->mPosition.set(bodyPos);
    pBody->mRadius = bodyRadius;
}

bool Snakehead::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (isNerve(GET_NERVE(Snakehead, SnakeheadNrvWaylay))) {
        return false;
    }

    if (MR::isMsgStarPieceReflect(msg)) {
        return true;
    }

    if (isNerve(GET_NERVE(Snakehead, SnakeheadNrvTurtleDown))) {
        return false;
    }

    if (MR::isMsgJetTurtleAttack(msg)) {
        setNerve(GET_NERVE(Snakehead, SnakeheadNrvTurtleDown));
        return true;
    }

    return false;
}

// The type's animation for `kind` (none for some types); its texture animation if it has one, else Wait's held on frame 0.
void Snakehead::choiceAndStartBck(BckKind kind) {
    const SnakeheadData& rParam = getParam(mType);
    const char* pBck = nullptr;
    switch (kind) {
    case Bck_Forward:
        pBck = rParam.mForwardBck;
        break;
    case Bck_Back:
        pBck = rParam.mBackBck;
        break;
    case Bck_Waylay:
        pBck = rParam.mWaylayBck;
        break;
    case Bck_Wait:
        pBck = rParam.mWaitBck;
        break;
    }

    if (pBck == nullptr) {
        return;
    }

    if (kind == Bck_Waylay) {
        MR::startBckNoInterpole(this, pBck);
    } else {
        MR::startBck(this, pBck);
    }

    if (MR::isExistBtk(this, pBck)) {
        MR::startBtk(this, pBck);
    } else if (MR::isExistBtk(this, "Wait")) {
        MR::startBtk(this, "Wait");
        MR::setBtkFrameAndStop(this, 0.0f);
    }
}

bool Snakehead::tryPointerSwoon() {
    if (isNerve(GET_NERVE(Snakehead, SnakeheadNrvPointerSwoon)) || isNerve(GET_NERVE(Snakehead, SnakeheadNrvWaylay)) ||
        isNerve(GET_NERVE(Snakehead, SnakeheadNrvTurtleDown))) {
        return false;
    }

    if (!mStarPointer->tryStartPointBind()) {
        return false;
    }

    setNerve(GET_NERVE(Snakehead, SnakeheadNrvPointerSwoon));
    return true;
}

bool Snakehead::isNearPlayerFromRail() const {
    Vec3f nearestPos;
    MR::calcNearestRailPos(&nearestPos, this, *MR::getPlayerPos());
    return nearestPos.distance(*MR::getPlayerPos()) <= mActivationRadius;
}

void Snakehead::endPointerSwoon() {
    mStarPointer->kill();
}

void Snakehead::exePointerSwoon() {
    MR::updateActorStateAndNextNerve(this, mStarPointer, GET_NERVE(Snakehead, SnakeheadNrvMoveBack));
}
