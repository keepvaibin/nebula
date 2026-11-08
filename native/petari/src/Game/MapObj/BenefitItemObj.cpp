#include "Game/MapObj/BenefitItemObj.hpp"
#include "Game/LiveActor/FlashingCtrl.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace {
    f32 cAppearThrowUpSpd = 10.0f;
    f32 cAppearThrowUpSpd2 = 15.0f;
    f32 cGravity = 0.5f;
    f32 cReflectWallX = 0.3f;
    static f32 cSpdRotateY = FloatUtil::PI() / 60.0f;
    static u16 sVal = 0x0258;
};  // namespace

void ShadowClipActor::endClipped() {
    LiveActor::endClipped();

    if (MR::isClipped(mHost)) {
        mHost->endClipped();
    }
}

BenefitItemObj::BenefitItemObj(const char* pName, const char* pPowerUp) : LiveActor(pName) {
    mIsDisabled = 0;
    mIsItemMoving = 0;
    mIsInAir = 0;
    mIsDemoPending = 0;
    mHasAppearCamera = 0;
    mCameraFrames = 0;
    mRotateY = 0;
    mFollowMtx = nullptr;
    mIsThrownHigh = 0;
    mHasAppearPicture = 0;
    mCameraInfo = nullptr;
    mHitSensorActor = nullptr;
    mEscapeVelocity.zero();
    mFallVelocity.zero();
    mModelName = pPowerUp;
    NebulaMtxIdentity(mModelMtx);
}

void BenefitItemObj::initModelAndEfx() {
    initModelManagerWithAnm(mModelName, nullptr, false);
    initEffectKeeper(5, mModelName);

    initBinder(50.0f, 50.0f, 0);
    initHitSensor(1);
    MR::addHitSensor(this, "body", ATYPE_KINOKO_ONEUP, 4, 50.0f, Vec3f(0.0f, 50.0f, 0.0f));
}

