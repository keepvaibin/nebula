#include "Game/Player/MarioModule.hpp"
#include "Game/Animation/XanimeCore.hpp"
#include "Game/Enemy/KarikariDirector.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioMapCode.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"

bool MarioModule::isInputDisable() const {
    if (mActor->mMario->mMovementStates.inputLocked) {
        return true;
    }
    if (mActor->mMario->isStatusActive(MarioStatus_FpView)) {
        return true;
    }
    if (isAnimationRun("ハード着地")) {
        return true;
    }
    if (isAnimationRun("中ダメージ着地")) {
        return true;
    }
    if (isAnimationRun("中後ダメージ着地")) {
        return true;
    }
    if (isAnimationRun("ステージインB")) {
        return true;
    }
    return mActor->mRemoteDemo;
}

Mario* MarioModule::getPlayer() const {
    return mActor->mMario;
}

MarioAnimator* MarioModule::getAnimator() const {
    return mActor->mMarioAnim;
}

Vec3f& MarioModule::getShadowPos() const {
    return mActor->mMario->mShadowPos;
}

const Vec3f& MarioModule::getGravityVec() const {
    return *mActor->mMario->getGravityVec();
}

const Vec3f& MarioModule::getAirGravityVec() const {
    return mActor->mMario->getAirGravityVec();
}

Vec3f& MarioModule::getFrontVec() const {
    return mActor->mMario->mFrontVec;
}

bool MarioModule::isStatusActiveID(u32 id) const {
    return mActor->mMario->isStatusActive(id);
}

bool MarioModule::isStatusActiveS(MarioState* pState) const {
    return mActor->mMario->isStatusActive(pState->mStatusId);
}

u16 MarioModule::getPlayerMode() const {
    return mActor->mPlayerMode;
}

bool MarioModule::isPlayerModeBee() const {
    return mActor->mPlayerMode == PlayerMode_Bee;
}

bool MarioModule::isPlayerModeIce() const {
    return mActor->mPlayerMode == PlayerMode_Ice;
}

bool MarioModule::isPlayerModeTeresa() const {
    return mActor->mPlayerMode == PlayerMode_Teresa;
}

bool MarioModule::isPlayerModeHopper() const {
    return mActor->mPlayerMode == PlayerMode_Hopper;
}

bool MarioModule::isPlayerModeInvincible() const {
    return mActor->mPlayerMode == PlayerMode_Invincible;
}

bool MarioModule::isPlayerModeFoo() const {
    return mActor->mPlayerMode == PlayerMode_Foo;
}

void MarioModule::changeAnimation(const char* pAnim1, const char* pAnim2) {
    if (!mActor->mAnimationFrozen) {
        if (pAnim2) {
            mActor->mMarioAnim->changeDefault(pAnim2);
        }
        if (pAnim1) {
            mActor->mMarioAnim->change(pAnim1);
        }
    }
}

void MarioModule::changeAnimationNonStop(const char* pAnim) {
    if (!mActor->mAnimationFrozen) {
        if (pAnim) {
            changeAnimation(pAnim);
        }
        XanimeFrameCtrl* pFrameCtrl = mActor->mMarioAnim->mXanimePlayer->mFrameCtrl;
        if (pFrameCtrl->getAttribute() == 0) {
            pFrameCtrl->setAttribute(1);
        }
    }
}

void MarioModule::changeAnimationWithAttr(const char* pAnim, u32 attribute) {
    if (!mActor->mAnimationFrozen) {
        if (pAnim) {
            changeAnimation(pAnim);
        }
        mActor->mMarioAnim->mXanimePlayer->mFrameCtrl->setAttribute(attribute);
    }
}

void MarioModule::stopAnimation(const char* pAnim1, const char* pAnim2) {
    if (!mActor->mAnimationFrozen) {
        if (pAnim2) {
            mActor->mMarioAnim->changeDefault(pAnim2);
        }
        if (pAnim1) {
            mActor->mMarioAnim->mXanimePlayer->stopAnimation(pAnim1);
        } else {
            mActor->mMarioAnim->mXanimePlayer->stopAnimation();
        }
    }
}

