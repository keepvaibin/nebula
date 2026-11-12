#include "Game/System/GameRules.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Util/MotionSignal.hpp"
#include "Game/LiveActor/Binder.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/MapObj/CollectCounter.hpp"
#include "Game/MapObj/StarPiece.hpp"
#include "Game/MapObj/StarPieceDirector.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioFpView.hpp"
#include "Game/Player/MarioHang.hpp"
#include "Game/Player/MarioHolder.hpp"
#include "Game/Player/MarioParts.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Player/MarioSwim.hpp"
#include "Game/Player/RushEndInfo.hpp"
#include "Game/Util.hpp"

namespace MarioAccess {
    void getTakePos(Vec3f* pOut) {
        if (getPlayerActor()->mTakingFixedPos != nullptr) {
            getPlayerActor()->mTakingFixedPos->calc();
            getPlayerActor()->mTakingFixedPos->copyTrans(pOut);
        }
    }

    bool isOnActor(const LiveActor* pActor) {
        if (getPlayerActor()->getMario()->mHitStates.groundSameSensor) {
            Triangle* marioGroundPolygon = getPlayerActor()->getMario()->mGroundPolygon;
            if (marioGroundPolygon->isValid()) {
                return marioGroundPolygon->mSensor->mHost == pActor;
            }

            return false;
        }

        if (getPlayerActor()->getMario()->mHitStates.groundEvenSlope || (getPlayerActor()->IsMarioSwimming() && getPlayerActor()->getMovementStates().shadowFound)) {
            Triangle* marioTri = getPlayerActor()->getMario()->mShadowPolygon;
            if (!marioTri->isValid()) {
                return false;
            }

            return marioTri->mSensor->mHost == pActor;
        }

        return false;
    }

    bool isOnGround(u32 playerNo) {
        if (playerNo != 0) {
            return false;
        }

        if (getPlayerActor()->mInRush) {
            return MR::isOnGround(getPlayerActor()->mRushSensor->mHost);
        }

        return getPlayerActor()->getMovementStates().onGround;
    }

    bool isHipDropFalling() {
        if (getPlayerActor()->getMovementStates().jumping && getPlayerActor()->getMovementStates().hipDrop && !getPlayerActor()->getMovementStates().onGround &&
            !getPlayerActor()->isJumpRising()) {
            return true;
        }

        return false;
    }

    bool isHipDropLand() {
        return getPlayerActor()->getDrawStates().hipDropLand;
    }

    bool isSwingAction() {
        if (getPlayerActor()->mInRush) {
            return false;
        }

        if (getPlayerActor()->getMovementStates().tornado) {
            return true;
        }

        return getPlayerActor()->isPunching();
    }

    bool isInRush() {
        return getPlayerActor()->mInRush || getPlayerActor()->getMario()->isStatusActive(MarioStatus_RecoveryOrWarp);
    }

    bool isSquat() {
        return getPlayerActor()->getMovementStates().squat && getPlayerActor()->getMovementStates().onGround;
    }

    bool isParalyzing() {
        return getPlayerActor()->getMario()->isStatusActive(MarioStatus_Paralyze);
    }

    bool isTeresaDisappear() {
        if (getPlayerActor()->mPlayerMode != PlayerMode_Teresa) {
            return false;
        }

        return getPlayerActor()->getMario()->mTeresaWallThroughTimer;
    }

    bool isFlying() {
        if (getPlayerActor()->getMario()->isStatusActive(MarioStatus_Foo)) {
            return true;
        }

        return getPlayerActor()->getMario()->mActionStates.fooFlying;
    }

    bool isNeedBrakingCamera() {
        return getPlayerActor()->getMario()->mHitStates.jet;
    }

    CubeCameraArea* getCameraCubeCode() {
        return getPlayerActor()->getMario()->getCameraCubeCode();
    }

    bool isSwimming() {
        return getPlayerActor()->getMario()->isSwimming();
    }

    bool isSkating() {
        return getPlayerActor()->getMario()->isStatusActive(MarioStatus_Skate);
    }

    Triangle* getGroundingPolygon(u32) {
        if (isSwimming()) {
            if (getPlayerActor()->getMovementStates().shadowFound) {
                return getPlayerActor()->getMario()->mShadowPolygon;
            }

            return nullptr;
        }

        if (!isOnGround(0)) {
            return nullptr;
        }

        if (getPlayerActor()->mInRush) {
            return &getPlayerActor()->mRushSensor->mHost->mBinder->mGroundInfo.mParentTriangle;
        }

        return getPlayerActor()->getMario()->mGroundPolygon;
    }

