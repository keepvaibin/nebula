#include "Game/Enemy/JumpGuarder.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/ActorCameraInfo.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/MultiEventCamera.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f1, "updateRotate.f1", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f2, "updateRotate.f2", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f3, "updateRotate.f3", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f4, "updateRotate.f4", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f5, "updateRotate.f5", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f6, "updateRotate.f6", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRotate_f7, "updateRotate.f7", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 44.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHopStart_f1, "exeHopStart.f1", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHopEnd_f1, "exeHopEnd.f1", 145.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeOpen_f1, "exeOpen.f1", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeOpen_f2, "exeOpen.f2", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeOpen_f3, "exeOpen.f3", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 145.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 145.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 35.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 4.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeHopStart_1, "exeHopStart.1", 10);

namespace {
    NEBULA_TUNABLE(f32, sRotateDegree, 3.0f);
    static const s32 sHopStep = NebulaFrameClock::stepsOfSeconds(5);
    NEBULA_TUNABLE(s32, sOpenInt, 74);
    NEBULA_TUNABLE(s32, sAttack, 360);
    NEBULA_TUNABLE(s32, sBabyNum, 4);
    NEBULA_TUNABLE(f32, sAppearHeight, 500.0f);
    NEBULA_TUNABLE(f32, sAppearDistance, 2000.0f);
    NEBULA_TUNABLE(f32, sDisappearDistance, 2200.0f);
    NEBULA_TUNABLE(f32, sHeadOffset, -100.0f);
    NEBULA_TUNABLE(f32, sBabyVelocity, 16.0f);
    NEBULA_TUNABLE(f32, sBabyOffset, 64.0f);
    NEBULA_TUNABLE(f32, sShadowRadius, 140.0f);
    NEBULA_TUNABLE(f32, sHideShadowRadius, 110.0f);
    NEBULA_TUNABLE(s32, sHitInt, 60);
    NEBULA_TUNABLE(s32, sCoinDefault, 4);
    NEBULA_TUNABLE(f32, sCameraLimitLength, 6250000.0f);
    NEBULA_TUNABLE(s32, sOpen, 70);
};  // namespace

namespace NrvJumpGuarder {
    NEW_NERVE(JumpGuarderNrvHide, JumpGuarder, Hide);
    NEW_NERVE(JumpGuarderNrvUp, JumpGuarder, Up);
    NEW_NERVE(JumpGuarderNrvWait, JumpGuarder, Wait);
    NEW_NERVE(JumpGuarderNrvDown, JumpGuarder, Down);
    NEW_NERVE(JumpGuarderNrvHopStart, JumpGuarder, HopStart);
    NEW_NERVE(JumpGuarderNrvHopWait, JumpGuarder, HopWait);
    NEW_NERVE(JumpGuarderNrvHopJump, JumpGuarder, HopJump);
    NEW_NERVE(JumpGuarderNrvHopEnd, JumpGuarder, HopEnd);
    NEW_NERVE(JumpGuarderNrvPreOpen, JumpGuarder, PreOpen);
    NEW_NERVE(JumpGuarderNrvOpen, JumpGuarder, Open);
    NEW_NERVE(JumpGuarderNrvClose, JumpGuarder, Close);
    NEW_NERVE(JumpGuarderNrvInter, JumpGuarder, Inter);
};  // namespace NrvJumpGuarder

JumpEmitter::JumpEmitter(const char* pName)
    : LiveActor(pName), mBodyJointMtx(), mHeadModel(), mCameraCenter(0.0f, 0.0f, 0.0f), mIsEventCamera(), mCameraInfo(), mMultEventCamera() {
    lumacore::setClass(this, lumacore::kClassId< JumpEmitter >);
    mHeadMtx.identity();
}

void JumpEmitter::kill() {
    LiveActor::kill();
    endEventCamera();
}