bool MarioModule::isDefaultAnimationRun(const char* pAnim) const {
    return mActor->mMarioAnim->isDefaultAnimationRun(pAnim);
}

void MarioModule::changeAnimationInterpoleFrame(u32 frame) {
    return mActor->mMarioAnim->mXanimePlayer->changeInterpoleFrame(frame);
}

bool MarioModule::isAnimationRun(const char* pAnimName) const {
    if (!pAnimName) {
        XanimePlayer* pXanimePlayer = mActor->mMarioAnim->mXanimePlayer;
        return pXanimePlayer->mCurrentAnimation != pXanimePlayer->mDefaultAnimation;
    } else {
        bool run = mActor->mMarioAnim->mXanimePlayer->isRun(pAnimName);
        if (mActor->mMarioAnim->mUpperRunning) {
            run |= mActor->mMarioAnim->mXanimePlayerUpper->isRun(pAnimName);
        }
        return run;
    }
}

bool MarioModule::isAnimationTerminate(const char* pAnim) const {
    if (pAnim) {
        return mActor->mMarioAnim->mXanimePlayer->isTerminate(pAnim);
    }
    return mActor->mMarioAnim->mXanimePlayer->isTerminate();
}

bool MarioModule::isAnimationTerminateUpper(const char* pAnim) const {
    if (pAnim) {
        return mActor->mMarioAnim->mXanimePlayerUpper->isTerminate(pAnim);
    }
    return mActor->mMarioAnim->mXanimePlayerUpper->isTerminate();
}

const char* MarioModule::getAnimationStringPointer(const char* pAnim) const {
    return mActor->mMarioAnim->mXanimePlayer->getNameStringPointer(pAnim);
}

const char* MarioModule::getCurrentBckName() const {
    if (mActor->mMarioAnim->mXanimePlayer->isAnimationRunSimple()) {
        return mActor->mMarioAnim->mXanimePlayer->getCurrentBckName();
    } else {
        return mActor->mMarioAnim->mCurrBck;
    }
}

void MarioModule::changeAnimationUpper(const char* pAnim1, const char* pAnim2) {
    if (!mActor->mAnimationFrozen) {
        mActor->mMarioAnim->changeDefaultUpper(pAnim2);
        if (pAnim1) {
            mActor->mMarioAnim->changeUpper(pAnim1);
        }
    }
}

void MarioModule::changeAnimationUpperWeak(const char* pAnim1, const char* pAnim2) {
    if (!mActor->mMarioAnim->mUpperRunning && !mActor->mAnimationFrozen) {
        mActor->mMarioAnim->changeDefaultUpper(pAnim2);
        if (pAnim1) {
            mActor->mMarioAnim->changeUpper(pAnim1);
        }
    }
}

void MarioModule::stopAnimationUpper(const char* pAnim1, const char* pAnim2) {
    if (!mActor->mAnimationFrozen) {
        mActor->mMarioAnim->changeDefaultUpper(pAnim2);
        mActor->mMarioAnim->stopUpper(pAnim1);
    }
}

void MarioModule::stopAnimationUpperForce() {
    mActor->mMarioAnim->stopUpper(nullptr);
}

f32 MarioModule::getAnimationFrame() const {
    return mActor->mMarioAnim->getFrame();
}

void MarioModule::changeAnimation(const char* pAnim, MarioIndexedAnime anime) {
    if (!mActor->mAnimationFrozen) {
        changeAnimation(pAnim);
        getPlayer()->mIndexedAnimeOn[anime] = 1;
    }
}

bool MarioModule::isAnimationRun(const char* pAnim, MarioIndexedAnime anime) {
    if (getPlayer()->mIndexedAnimeOn[anime] == 0) {
        return false;
    }

    bool isAnimRun = isAnimationRun(pAnim);

    if (!isAnimRun) {
        getPlayer()->mIndexedAnimeOn[anime] = 0;
    }

    return isAnimRun;
}