    Triangle* getShadowingPolygon() {
        if (getPlayerActor()->getMovementStates().shadowFound) {
            return getPlayerActor()->getMario()->mShadowPolygon;
        }

        return nullptr;
    }

    f32 getShadowHeight() {
        return getPlayerActor()->getMario()->mVerticalSpeed;
    }

    void forceKill(u32 type, u32) {
        getPlayerActor()->forceKill(type);
    }

    bool isOnPress() {
        return getPlayerActor()->mPressTimer != 0;
    }

    bool isDisableFpView() {
        return getPlayerActor()->getMario()->isDisableFpViewMode();
    }

    bool isFpViewChangingFailure() {
        return getPlayerActor()->getMario()->mFpViewChangeFailed;
    }

    void stopFpView() {
        if (getPlayerActor()->getMario()->isStatusActive(MarioStatus_FpView)) {
            getPlayerActor()->getMario()->mFpView->forceClose();
            getPlayerActor()->getMario()->closeStatus(nullptr);
        }
    }

    void noticeDashChance() {
        getPlayerActor()->getMario()->mDashChanceTimer = 5;
    }

    void setWalkingResist(f32 walkingResist) {
        getPlayerActor()->getMario()->mWalkingResist = walkingResist;
    }

    void forceFly(const Vec3f& rTarget, const Vec3f& rEndFront, s32 frames) {
        getPlayerActor()->getMario()->doPointWarp(rTarget, rEndFront, frames);
    }

    void setJumpVec(const Vec3f& rJumpVec) {
        getPlayerActor()->getMario()->mJumpVec = rJumpVec;
    }

    void forceJump(const Vec3f& rVec, u32) {
        getPlayerActor()->getMario()->mMovementStates.squatLocked = true;
        getPlayerActor()->getMario()->tryForceJumpDelay(rVec);
    }

    void freeJump(const Vec3f& rVec, u32) {
        getPlayerActor()->getMario()->mMovementStates.squatLocked = true;
        getPlayerActor()->getMario()->tryFreeJumpDelay(rVec);
    }

    void tornadoJump() {
        getPlayerActor()->getMario()->mTornadoTimer = getPlayerActor()->getConst().getTable()->mTornadoTimeAir;
        getPlayerActor()->getMario()->tryTornadoJump();
        getPlayerActor()->mMario->startTornadoCentering(getPlayerActor()->mLastRushSensor);
    }

    void tornadoJumpMini() {
        getPlayerActor()->getMario()->mTornadoTimer = getPlayerActor()->getConst().getTable()->mTornadoTimeAir;
        getPlayerActor()->getMario()->tryTornadoJump();
        Vec3f up(-*getPlayerActor()->getMario()->getGravityVec() * getPlayerActor()->getConst().getTable()->mTornadoBoostPower);
        getPlayerActor()->getMario()->setRocketBooster(up, getPlayerActor()->getConst().getTable()->mTornadoBoostAttnMini,
                                                       getPlayerActor()->getConst().getTable()->mTornadoBoostTimerMini);
        getPlayerActor()->mMario->startTornadoCentering(getPlayerActor()->mLastRushSensor);
    }

    void becomeNormalJumpStatus() {
        getPlayerActor()->getMario()->mJumpKind = 0;
    }

    void setFrontVecKeepUp(const Vec3f& rVec, u16) {
        if (getPlayerActor()->getMario()->isStatusActive(MarioStatus_Hang)) {
            return;
        }

        getPlayerActor()->getMario()->setFrontVecKeepUp(rVec);
        getPlayerActor()->mCameraFrontVec = getPlayerActor()->getMario()->mFrontVec;
        getPlayerActor()->mUpVec = getPlayerActor()->getMario()->mHeadVec;
        getPlayerActor()->mCameraSideVec = getPlayerActor()->getMario()->mSideVec;
    }

    void setFrontVecTarget(const Vec3f& rVec, u16 frontMode) {
        MarioActor* actor = getPlayerActor();
        actor->mRushEndFront = rVec;
        actor->mRushEndFrontMode = frontMode;
    }

    void getThrowVec(Vec3f* pOut) {
        if (getPlayerActor()->getMario()->isStatusActive(MarioStatus_Foo)) {
            *pOut = getPlayerActor()->getMario()->mHeadVec;
            return;
        }

        if (getPlayerActor()->getMario()->isSwimming()) {
            getPlayerActor()->getThrowVec(pOut);
            return;
        }

        pOut->set(getPlayerActor()->mMoveFrontVec);
    }