void JumpEmitter::initEventCamera(const TableIter& rIter) {
    mCameraInfo = new ActorCameraInfo(rIter);

    if (mCameraInfo->mCameraSetID != -1) {
        mMultEventCamera = new MultiEventCamera();
        mMultEventCamera->setUp(mName, mCameraInfo, 2);
        mMultEventCamera->setEndCameraTypeSoon();
    }
}

void JumpEmitter::startEventCamera() {
    if (mMultEventCamera == nullptr) {
        return;
    }

    mMultEventCamera->start(0);
    mIsEventCamera = true;
    mCameraCenter.set(mPosition);
}

void JumpEmitter::updateEventCamera() {
    if (!mIsEventCamera) {
        return;
    }

    mMultEventCamera->update();

    if (mMultEventCamera->isActive(0) && !MR::isPlayerJumpRising()) {
        mMultEventCamera->nextForce();
    }

    if (::sCameraLimitLength < mCameraCenter.squared(*MR::getPlayerCenterPos()) || MR::isOnGroundPlayer() || MR::isPlayerInRush()) {
        endEventCamera();
    }
}

void JumpEmitter::endEventCamera() {
    if (mMultEventCamera == nullptr) {
        return;
    }

    if (mIsEventCamera) {
        mIsEventCamera = false;
        mMultEventCamera->endForceSoon();
    }
}

void JumpEmitter::updateRotate() {
    PartsModel* head = mHeadModel;

    Rot3f mtx;
    mtx.identity();
    mtx.set(getBaseMtx());
    mtx.invert(mtx);

    Vec3f playerPos(*MR::getPlayerPos());
    mtx.mult(playerPos, playerPos);

    playerPos.y = 0.0f;
    if (MR::isNearZero(playerPos)) {
        return;
    }

    MR::normalize(&playerPos);

    f32 newAngle;
    if (playerPos.z == 0.0f) {
        if (playerPos.x >= 0.0f) {
            newAngle = cNum_updateRotate_f1;
        } else {
            newAngle = -cNum_updateRotate_f2;
        }
    } else if (playerPos.z >= 0.0f) {
        newAngle = MR::toDegree(MR::atan2(playerPos.x, playerPos.z));
    } else {
        newAngle = cNum_updateRotate_f3 - MR::toDegree(MR::atan2(playerPos.x, -playerPos.z));
    }

    f32 angleDiff = MR::repeat(newAngle - head->mRotation.y, -cNum_updateRotate_f4, cNum_updateRotate_f5);

    if (MR::abs(angleDiff) < ::sRotateDegree) {
        head->mRotation.y = newAngle;
    } else {
        head->mRotation.y += ::sRotateDegree * MR::sign(angleDiff);
    }

    head->mRotation.y = MR::repeat(head->mRotation.y, -cNum_updateRotate_f6, cNum_updateRotate_f7);
}

bool JumpGuarder::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (MR::isMsgPlayerTrample(msg)) {
        if (pReceiver->isType(ATYPE_PLAYER_AUTO_JUMP)) {
            Vec3f upVec;
            MR::calcUpVec(&upVec, this);
            MR::setPlayerJumpVec(upVec);
            startEventCamera();
            setNerve(GET_NERVE(JumpGuarder, JumpGuarderNrvHopJump));
        } else {
            setNerve(GET_NERVE(JumpGuarder, JumpGuarderNrvHopStart));
        }

        return true;
    }

    if (MR::isMsgPlayerHipDrop(msg)) {
        MR::forceJumpPlayer(-mGravity);
        return true;
    }

    if (MR::isMsgPlayerSpinAttack(msg)) {
        if (!isNerve(GET_NERVE(JumpGuarder, JumpGuarderNrvHopStart))) {
            setNerve(GET_NERVE(JumpGuarder, JumpGuarderNrvHopStart));
        }

        return true;
    }

    return MR::isMsgStarPieceReflect(msg);
}