void BenefitItemObj::init(const TableIter& rIter) {
    LiveActor::init(rIter);

    if (MR::isValidInfo(rIter)) {
        MR::initDefaultPos(this, rIter);
    }

    initModelAndEfx();

    MR::connectToSceneItemStrongLight(this);

    MR::offCalcGravity(this);

    MR::calcUpVec(&mGravity, this);
    mGravity = -mGravity;

    initSound(4, false);

    mFlashCtrl = new FlashingCtrl(this, true);

    MR::offBind(this);

    mIsOnRail = 0;

    if (MR::isValidInfo(rIter)) {
        mIsOnRail = MR::isConnectedWithRail(rIter);

        if (mIsOnRail) {
            initRailRider(rIter);
        }

        mRailSpeed = 5.0f;

        s32 railArg;

        if (mIsOnRail && MR::getRailArg0NoInit(this, &railArg)) {
            mRailSpeed = railArg / 10.0f;
        }
    }

    MR::setClippingFar100m(this);
    MR::setClippingTypeSphere(this, 100.0f);

    initNerve(GET_NERVE(BenefitItemObj, HostTypeNrvWait));

    mIsActive = 1;
    mAppearType = 0;
    mEscapeType = 0;
    mIsWaitingAppear = 0;

    s32 arg0 = -1;
    s32 arg1 = -1;
    s32 arg2 = -1;
    s32 arg3 = -1;
    s32 arg4 = -1;
    s32 arg5 = -1;
    s32 arg6 = -1;
    s32 arg7 = -1;

    if (MR::isValidInfo(rIter)) {
        MR::getPlacementArg0NoInit(rIter, &arg0);
        MR::getPlacementArg1NoInit(rIter, &arg1);
        MR::getPlacementArg2NoInit(rIter, &arg2);
        MR::getPlacementArg3NoInit(rIter, &arg3);
        MR::getPlacementArg4NoInit(rIter, &arg4);
        MR::getPlacementArg5NoInit(rIter, &arg5);
        MR::getPlacementArg6NoInit(rIter, &arg6);
        MR::getPlacementArg7NoInit(rIter, &arg7);
    } else {
        mIsWaitingAppear = 1;
        mIsActive = 0;
        mEscapeType = 0;
        mAppearType = 0;
    }

    switch (arg0) {
    case -1:
    case 0:
        mIsActive = 0;
        break;
    }

    switch (arg1 & 0xF) {
    case 1:
        mAppearType = 1;
        break;

    case 2:
        mAppearType = 2;
        break;

    case 3:
        mAppearType = 3;
        break;
    }

    if (arg1 != -1 && (arg1 & 0x80) != 0) {
        mHasAppearPicture = 1;
    }

    switch (arg2) {
    case 1:
        mEscapeType = 1;
        break;

    case 2:
        mEscapeType = 2;
        break;

    case 3:
        mEscapeType = 3;
        break;
    }

    bool setClipType = false;

    if (arg3 == 1) {
        setClipType = true;
    }

    mShadowType = 0;

    switch (arg4) {
    case -1:
        mShadowType = 1;
        break;

    case 1:
        mShadowType = 2;
        break;

    case 2:
        mShadowType = 3;
        break;

    case 3:
        mShadowType = 4;
        break;
    }

    if (arg5 == 1 && MR::isValidInfo(rIter)) {
        MR::initActorCamera(this, rIter, &mCameraInfo);
        mHasAppearCamera = 1;
    }

    if (arg6 == -1) {
        arg6 = 0;
    }

    mRotateFlags = arg6;
    f32 shadowDist = 2000.0f;

    if (arg7 != -1) {
        shadowDist = arg7;
    }

    if (!mIsDisabled && MR::isValidInfo(rIter)) {
        MR::useStageSwitchWriteDead(this, rIter);

        if (MR::useStageSwitchReadAppear(this, rIter)) {
            MR::listenStageSwitchOnAppear(this, MR::Functor(this, &BenefitItemObj::appear));
            mIsWaitingAppear = 1;
        }
    }

    if (mIsDisabled) {
        mIsActive = 0;

        makeActorDead();

        return;
    }

    if (mIsActive) {
        makeActorAppeared();

        if (mIsWaitingAppear) {
            MR::invalidateHitSensors(this);
        } else {
            runEfx("Glow");
        }
    } else if (mIsWaitingAppear) {
        makeActorDead();
    } else {
        makeActorAppeared();
        runEfx("Glow");
    }

    if (!MR::isDead(this) && MR::isValidInfo(rIter)) {
        MR::useStageSwitchSleep(this, rIter);
    }

    Vec3f rotY;
    MR::getRotatedAxisY(&rotY, mRotation);
    mUp.set(rotY);

    if (!mShadowType) {
        return;
    }

    MR::initShadowVolumeCylinder(this, 50.0f);
    MR::setShadowDropLength(this, nullptr, shadowDist);

    if (arg7 == -1) {
        MR::onShadowVolumeCutDropLength(this, nullptr);
        MR::setShadowVolumeEndDropOffset(this, nullptr, 50.0f);
    }

    if ((mShadowType & 0x1) != 0) {
        if ((mShadowType & 0x2) != 0) {
            MR::onCalcShadowDropPrivateGravity(this, nullptr);
            MR::onCalcShadow(this, nullptr);
        } else {
            MR::onCalcShadowDropPrivateGravityOneTime(this, nullptr);
            MR::onCalcShadowOneTime(this, nullptr);
        }
    } else if ((mShadowType & 0x2) != 0) {
        MR::onCalcShadow(this, nullptr);
    } else {
        MR::onCalcShadowOneTime(this, nullptr);
    }

    if (setClipType) {
        MR::setClippingTypeSphere(this, shadowDist);
    }
}

void BenefitItemObj::initAfterPlacement() {
    if (mIsOnRail) {
        MR::moveCoordAndTransToNearestRailPos(this);
    }
}