    void setTrans(const Vec3f& rTrans, u16) {
        MotionSignal::playerPlaced(getPlayerActor(), rTrans);
        getPlayerActor()->mPosition.set(rTrans);
        getPlayerActor()->getMario()->mPosition = rTrans;

        if (getPlayerActor()->getMovementStates().mode2D) {
            getPlayerActor()->getMario()->m2DPlanePos = rTrans;
        }

        getPlayerActor()->mBaseMtxSet = true;

        getPlayerActor()->mCameraTrans = rTrans;
        MR::updateHitSensorsAll(getPlayerActor());
    }

    void endRush(const RushEndInfo* pRushEndInfo) {
        getPlayerActor()->endRush(pRushEndInfo);
    }

    void incLife(u32 amt) {
        if (getPlayerActor()->IsMarioSwimming()) {
            for (int i = 0; i < amt; i++) {
                getPlayerActor()->getMario()->mSwim->incLife();
            }
        } else {
            getPlayerActor()->incLife(amt);
        }

        if (getPlayerActor()->mPlayerMode == PlayerMode_Bee) {
            for (u32 i = 0; i < getPlayerActor()->getConst().getTable()->mAirWalkTime / 8; i++) {
                getPlayerActor()->getMario()->incAirWalkTimer();
            }
        }
    }

    bool isConfrontDeath() {
        if (!getPlayerActor()->mSuperKinokoCollected) {
            if (getPlayerActor()->mHealth == 0) {
                return true;
            }

            if (getPlayerActor()->mHealth == 1 && getPlayerActor()->getMario()->isDamaging() && !getPlayerActor()->IsMarioSwimming()) {
                return true;
            }
        }

        if (getPlayerActor()->getMario()->mSinkTimer != 0 && getPlayerActor()->getMario()->isCurrentFloorSink()) {
            return true;
        }

        return !getPlayerActor()->isEnableNerveChange();
    }

    void addStarPiece() {
        f32 fogLevel = getPlayerActor()->getConst().getTable()->mStarPieceFogLevel;
        u8 fogTime = getPlayerActor()->getConst().getTable()->mStarPieceFogTime;
        MarioActor* actor = getPlayerActor();
        actor->mFlashFogTimer = fogTime;
        actor->mFlashFogRate = fogLevel;
        actor->mFlashFogColor.set(0xFF, 0xFF, 0xFF, 0);
        actor->mFlashFogOn = false;
    }

    void getStarPieceDirect() {
    }

    MtxPtr getJointMtx(const char* pName) {
        return getPlayerActor()->getGlobalJointMtx(pName);
    }

    Vec3f* getVelocity() {
        if (getPlayerActor()->mInRush) {
            return getLastMove();
        }

        return &getPlayerActor()->getMario()->mVelocity;
    }

    Vec3f* getLastMove() {
        return const_cast< Vec3f* >(&getPlayerActor()->getLastMove());
    }

    void hide() {
        getPlayerActor()->mHidden = true;
        MR::forceDeleteEffectAll(getPlayerActor());
        getPlayerActor()->updateHand();
        getPlayerActor()->updateFace();
    }

    void show() {
        getPlayerActor()->mHidden = false;
    }

    HitSensor* getTakingSensor() {
        if (getPlayerActor()->mTakeSensors[0] != nullptr) {
            return getPlayerActor()->mTakeSensors[0];
        }

        if (getPlayerActor()->mTakenSensor != nullptr) {
            return getPlayerActor()->mTakenSensor;
        }

        return nullptr;
    }

    void dropTakingActor() {
        getPlayerActor()->rushDropThrowMemoSensor();
    }

    void killTakingActor() {
        getPlayerActor()->damageDropThrowMemoSensor();
    }

    f32 getAnimationFrameMax() {
        return getPlayerActor()->mMarioAnim->getXanimePlayer()->mFrameCtrl->mEnd;
    }

    void changeAnimationJ(const char* pAnimName) {
        getPlayerActor()->changeAnimationNonStop(pAnimName);
    }