bool JumpGuarder::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_GROUP_ATTACK) {
        MR::invalidateClipping(this);
        setNerve(GET_NERVE(JumpGuarder, JumpGuarderNrvUp));
        return true;
    }

    if (msg == ACTMES_GROUP_HIDE) {
        setNerve(GET_NERVE(JumpGuarder, JumpGuarderNrvDown));
        return true;
    }

    return false;
}

void JumpGuarder::control() {
    PartsModel* head = mHeadModel;
    Mtx34f mtx;
    mtx.identity();

    MR::makeMtxTRS(mtx.mMtx, Vec3f(0.0f, cNum_control_f1, 0.0f), head->mRotation, head->mScale);

    mHeadMtx.set(mBodyJointMtx);

    Mtx34f headMtx;
    headMtx.concat(mHeadMtx, mtx);
    mHeadMtx.set(headMtx);

    mBumpCooldown = MR::max(mBumpCooldown - 1, 0);

    updateEventCamera();
}

void JumpGuarder::exeHide() {
    updateRotate();

    if (MR::isFirstStep(this)) {
        MR::startBck(mHeadModel, "Wait");
        MR::startBrk(mHeadModel, "Green");
        MR::validateClipping(this);
        MR::setShadowVolumeSphereRadius(this, nullptr, ::sHideShadowRadius);
    }

    if (MR::enableGroupAttack(this, ::sAppearDistance, ::sAppearHeight)) {
        MR::sendMsgToGroupMember(ACTMES_GROUP_ATTACK, this, getSensor("Body"), "Body");
    }
}

void JumpGuarder::exeUp() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Up");
        MR::startSound(this, "SE_EM_JGUARDER_APPEAR");
    }

    f32 bckEnd = static_cast< f32 >(MR::getBckCtrl(this)->getEnd());
    f32 ratio = bckEnd > 0.0f ? MR::getBckFrame(this) / bckEnd : 1.0f;
    MR::setShadowVolumeSphereRadius(this, nullptr, (::sShadowRadius - ::sHideShadowRadius) * ratio + ::sHideShadowRadius);

    if (MR::isBckStopped(this)) {
        MR::setShadowVolumeSphereRadius(this, nullptr, ::sShadowRadius);
        setNerve(GET_NERVE(JumpGuarder, JumpGuarderNrvWait));
    }
}

void JumpGuarder::exeWait() {
    updateRotate();

    if (!MR::enableGroupAttack(this, ::sDisappearDistance, ::sAppearHeight)) {
        MR::sendMsgToGroupMember(ACTMES_GROUP_HIDE, this, getSensor("Body"), "Body");
    } else if (enableAttack()) {
        setNerve(GET_NERVE(JumpGuarder, JumpGuarderNrvPreOpen));
    }
}

void JumpGuarder::exeDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Down");
        MR::startSound(this, "SE_EM_JGUARDER_HIDE");
    }

    f32 bckEnd = static_cast< f32 >(MR::getBckCtrl(this)->getEnd());
    f32 ratio = bckEnd > 0.0f ? 1.0f - MR::getBckFrame(this) / bckEnd : 0.0f;
    MR::setShadowVolumeSphereRadius(this, nullptr, (::sShadowRadius - ::sHideShadowRadius) * ratio + ::sHideShadowRadius);

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(JumpGuarder, JumpGuarderNrvHide));
    }
}

void JumpGuarder::exeHopStart() {
    if (MR::isFirstStep(this)) {
        MR::startBck(mHeadModel, "HopStart");
        MR::startBrk(mHeadModel, "OnAndOff");
        MR::startBck(this, "Damage");
        MR::startSound(this, "SE_EM_JGUARDER_HIT");
        HitSensor* body = getSensor("Body");
        body->mRadius = cNum_exeHopStart_f1;
    }

    MR::startLevelSound(this, "SE_EM_LV_JGUARDER_SHAKE");

    if (MR::isStep(this, cStep_exeHopStart_1)) {
        for (u32 i = 0; i < mNumActiveBabies; i++) {
            mActiveBabies[i]->makeActorDead();
        }

        mNumActiveBabies = 0;
    }

    if (MR::isBckStopped(mHeadModel)) {
        setNerve(GET_NERVE(JumpGuarder, JumpGuarderNrvHopWait));
    }
}