void BenefitItemObj::makeActorAppeared() {
    LiveActor::makeActorAppeared();
    runEfx("Glow");
}

void BenefitItemObj::appear() {
    LiveActor::appear();

    mIsWaitingAppear = 0;
    mIsActive = 1;

    switch (mAppearType) {
    case 0:

        if (mEscapeType) {
            setNerve(GET_NERVE(BenefitItemObj, HostTypeNrvPreEscape));
        }

        MR::validateHitSensors(this);
        break;

    case 1:
        appearGround();
        break;

    case 2:
        appearThrowUp();
        break;

    case 3:
        appearThrowUpQuestionBox();
        break;
    }

    LiveActor::calcAndSetBaseMtx();

    NebulaMtxCopy(getBaseMtx(), mModelMtx);

    if (mHasAppearCamera) {
        mIsDemoPending = 1;
        MR::invalidateClipping(this);
    }

    if (mEscapeType) {
        MR::invalidateClipping(this);
    }

    if (mHasAppearPicture) {
        MR::emitEffect(this, "AppearPicture");
    }
}

void BenefitItemObj::appearGround() {
    MR::startSystemSE("SE_SY_ITEM_APPEAR");

    setNerve(GET_NERVE(BenefitItemObj, HostTypeNrvAppearGround));

    MR::hideModel(this);

    MR::invalidateClipping(this);
}

void BenefitItemObj::appearThrowUp() {
    MR::startSystemSE("SE_SY_ITEM_APPEAR");

    runBck("Appear");

    Vec3f rotY;
    MR::getRotatedAxisY(&rotY, mRotation);
    mUp.set(rotY);

    setNerve(GET_NERVE(BenefitItemObj, HostTypeNrvShoot));

    if (!mIsThrownHigh) {
        mVelocity = rotY * ::cAppearThrowUpSpd;
    } else {
        mVelocity = rotY * ::cAppearThrowUpSpd2;
    }

    MR::invalidateClipping(this);
}

void BenefitItemObj::shoot(const Vec3f& rPosition, const Vec3f&, bool) {
    mPosition = rPosition;

    if (!mIsThrownHigh) {
        mAppearType = 2;
    } else {
        mAppearType = 3;
    }

    appear();
}

void BenefitItemObj::appearThrowUpQuestionBox() {
    appearThrowUp();
}

void BenefitItemObj::control() {
    if (mIsDemoPending && MR::tryStartDemoWithoutCinemaFrame(this, "出現")) {
        mIsDemoPending = 0;

        MR::startActorCameraTargetSelf(this, mCameraInfo);

        MR::requestMovementOn(this);

        MR::pauseOffCameraDirector();
        mCameraFrames = MR::getActorCameraFrames(this, mCameraInfo);

        if (mCameraFrames == 0) {
            mCameraFrames = 120;
        }
    }

    Vec3f v9(mGravity);
    MR::calcGravityOrZero(this);
    calcAndSetBaseMtxInMovement();

    if (mHasAppearCamera && mCameraFrames != 0) {
        mCameraFrames--;

        if (mCameraFrames == 0) {
            mHasAppearCamera = 0;

            MR::endActorCamera(this, mCameraInfo, false);
            MR::endDemo(this, "出現");

            MR::validateClipping(this);
        }
    }

    if (mIsItemMoving) {
        return;
    }

    if (MR::isBindedGroundDamageFire(this)) {
        runEfx("Vanish");
        kill();
    }

    if (!MR::isNearZero(mVelocity) && MR::isInDeath(this, Vec3f(0.0f, 0.0f, 0.0f))) {
        runEfx("Vanish");
        kill();
    }
}