void MarioModule::stopAnimation(const char* pAnim, MarioIndexedAnime anime) {
    if (!mActor->mAnimationFrozen && mActor->getMario()->mIndexedAnimeOn[anime]) {
        stopAnimation(pAnim);
        getPlayer()->mIndexedAnimeOn[anime] = 0;
    }
}

void MarioModule::setJointGlobalMtx(u32 jointIndex, MtxPtr mtx) {
    XjointTransform* pTransform = mActor->mMarioAnim->mXanimePlayer->mCore->mTransformList;
    if (pTransform != nullptr) {
        pTransform[jointIndex].mBaseMtx = mtx;
    }
}

void MarioModule::setYangleOffset(f32 offset) {
    mActor->mMario->mYAngleOffset = offset;
}

bool MarioModule::isStickOn() const {
    return !MR::isNearZero(mActor->mMario->mStickPos.z);
}

bool MarioModule::isStickFull() const {
    return mActor->mMario->mStickPos.z > 0.9f;
}

Vec3f& MarioModule::getWorldPadDir() const {
    return mActor->mMario->mWorldPadDir;
}

bool MarioModule::calcWorldPadDir(Vec3f* pDest, f32 stickX, f32 stickY, bool isDisableMove) {
    pDest->zero();
    if (MR::isNearZero(stickX) && MR::isNearZero(stickY)) {
        return false;
    }
    if (!mActor->mMario->mActionStates.heavySteeringStick) {
        if (MR::abs(stickY) > mActor->mConst->getTable()->mStickMarginYstart) {
            if (MR::abs(stickX) < mActor->mConst->getTable()->mStickMarginX) {
                stickX = 0.0f;
            } else if (stickX > 0.0f) {
                stickX = (stickX - mActor->mConst->getTable()->mStickMarginX) / (1.0f - mActor->mConst->getTable()->mStickMarginX);
            } else {
                stickX = (stickX + mActor->mConst->getTable()->mStickMarginX) / (1.0f - mActor->mConst->getTable()->mStickMarginX);
            }
        } else if (MR::abs(stickX) > mActor->mConst->getTable()->mStickMarginXstart) {
            if (MR::abs(stickY) < mActor->mConst->getTable()->mStickMarginY) {
                stickY = 0.0f;
            } else if (stickY > 0.0f) {
                stickY = (stickY - mActor->mConst->getTable()->mStickMarginY) / (1.0f - mActor->mConst->getTable()->mStickMarginY);
            } else {
                stickY = (stickY + mActor->mConst->getTable()->mStickMarginY) / (1.0f - mActor->mConst->getTable()->mStickMarginY);
            }
        }
    }
    mActor->mMario->calcMoveDir(stickX, stickY, pDest, isDisableMove);
    return true;
}

void MarioModule::addVelocity(const Vec3f& rAdd) {
    mActor->mMario->mVelocity += rAdd;
}

void MarioModule::addVelocity(const Vec3f& rAdd, f32 scale) {
    Vec3f& velocity = mActor->mMario->mVelocity;
    velocity.scaleAdd(scale, rAdd);
}

void MarioModule::addVelocityAfter(const Vec3f& rAdd) {
    mActor->mMario->mVelocityAfter += rAdd;
}

void MarioModule::clearVelocity() {
    mActor->mMario->mVelocity.zero();
}

Vec3f& MarioModule::getVelocity() const {
    return mActor->mMario->mVelocity;
}

void MarioModule::addTrans(const Vec3f& rShift) {
    mActor->mMario->addTrans(rShift);
}

f32 MarioModule::cutGravityElementFromJumpVec(bool useActorGravity) {
    if (useActorGravity) {
        return MR::vecKillElement(mActor->getMario()->mJumpVec, mActor->mGravityVec, &mActor->getMario()->mJumpVec);
    } else {
        Vec3f* pJumpVec = &mActor->mMario->mJumpVec;
        return MR::vecKillElement(*pJumpVec, mActor->getGravityVec(), pJumpVec);
    }
}