void JumpGuarder::exeHopWait() {
    if (MR::isFirstStep(this)) {
        MR::stopBck(this);
        MR::startBck(mHeadModel, "HopWait");
        getSensor("Jump")->validate();
    }

    MR::startLevelSound(this, "SE_EM_LV_JGUARDER_SHAKE");

    if (MR::isStep(this, ::sHopStep)) {
        setNerve(GET_NERVE(JumpGuarder, JumpGuarderNrvHopEnd));
    }
}

void JumpGuarder::exeHopJump() {
    if (MR::isFirstStep(this)) {
        MR::startBck(mHeadModel, "HopJump");
        MR::startSound(this, "SE_EM_JGUARDER_TRAMPLE");
    }

    if (MR::isBckStopped(mHeadModel)) {
        setNerve(GET_NERVE(JumpGuarder, JumpGuarderNrvHopWait));
    }
}

void JumpGuarder::exeHopEnd() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "HopEnd");
        MR::startBck(mHeadModel, "HopEnd");
        MR::startBrk(mHeadModel, "Green");
        MR::startSound(this, "SE_EM_JGUARDER_CLOSE_SPRING");
    }

    if (MR::isBckStopped(this) && MR::isBckStopped(mHeadModel)) {
        HitSensor* body = getSensor("Body");
        body->mRadius = cNum_exeHopEnd_f1;
        getSensor("Jump")->invalidate();
        setNerve(GET_NERVE(JumpGuarder, JumpGuarderNrvWait));
    }
}

void JumpGuarder::exePreOpen() {
    updateRotate();

    if (!MR::enableGroupAttack(this, ::sDisappearDistance, ::sAppearHeight)) {
        MR::sendMsgToGroupMember(ACTMES_GROUP_HIDE, this, getSensor("Body"), "Body");
    } else if (MR::isStep(this, 0)) {
        setNerve(GET_NERVE(JumpGuarder, JumpGuarderNrvOpen));
    }
}

inline void JumpGuarder::addBaby(JumpGuarderBaby* pBaby) {
    s32 idx = mNumActiveBabies++;
    mActiveBabies[idx] = pBaby;
}

void JumpGuarder::exeOpen() {
    if (MR::isLessStep(this, ::sOpen)) {
        updateRotate();
    }

    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Open");
        MR::startSound(this, "SE_EM_JGUARDER_SHUTTER_OPEN");

        mNumActiveBabies = 0;
        for (int i = 0; i < mNumBabies; i++) {
            if (MR::isDead(&mBabies[i])) {
                addBaby(&mBabies[i]);
            }
        }

        Vec3f yDir;
        MR::extractMtxYDir(mHeadModel->getBaseMtx(), &yDir);
        Vec3f zDir;
        MR::extractMtxZDir(mHeadModel->getBaseMtx(), &zDir);

        for (u32 i = 0; i < mNumActiveBabies; i++) {
            JumpGuarderBaby* baby = mActiveBabies[i];
            MR::rotateVecDegree(&zDir, yDir, cNum_exeOpen_f1 / mNumActiveBabies);

            baby->mPosition.set(mPosition + zDir * ::sBabyOffset);
            baby->mVelocity.set(Vec3f(0.0f, 0.0f, 0.0f));

            if (mNumCoins > 0) {
                mNumCoins -= 1;
                baby->appearFromGuarder();
            } else {
                baby->appearFromGuarder();
            }
        }
    }

    if (MR::isLessStep(this, ::sOpen)) {
        Vec3f yDir;
        MR::extractMtxYDir(mHeadModel->getBaseMtx(), &yDir);
        Vec3f zDir;
        MR::extractMtxZDir(mHeadModel->getBaseMtx(), &zDir);

        for (u32 i = 0; i < mNumActiveBabies; i++) {
            JumpGuarderBaby* baby = mActiveBabies[i];
            MR::rotateVecDegree(&zDir, yDir, cNum_exeOpen_f2 / mNumActiveBabies);

            baby->mPosition.set(mPosition + zDir * ::sBabyOffset);
            baby->mVelocity.set(Vec3f(0.0f, 0.0f, 0.0f));
        }
    }

    if (MR::isStep(this, ::sOpen)) {
        Vec3f yDir;
        MR::extractMtxYDir(mHeadModel->getBaseMtx(), &yDir);
        Vec3f zDir;
        MR::extractMtxZDir(mHeadModel->getBaseMtx(), &zDir);

        for (u32 i = 0; i < mNumActiveBabies; i++) {
            JumpGuarderBaby* baby = mActiveBabies[i];
            MR::rotateVecDegree(&zDir, yDir, cNum_exeOpen_f3 / mNumActiveBabies);

            baby->mVelocity.set(zDir * mBabyVelocity);
        }

        MR::startSound(this, "SE_EM_JGUARDER_LAUNCH_BABY");
    }

    if (MR::isStep(this, ::sOpenInt)) {
        mNumActiveBabies = 0;
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(JumpGuarder, JumpGuarderNrvClose));
    }
}