    void changeAnimationE(const char* pAnimName, s32 interpoleFrame) {
        if (getPlayerActor()->mForceKilled) {
            return;
        }

        if (getPlayerActor()->getMario()->isPlayerModeTeresa()) {
            getPlayerActor()->changeTeresaAnimation(pAnimName, interpoleFrame);
            return;
        }

        if (getPlayerActor()->mTakeCount == 0) {
            getPlayerActor()->getMario()->stopAnimationUpperForce();
        }

        MR::startBck(getPlayerActor(), pAnimName);

        if (interpoleFrame >= 0) {
            getPlayerActor()->mMarioAnim->getXanimePlayer()->changeInterpoleFrame(interpoleFrame);
        }

        getPlayerActor()->setBlink(pAnimName);
        getPlayerActor()->mMarioAnim->closeCallback();
        getPlayerActor()->mMarioAnim->entryCallback(pAnimName);
    }

    void changeAnimationE(const char* pAnimName, const char* pChar2) {
        // unused
        getPlayerActor();
        if (getPlayerActor()->mForceKilled) {
            return;
        }

        if (getPlayerActor()->mTakeCount == 0) {
            getPlayerActor()->getMario()->stopAnimationUpperForce();
        }

        MR::startBck(getPlayerActor(), pAnimName, pChar2);

        getPlayerActor()->setBlink(pAnimName);
        getPlayerActor()->mMarioAnim->closeCallback();
        getPlayerActor()->mMarioAnim->entryCallback(pAnimName);
    }

    void changeAnimationE(const char* pAnimName, const BckCtrlData& rBckCtrlData) {
        // unused
        getPlayerActor();
        if (getPlayerActor()->mForceKilled) {
            return;
        }

        if (getPlayerActor()->mTakeCount == 0) {
            getPlayerActor()->getMario()->stopAnimationUpperForce();
        }

        MR::startBck(getPlayerActor(), pAnimName);

        MR::reflectBckCtrlData(getPlayerActor(), rBckCtrlData);

        getPlayerActor()->setBlink(pAnimName);
        getPlayerActor()->mMarioAnim->closeCallback();
        getPlayerActor()->mMarioAnim->entryCallback(pAnimName);
    }

    void keepCurrentAnimation() {
        if (getPlayerActor()->mMarioAnim->getXanimePlayer()->mFrameCtrl->getAttribute() == 0) {
            getPlayerActor()->mMarioAnim->getXanimePlayer()->mFrameCtrl->setAttribute(1);
        }
    }

    void progressAnimation() {
        getPlayerActor()->mMarioAnim->update();
        getPlayerActor()->calcAnimInMovement();

        XanimePlayer* animePlayer = getPlayerActor()->mMarioAnim->getXanimePlayer();
        animePlayer->updateBeforeMovement();
        animePlayer->updateAfterMovement();
    }

    const char* getCurrentBckName() {
        return getPlayerActor()->getMario()->getCurrentBckName();
    }

    void setAnimationBlendWeight(const f32* pWeights) {
        getPlayerActor()->mMarioAnim->forceSetBlendWeight(pWeights);
    }

    void offControl() {
        getPlayerActor()->mRemoteDemo = true;
    }

    bool isOffControl() {
        return getPlayerActor()->mRemoteDemo;
    }

    void onControl(bool resetCondition) {
        getPlayerActor()->mRemoteDemo = false;

        if (resetCondition) {
            getPlayerActor()->resetCondition();
        }
    }

    void setStateWait() {
        getPlayerActor()->getMario()->stopJump();
        getPlayerActor()->getMario()->stopWalk();
    }

    void startTalk(const LiveActor* pTalkActor) {
        getPlayerActor()->getMario()->startTalk(pTalkActor);
        getPlayerActor()->stopSpinTicoEffect(false);
    }

    void endTalk() {
        getPlayerActor()->getMario()->endTalk();
    }