void BenefitItemObj::exeWait() {
    if (!mIsActive) {
        return;
    }

    if (mIsWaitingAppear) {
        return;
    }

    if (mRotateFlags) {
        doRotateY();
        return;
    }

    if (mIsWaitingAppear) {
        return;
    }

    if (mRotateFlags & 0x2) {
        doRotateY();
        return;
    }

    if (mRotateFlags & 0x1) {
        doRotateY();
    }
}

void BenefitItemObj::doRotateY() {
    if (isNerve(GET_NERVE(BenefitItemObj, HostTypeNrvAppearGround))) {
        mRotateY = 0.0f;
    } else {
        mRotateY = MR::repeat(mRotateY + ::cSpdRotateY, 0.0f, TWO_PI);
    }
}

void BenefitItemObj::exeShoot() {
    if (MR::isFirstStep(this)) {
        MR::onBind(this);
    }

    Vec3f newVel(mVelocity);
    f32 verticalSpeed = MR::vecKillElement(newVel, mGravity, &newVel);

    if (MR::isBindedGround(this)) {
        runBck("Land");

        MR::validateHitSensors(this);

        mVelocity.zero();

        if (mEscapeType == 0) {
            if (MR::isGreaterStep(this, 10)) {
                setNerve(GET_NERVE(BenefitItemObj, HostTypeNrvWait));
            }
        } else {
            setNerve(GET_NERVE(BenefitItemObj, HostTypeNrvEscape));
        }

        return;
    }

    f32 val = verticalSpeed + ::cGravity;

    if (getNerveStep() == 60) {
        MR::validateHitSensors(this);
    }

    if (MR::isBindedWall(this)) {
        f32 speed = MR::vecKillElement(newVel, *MR::getWallNormal(this), &newVel);
        newVel -= *MR::getWallNormal(this) * speed * ::cReflectWallX;
    }

    newVel += mGravity * val;
    mVelocity.set(newVel);
}

void BenefitItemObj::exeCatch() {
    if (MR::isFirstStep(this)) {
        runEfx("Get");

        MR::requestOneUp();

        MR::hideModel(this);

        MR::incPlayerLeft();
        MR::explainOneUpIfAtFirst();

        kill();
    }

    mVelocity.set(0.0f);
    mPosition.set(mHitSensorActor->mPosition);
}

bool BenefitItemObj::receiveMsgPlayerAttack(u32 msg, HitSensor*, HitSensor*) {
    return MR::isMsgStarPieceReflect(msg);
}

bool BenefitItemObj::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isDead(this)) {
        return false;
    }

    if (MR::receiveItemShowMsg(msg, pSender, pReceiver)) {
        return true;
    }

    if (MR::receiveItemHideMsg(msg, pSender, pReceiver)) {
        return true;
    }

    if (MR::isMsgItemStartMove(msg)) {
        mIsItemMoving = 1;
        return true;
    }

    if (MR::isMsgItemEndMove(msg)) {
        mIsItemMoving = 0;
        return true;
    }

    if (msg == ACTMES_ITEM_GET) {
        if (mHitSensorActor != nullptr) {
            return false;
        }

        if (isNerve(GET_NERVE(BenefitItemObj, HostTypeNrvWait)) || isNerve(GET_NERVE(BenefitItemObj, HostTypeNrvShoot)) ||
            isNerve(GET_NERVE(BenefitItemObj, HostTypeNrvAppearGround)) || isNerve(GET_NERVE(BenefitItemObj, HostTypeNrvEscape))) {
            mHitSensorActor = pSender->mHost;

            setNerve(GET_NERVE(BenefitItemObj, HostTypeNrvCatch));

            MR::tryRumblePadWeak(this, PLAYER_1);

            return true;
        }
    }

    return false;
}