f32 MarioModule::cutVecElementFromJumpVec(const Vec3f& rCut) {
    return MR::vecKillElement(mActor->mMario->mJumpVec, rCut, &mActor->mMario->mJumpVec);
}

Vec3f& MarioModule::getJumpVec() const {
    return mActor->mMario->mJumpVec;
}

void MarioModule::setJumpVec(const Vec3f& rVec) {
    mActor->mMario->mJumpVec = rVec;
}

void MarioModule::playEffect(const char* pEffectName) {
    mActor->playEffect(pEffectName);
}

void MarioModule::playEffectTrans(const char* pEffectName, const Vec3f& rTrans) {
    mActor->playEffectTrans(pEffectName, rTrans);
}

void MarioModule::playEffectRT(const char* pEffectName, const Vec3f& rDir, const Vec3f& rTrans) {
    mActor->playEffectRT(pEffectName, rDir, rTrans);
}

void MarioModule::playEffectRTZ(const char* pEffectName, const Vec3f& rDir, const Vec3f& rTrans) {
    mActor->playEffectRTZ(pEffectName, rDir, rTrans);
}

void MarioModule::playEffectRTW(const char* pEffectName, const Vec3f& rDir, const Vec3f& rTrans) {
    mActor->playEffectRTW(pEffectName, rDir, rTrans);
}

void MarioModule::playEffectSRT(const char* pEffectName, f32 scale, const Vec3f& rDir, const Vec3f& rTrans) {
    mActor->playEffectSRT(pEffectName, scale, rDir, rTrans);
}

void MarioModule::stopEffect(const char* pEffectName) {
    mActor->stopEffect(pEffectName);
}

void MarioModule::stopEffectForce(const char* pEffectName) {
    mActor->stopEffectForce(pEffectName);
}

void MarioModule::playSound(const char* pSoundName, s32 timing) {
    mActor->mMario->playSoundJ(pSoundName, timing);
}

void MarioModule::stopSound(const char* pSoundName, u32 delay) {
    mActor->mMario->stopSoundJ(pSoundName, delay);
}

void MarioModule::startCamVib(u32 strength) {
    switch (strength) {
    case 1:
        MR::shakeCameraWeak();
        return;
    case 2:
        MR::shakeCameraWeak();
        return;
    case 0:
        MR::shakeCameraNormal();
        return;
    case 3:
        MR::shakeCameraNormalStrong();
        return;
    case 4:
        MR::shakeCameraStrong();
        return;
    default:
        return;
    }
}

Vec3f& MarioModule::getCamPos() const {
    return mActor->mCamPos;
}

Vec3f& MarioModule::getCamDirX() const {
    return mActor->mCamDirX;
}

Vec3f& MarioModule::getCamDirY() const {
    return mActor->mCamDirY;
}

Vec3f& MarioModule::getCamDirZ() const {
    return mActor->mCamDirZ;
}

void MarioModule::startPadVib(u32 strength) {
    switch (strength) {
    case 1:
        MR::tryRumblePadVeryWeak(mActor, PLAYER_1);
        return;
    case 0:
        MR::tryRumblePadWeak(mActor, PLAYER_1);
        return;
    case 2:
        MR::tryRumblePadMiddle(mActor, PLAYER_1);
        return;
    case 3:
        MR::tryRumblePadStrong(mActor, PLAYER_1);
        return;
    default:
        return;
    }
}

void MarioModule::startPadVib(const char* pStrength) {
    MR::tryRumblePad(mActor, pStrength, PLAYER_1);
}

f32 MarioModule::getStickX() const {
    return mActor->mMario->mStickPos.x;
}

f32 MarioModule::getStickY() const {
    return mActor->mMario->mStickPos.y;
}

f32 MarioModule::getStickP() const {
    return mActor->mMario->mStickPos.z;
}

bool MarioModule::checkTrgA() const {
    return mActor->checkButtonType(3, false);
}

bool MarioModule::checkLvlA() const {
    return mActor->checkButtonType(5, false);
}