    void readyRemoteDemo() {
        if (getPlayerActor()->mInDemo) {
            return;
        }

        if (getPlayerActor()->getMario()->isStatusActive(MarioStatus_RecoveryOrWarp) || getPlayerActor()->mPlayerMode == PlayerMode_Hopper) {
            offControl();
            getPlayerActor()->getMario()->mActionStates.remoteDemoSkipped = true;
            return;
        }

        getPlayerActor()->getMario()->mActionStates.remoteDemoSkipped = false;

        if (getPlayerActor()->mInRush) {
            getPlayerActor()->mRushSensor->receiveMessage(ACTMES_RUSH_CANCEL, getPlayerActor()->getSensor("body"));
            if (getPlayerActor()->mInRush) {
                RushEndInfo endInfo(nullptr, RushEndInfo::Mode_Release, Vec3f(0.0f, 0.0f, 0.0f), false, 0);
                endRush(&endInfo);
            }
        }

        if (getPlayerActor()->getMario()->isStatusActive(MarioStatus_FpView)) {
            getPlayerActor()->getMario()->closeStatus(nullptr);
        }

        getPlayerActor()->flushCoinPull();
        getPlayerActor()->calcAndSetBaseMtx();

        offControl();

        getPlayerActor()->mInDemo = true;
        MR::invalidateHitSensors(getPlayerActor());

        NebulaMtxCopy(getPlayerActor()->getBaseMtx(), getPlayerActor()->mForcedBaseMtx);

        getPlayerActor()->mAnimationFrozen = true;
        getPlayerActor()->getMario()->stopWalk();

        MR::deleteEffectAll(getPlayerActor());

        getPlayerActor()->mTrampleCounter->kill();
        getPlayerActor()->mRasterScrollAmp = 0.0f;
        getPlayerActor()->mDemoBaseKept = false;
    }

    void onFollowDemo() {
        if (getPlayerActor()->mPlayerMode == PlayerMode_Teresa) {
            MR::requestMovementOn(getPlayerActor()->mTeresaModel);
        }
    }

    void setBaseMtx(MtxPtr mtx) {
        MotionSignal::playerPlaced(getPlayerActor(), Vec3f(mtx[0][3], mtx[1][3], mtx[2][3]));
        getPlayerActor()->forceSetBaseMtx(mtx);
    }

    MtxPtr getBaseMtx() {
        if (getPlayerActor()->mBaseMtxForced) {
            return getPlayerActor()->mForcedBaseMtx;
        }

        return getPlayerActor()->getBaseMtx();
    }

    bool calcSpinPullVelocity(Vec3f* pOut, const Vec3f& rVec) {
        getPlayerActor()->tryPullTrans(pOut, rVec);
        return true;
    }

    void tryCoinPull() {
        if (!getPlayerActor()->getSensor("eye")->isValid()) {
            validateSensor();
        }
        getPlayerActor()->mClapCatchStart = true;
    }

    void addVelocity(const Vec3f& rVec) {
        if (getPlayerActor()->IsMarioSwimming() && getPlayerActor()->getMario()->mSwim->mJetTimer != 0) {
            return;
        }

        if (getPlayerActor()->mInDemo) {
            return;
        }

        if (getPlayerActor()->mInRush) {
            return;
        }

        getPlayerActor()->getMario()->push(rVec);

        if (getPlayerActor()->getMario()->isStatusActive(MarioStatus_Hang)) {
            getPlayerActor()->getMario()->mHang->forceDrop();
        }
    }

    void addVelocityFromArea(const Vec3f& rVec) {
        if (getPlayerActor()->IsMarioSwimming() && getPlayerActor()->getMario()->mSwim->mJetTimer != 0) {
            return;
        }

        if (getPlayerActor()->mInDemo) {
            return;
        }

        if (getPlayerActor()->mInRush) {
            return;
        }

        getPlayerActor()->getMario()->push(rVec);
    }

    bool isOnWaterSurface() {
        if (getPlayerActor()->getMario()->isStatusActive(MarioStatus_Swim)) {
            return getPlayerActor()->getMario()->mSwim->isOnWaterSurface();
        }

        return getPlayerActor()->isAnimationRun("水泳ジェット");
    }

    void calcWorldPadDir(Vec3f* pOut, f32 stickX, f32 stickY) {
        getPlayerActor()->getMario()->calcWorldPadDir(pOut, stickX, stickY, false);
    }

    void preventRush() {
        getPlayerActor()->setNerve(GET_NERVE(MarioActor, MarioActorNrvNoRush));
    }

    MarioActor* getPlayerActor() {
        return MR::getMarioHolder()->getMarioActor();
    }

    void validateSensor() {
        getPlayerActor()->getSensor("eye")->validate();
    }

    void incOxygen(u32 amt) {
        if (!getPlayerActor()->IsMarioSwimming()) {
            return;
        }

        for (int i = 0; i < amt; i++) {
            getPlayerActor()->getMario()->mSwim->incOxygen();
        }

        getPlayerActor()->playEffect("酸素回復");
    }

    void scatterStarPiece(u32 amt) {
        if (MR::getStarPieceNum() > 0) {
            MR::startSound(getPlayerActor(), "SE_OJ_STAR_PIECE_BURST");
        }

        for (int i = 0; i < amt; i++) {
            if (MR::getStarPieceNum() == 0) {
                return;
            }

            StarPiece* launchPiece = MR::getDeadStarPiece();

            launchPiece->launch(getPlayerActor()->mCenterPos, 24.0f, 24.0f, false, false);
            MR::addStarPiece(-1);
        }
    }