void BenefitItemObj::exeAppearGround() {
    if (MR::isFirstStep(this)) {
        MR::onBind(this);
    }

    if (getNerveStep() < 60) {
        return;
    }

    if (getNerveStep() == 60) {
        MR::showModel(this);

        runBck("Appear");

        MR::startSound(this, "SE_OJ_KINOKO_1UP_GROW");
    }

    if (!MR::isBckStopped(this)) {
        return;
    }

    if (!mEscapeType) {
        setNerve(GET_NERVE(BenefitItemObj, HostTypeNrvWait));
    } else {
        setNerve(GET_NERVE(BenefitItemObj, HostTypeNrvPreEscape));
    }

    MR::validateHitSensors(this);
}

void BenefitItemObj::exePreEscape() {
    if (getNerveStep() >= 30) {
        setNerve(GET_NERVE(BenefitItemObj, HostTypeNrvEscape));
    }
}

void BenefitItemObj::initEscape() {
    mEscapeLife = ::sVal;
    mFlashPeriod = 0;
    mFlashHideTime = 0;
    mFlashCounter = 0;
    mHideCounter = 0;

    Vec3f rotZ;
    MR::getRotatedAxisZ(&rotZ, mRotation);
    mEscapeVelocity = rotZ * 7.0f;

    MR::startSound(this, "SE_OJ_KINOKO_1UP_RUN_START");
}

void BenefitItemObj::doEscape() {
    MR::startLevelSound(this, "SE_OJ_LV_KINOKO_1UP_RUN");

    bool val = false;

    if (mEscapeType == 2) {
        val = true;
    }

    if (MR::isBindedGround(this)) {
        f32 length = mEscapeVelocity.length();
        MR::vecKillElement(mEscapeVelocity, mGravity, &mEscapeVelocity);
        mEscapeVelocity.setLength(length);

        if (val) {
            Vec3f vec38(mPosition - *MR::getPlayerPos());
            MR::vecKillElement(vec38, mGravity, &vec38);
            MR::normalizeOrZero(&vec38);

            if (mEscapeVelocity.dot(vec38) < 0.0f) {
                mEscapeVelocity += vec38 * 0.02f;
                mEscapeVelocity.x *= 1.1f;
                mEscapeVelocity.y *= 1.1f;
                mEscapeVelocity.z *= 1.1f;
            } else {
                mEscapeVelocity += vec38 * 0.01f;
                mEscapeVelocity.x *= 1.1f;
                mEscapeVelocity.y *= 1.1f;
                mEscapeVelocity.z *= 1.1f;
            }
        }

        if (mEscapeVelocity.length() > 7.0f) {
            mEscapeVelocity.setLength(7.0f);
        }

        if (mIsInAir) {
            mIsInAir = 0;
            runBck("Land");
        }

        mFallVelocity = mGravity * 5.0f;

        runEfx("Move");
    } else {
        mEscapeVelocity.x *= 0.95f;
        mEscapeVelocity.y *= 0.95f;
        mEscapeVelocity.z *= 0.95f;
        mIsInAir = 1;

        mFallVelocity += mGravity * 1.0f;

        if (mFallVelocity.length() > 20.0f) {
            mFallVelocity.setLength(20.0f);
        }

        stopEfx("Move");
    }

    if (!MR::isBindedWall(this)) {
        return;
    }

    if (mIsInAir) {
        return;
    }

    if (!(MR::getWallNormal(this)->dot(mVelocity) < 0.0f)) {
        return;
    }

    mEscapeVelocity += -*MR::getWallNormal(this) * MR::vecKillElement(mEscapeVelocity, *MR::getWallNormal(this), &mEscapeVelocity);
    mFallVelocity += -mGravity * 20.0f;

    mIsInAir = 1;
}