bool MarioModule::checkTrgZ() const {
    return mActor->checkButtonType(7, false);
}

bool MarioModule::checkLvlZ() const {
    return mActor->checkButtonType(9, false);
}

bool MarioModule::checkPreLvlZ() const {
    return mActor->checkButtonType(9, true);
}

bool MarioModule::checkSquat(bool allowWhileRising) {
    if (MR::getKarikariClingNum() >= 1) {
        return false;
    }
    if (mActor->mTakeCount) {
        return false;
    }
    if (mActor->mBombTeresaTimer != 0) {
        return false;
    }
    if (mActor->mBeeWallWalk != 0) {
        return false;
    }
    if (mActor->mMario->mMovementStates.squatLocked) {
        if (!mActor->checkButtonType(9, false)) {
            mActor->mMario->mMovementStates.squatLocked = 0;
        }
        return false;
    }
    if (!allowWhileRising && mActor->mMario->mVerticalSpeed > 100.0f) {
        return false;
    }
    return mActor->checkButtonType(9, false);
}

Vec3f& MarioModule::getTrans() const {
    return mActor->mMario->mPosition;
}

const Triangle* MarioModule::getGroundPolygon() const {
    return mActor->mMario->mGroundPolygon;
}

bool MarioModule::isOnSlipGround() const {
    Mario* pMario = mActor->mMario;
    if (pMario->mMovementStates.onGround && pMario->isSlipPolygon(pMario->mGroundPolygon)) {
        return true;
    }
    return false;
}

bool MarioModule::isSlipFloorCode(s32 floorCode) const {
    switch (floorCode) {
    case 2:
        return true;
    case 3:
    case 0xd:
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x1e:
        return false;
    }
    return true;
}

bool MarioModule::isSlipPolygon(const Triangle* pTri) const {
    if (!pTri->isValid()) {
        return false;
    }

    u32 code = mActor->mMario->mFloorCodeReader->getCode(pTri);

    switch (code) {
    case 2:
        return !(calcAngleD(*pTri->getNormal(0)) <= 3.0f);
    case 3:
    case 0xd:
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x1e:
        return false;
    }

    f32 angle = calcAngleD(*pTri->getNormal(0));

    return angle >= mActor->mConst->getTable()->mFlatAngle - 0.5f;
}

u32 MarioModule::getFloorCode() const {
    return mActor->mMario->mFloorCodeReader->getCode(mActor->mMario->mGroundPolygon);
}

f32 MarioModule::calcPolygonAngleD(const Triangle* pTri) const {
    if (pTri->isValid() == false) {
        return 0.0f;
    }
    if (MR::isNearZero(*pTri->getNormal(0))) {
        return 0.0f;
    }
    return calcAngleD(*pTri->getNormal(0));
}

f32 MarioModule::calcAngleD(const Vec3f& rVec) const {
    f32 angleDeg = (MR::acosEx(-mActor->mMario->getAirGravityVec().dot(rVec)) / 3.14159f) * 180.0f;
    if (MR::isNearZero(angleDeg, 1.0f)) {
        angleDeg = 0.0f;
    }
    if (MR::isNearZero(angleDeg - 90.0f, 1.0f)) {
        angleDeg = 90.0f;
    }
    return angleDeg;
}

Triangle* MarioModule::getTmpPolygon() const {
    return mActor->mMario->mTmpPolygon;
}

bool MarioModule::sendPunch(HitSensor* pSensor, bool isSpinAttack) {
    if (!pSensor) {
        return false;
    }
    bool ret = mActor->sendPunch(pSensor, isSpinAttack);
    if (ret) {
        mActor->reactionPunch(pSensor);
    }
    return ret;
}

HitSensor* MarioModule::getSensor(const Triangle* pTri) const {
    if (pTri->isValid() == false) {
        return nullptr;
    } else {
        return pTri->mSensor;
    }
}

f32 MarioModule::marioAcos(f32 cosine) const {
    return MR::acosEx(cosine);
}