void JumpGuarder::exeClose() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Close");
    }

    MR::startLevelSound(this, "SE_EM_LV_JGUARDER_SHUTTER_CLOSE");

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(JumpGuarder, JumpGuarderNrvInter));
    }
}

void JumpGuarder::exeInter() {
    updateRotate();

    if (!MR::enableGroupAttack(this, ::sDisappearDistance, ::sAppearHeight)) {
        MR::sendMsgToGroupMember(ACTMES_GROUP_HIDE, this, getSensor("Body"), "Body");
    } else {
        setNerve(GET_NERVE(JumpGuarder, JumpGuarderNrvWait));
    }
}

JumpGuarder::JumpGuarder(const char* pName)
    : JumpEmitter(pName), mBabies(), mNumBabies(::sBabyNum), mBumpCooldown(), mActiveBabies(), mNumActiveBabies(), mNumCoins(::sCoinDefault),
      mBabyVelocity(::sBabyVelocity) {
    lumacore::setClass(this, lumacore::kClassId< JumpGuarder >);
    mNumActiveBabies = 0;
}

void JumpGuarder::init(const TableIter& rIter) {
    initModelManagerWithAnm("JumpGuarder", nullptr, false);
    // "Jump guarder head"
    mHeadModel = MR::createPartsModelNoSilhouettedMapObj(this, "ジャンプガーダー頭", "JumpGuarderHead", mHeadMtx);
    MR::initLightCtrl(mHeadModel);
    MR::initDefaultPos(this, rIter);
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    initHitSensor(2);

    MR::addHitSensorMtx(this, "Jump", ATYPE_PLAYER_AUTO_JUMP, 8, cNum_init_f1, MR::getJointMtx(mHeadModel, "SpringJoint3"),
                        Vec3f(0.0f, ::sHeadOffset, 0.0f));

    MR::addHitSensorMtxEnemy(this, "Body", 8, cNum_init_f2, MR::getJointMtx(this, "Body"), Vec3f(0.0f, cNum_init_f3, 0.0f));
    getSensor("Body")->setType(29);
    getSensor("Body")->validate();
    getSensor("Jump")->invalidate();
    MR::initShadowVolumeSphere(this, ::sShadowRadius);
    initEffectKeeper(1, nullptr);
    initSound(8, false);
    MR::invalidateClipping(this);
    initNerve(GET_NERVE(JumpGuarder, JumpGuarderNrvHide));
    MR::startBckWithInterpole(this, "Down", 0);
    MR::setBckFrame(this, MR::getBckCtrl(this)->getEnd() - 1);
    MR::calcAnimDirect(this);
    mBodyJointMtx = MR::getJointMtx(this, "Body");
    MR::useStageSwitchReadA(this, rIter);
    MR::useStageSwitchReadB(this, rIter);
    initEventCamera(rIter);
    MR::joinToGroupArray(this, rIter, nullptr, 32);
    makeActorAppeared();
    s32 babySpeedModifier = 0;
    MR::getPlacementArg0NoInit(rIter, &mNumCoins);
    MR::getPlacementArg1NoInit(rIter, &babySpeedModifier);
    MR::getPlacementArg2NoInit(rIter, &mNumBabies);

    switch (babySpeedModifier) {
    case 0:
        mBabyVelocity = ::sBabyVelocity;
        break;
    case 1:
        mBabyVelocity = ::sBabyVelocity / cNum_init_f4;
        break;
    case 2:
        mBabyVelocity = ::sBabyVelocity / cNum_init_f5;
        break;
    }

    const s32 babyRoom = mNumBabies > ::sBabyNum ? mNumBabies : ::sBabyNum;
    mBabies = new JumpGuarderBaby[babyRoom];
    mActiveBabies = new JumpGuarderBaby*[babyRoom];
    MR::declareCoin(this, mNumCoins);

    for (int i = 0; i < babyRoom; i++) {
        JumpGuarderBaby* baby = &mBabies[i];
        baby->mHost = this;
        baby->mPosition.set(mPosition);
        baby->initWithoutIter();
        baby->makeActorDead();
    }
}