void BenefitItemObj::exeEscape() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);

        MR::onBind(this);

        initEscape();
    }

    doEscape();

    if (mEscapeLife) {
        MR::showModel(this);

        mEscapeLife--;

        if (mHideCounter != 0) {
            mHideCounter--;
            MR::hideModel(this);
        }

        if (mFlashCounter != 0) {
            mFlashCounter--;

            if (mFlashCounter == 0) {
                mFlashCounter = mFlashPeriod;
                mHideCounter = mFlashHideTime;
            }
        }

        if (mEscapeLife == 0x3C) {
            mFlashPeriod = 4;
            mFlashHideTime = 2;
        } else if (mEscapeLife == 0x78) {
            mFlashPeriod = 8;
            mFlashHideTime = 3;
        } else if (mEscapeLife == 0xB4) {
            mFlashPeriod = 12;
            mFlashHideTime = 4;
            mFlashCounter = 12;
            mHideCounter = 4;
        }

        if (mEscapeLife == 0) {
            runEfx("Vanish");
            makeActorDead();
        }
    }

    if (mIsOnRail) {
        MR::moveCoord(this, mRailSpeed);
        mEscapeVelocity = MR::getRailPos(this) - mPosition;
    }

    mVelocity = mEscapeVelocity;
    mVelocity += mFallVelocity;

    if ((mRotateFlags & 0x4) != 0) {
        doRotateY();
    }

    if (!mShadowType) {
        return;
    }

    if ((mShadowType & 0x2) != 0) {
        return;
    }

    if ((mShadowType & 0x1) != 0) {
        MR::onCalcShadowOneTime(this, nullptr);
    } else {
        MR::onCalcShadowDropPrivateGravityOneTime(this, nullptr);
    }
}

void BenefitItemObj::calcAndSetBaseMtx() {
}

void BenefitItemObj::calcAndSetBaseMtxInMovement() {
    if (isNerve(GET_NERVE(BenefitItemObj, HostTypeNrvWait))) {
        LiveActor::calcAndSetBaseMtx();

        NebulaMtxCopy(getBaseMtx(), mModelMtx);

        if (mFollowMtx != nullptr) {
            NebulaMtxCopy(mFollowMtx, mModelMtx);
            MR::extractMtxTrans(mFollowMtx, &mPosition);
        }

        MR::multMtx(getBaseMtx(), MR::tmpMtxRotYRad(mRotateY), mModelMtx);

        return;
    }

    if (MR::isBindedGround(this)) {
        mUp = *MR::getGroundNormal(this);
    } else if (MR::isBindedWall(this)) {
        mUp = *MR::getWallNormal(this);
    }

    Vec3f stack_8(mVelocity);
    MR::normalizeOrZero(&stack_8);

    if (isNerve(GET_NERVE(BenefitItemObj, HostTypeNrvPreEscape))) {
        MR::vecBlendSphere(mBlendFront, stack_8, &stack_8, 0.1f);
        mBlendFront = stack_8;
    }

    if (MR::isNearZero(stack_8)) {
        return;
    }

    if (MR::isSameDirection(mUp, stack_8, 0.01f)) {
        MR::setMtxTrans(getBaseMtx(), mPosition);
        return;
    }

    Pos3f pos;
    MR::makeMtxUpFrontPos(&pos, mUp, stack_8, mPosition);
    MR::blendMtx(mModelMtx, pos, 0.2f, mModelMtx);
    MR::multMtx(getBaseMtx(), MR::tmpMtxRotYRad(mRotateY), mModelMtx);
    MR::setMtxTrans(getBaseMtx(), mPosition);
}

void BenefitItemObj::setFollowMtx(MtxPtr pMtx) {
    mFollowMtx = pMtx;
    MR::extractMtxTrans(pMtx, &mPosition);
}

void BenefitItemObj::runBck(const char* pBckName) {
    MR::startBck(this, pBckName);
}

void BenefitItemObj::runEfx(const char* pEfxName) {
    MR::emitEffect(this, pEfxName);
}

void BenefitItemObj::stopEfx(const char* pEfxName) {
    MR::deleteEffect(this, pEfxName);
}

void MR::setMtxTrans(MtxPtr pMtx, const Vec3f& rVec) {
    setMtxTrans(pMtx, rVec.x, rVec.y, rVec.z);
}