    void startDownWipe() {
        if (getPlayerActor()->isNerve(GET_NERVE(MarioActor, MarioActorNrvGameOverSink))) {
            return;
        }

        if (getPlayerActor()->isAnimationRun("氷結")) {
            return;
        }

        if (getPlayerActor()->mPressTimer != 0) {
            return;
        }

        getPlayerActor()->mPreWipeOn = true;
    }

    void readyDemo() {
        if (getPlayerActor()->mInRush) {
            if (!getPlayerActor()->mRushSensor->receiveMessage(ACTMES_RUSH_CANCEL, getPlayerActor()->getSensor("body"))) {
                getPlayerActor()->mRushSensor->receiveMessage(ACTMES_RUSH_FORCE_CANCEL, getPlayerActor()->getSensor("body"));
            }

            if (getPlayerActor()->mInRush) {
                RushEndInfo endInfo(nullptr, RushEndInfo::Mode_Release, Vec3f(0.0f, 0.0f, 0.0f), false, 0);
                endRush(&endInfo);
            }
        }

        if (getPlayerActor()->getMario()->isStatusActive(MarioStatus_FpView)) {
            getPlayerActor()->getMario()->closeStatus(nullptr);
        }
    }

    void endRemoteDemo(const RushEndInfo* pEndInfo) {
        if (getPlayerActor()->getMario()->isStatusActive(MarioStatus_RecoveryOrWarp) || getPlayerActor()->getMario()->isStatusActive(MarioStatus_Talk) ||
            getPlayerActor()->getMario()->mActionStates.remoteDemoSkipped || !getPlayerActor()->mInDemo) {
            getPlayerActor()->mRemoteDemo = false;
            return;
        }

        getPlayerActor()->mAnimationFrozen = false;
        onControl(true);
        getPlayerActor()->mInDemo = false;
        getPlayerActor()->stopAnimation(nullptr);

        if (getPlayerActor()->mTakeCount != 0) {
            MarioActor* actor = getPlayerActor();
            getPlayerActor()->mMarioAnim->updateTakingAnimation(actor->mTakeCount == 0 ? nullptr : actor->mTakeSensors[0]);
        }

        MR::validateHitSensors(getPlayerActor());
        getPlayerActor()->getSensor("dummy")->invalidate();
        getPlayerActor()->mDemoBaseKept = false;
    }

    bool isInWaterMode() {
        if (getPlayerActor()->getMario()->isStatusActive(MarioStatus_Swim)) {
            return true;
        }

        return getPlayerActor()->isAnimationRun("水泳ジェット");
    }

    void changeItemStatus(s32 itemStatus) {
        switch (itemStatus) {
        case 0:
        case 8:
            getPlayerActor()->setPlayerMode(PlayerMode_Normal, true);
            break;
        case 1:
            getPlayerActor()->setPlayerMode(PlayerMode_Hopper, true);
            break;
        case 2:
            getPlayerActor()->setPlayerMode(PlayerMode_Bee, true);
            break;
        case 3:
            getPlayerActor()->setPlayerMode(PlayerMode_Teresa, true);
            break;
        case 4:
            getPlayerActor()->setPlayerMode(PlayerMode_Ice, true);
            break;
        case 5:
            getPlayerActor()->setPlayerMode(PlayerMode_Fire, true);
            break;
        case 6:
            getPlayerActor()->setPlayerMode(PlayerMode_Foo, true);
            break;
        case 7:
            getPlayerActor()->setPlayerMode(PlayerMode_Invincible, true);
            break;
        case 9:
            getPlayerActor()->getMario()->mMovementStates.tornado = true;
            getPlayerActor()->getMario()->mTornadoTimer = 3;
            break;
        case 10:
            if (!getPlayerActor()->isEnableNerveChange()) {
                return;
            }

            if (getPlayerActor()->mMaxHealth > static_cast< u32 >(GameRules::playerHealth())) {
                MR::startSound(getPlayerActor(), "SE_OJ_MORPH_ITEM_INVALID");
                getPlayerActor()->changeMaxLife(GameRules::lifeUpHealth());
                return;
            }

            getPlayerActor()->mSuperKinokoCollected = true;
        }
    }
};  // namespace MarioAccess