// "Baby Begoman"
JumpGuarderBaby::JumpGuarderBaby() : BegomanBaby("ベビーベーゴマン") {
    lumacore::setClass(this, lumacore::kClassId< JumpGuarderBaby >);
}

void JumpGuarder::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorEnemy(pReceiver)) {
        if (isHit(pReceiver->mHost)) {
            MR::sendMsgEnemyAttack(pReceiver, pSender);
            if (isNerve(GET_NERVE(JumpGuarder, JumpGuarderNrvWait)) && !MR::isOnGround(pReceiver->mHost)) {
                MR::startBck(this, "Hit");
                MR::startSound(this, "SE_EM_JGUARDER_HIT");
            }
        }
    } else if (MR::isSensorPlayer(pReceiver)) {
        MR::sendMsgPush(pReceiver, pSender);
        if ((isNerve(GET_NERVE(JumpGuarder, JumpGuarderNrvWait)) || isNerve(GET_NERVE(JumpGuarder, JumpGuarderNrvHopWait))) &&
            MR::isPlayerStaggering() && mBumpCooldown == 0) {
            mBumpCooldown = ::sHitInt;
            MR::startBck(this, "Hit");
            MR::startSound(this, "SE_EM_JGUARDER_HIT");
        }
    }
}

bool JumpGuarder::enableAttack() {
    for (int i = 0; i < mNumBabies; i++) {
        if (!MR::isDead(&mBabies[i])) {
            continue;
        }

        return getNerveStep() % ::sAttack == 0;
    }

    return false;
}

bool JumpGuarder::isHit(const LiveActor* pActor) const {
    for (u32 i = 0; i < mNumActiveBabies; i++) {
        if (mActiveBabies[i] == pActor) {
            return false;
        }
    }

    return true;
}

bool MR::enableGroupAttack(LiveActor* pActor, f32 radius, f32 height) {
    LiveActorGroup* group = MR::getGroupFromArray(pActor);

    if (isValidSwitchA(pActor) && !isOnSwitchA(pActor)) {
        return false;
    }

    if (isValidSwitchB(pActor) && !isOnSwitchB(pActor)) {
        return false;
    }

    LiveActor* closest = pActor;
    if (group != nullptr) {
        for (int i = 0; i < group->getObjNum(); i++) {
            LiveActor* actor = group->getActor(i);
            if (MR::calcDistanceToPlayer(actor) < MR::calcDistanceToPlayer(closest)) {
                closest = actor;
            }
        }
    }

    return isNearPlayerPose(closest, radius, height);
}
