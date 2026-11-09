#include "Game/Util/PlayerUtil.hpp"
#include "Game/Camera/CameraTargetArg.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/MapObj/StarPieceDirector.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/PlayerEvent.hpp"
#include "Game/Player/RushEndInfo.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"

namespace MR {
    bool isOnGroundPlayer() {
        return MarioAccess::isOnGround(0);
    }

    Triangle* getPlayerGroundingPolygon() {
        return MarioAccess::getGroundingPolygon(0);
    }

    void forceKillPlayerByAbyss() {
        return MarioAccess::forceKill(0, 0);
    }

    void forceKillPlayerByWaterRace() {
        return MarioAccess::forceKill(1, 0);
    }

    void forceKillPlayerByGroundRace() {
        return MarioAccess::forceKill(5, 0);
    }

    void forceKillPlayerByGhostRace() {
        return MarioAccess::forceKill(2, 0);
    }

    bool isPlayerDead() {
        return !MarioAccess::getPlayerActor()->isEnableNerveChange();
    }

    bool isPlayerRefuseTalk() {
        return MarioAccess::getPlayerActor()->isRefuseTalk();
    }

    bool isPlayerTeresaDisappear() {
        return MarioAccess::isTeresaDisappear();
    }

    bool isPlayerInAreaObj(const char* pName) {
        return isInAreaObj(pName, MarioAccess::getPlayerActor()->mPosition);
    }

    Vec3f* getPlayerPos() {
        return &MarioAccess::getPlayerActor()->mPosition;
    }

    Vec3f* getPlayerCenterPos() {
        return &MarioAccess::getPlayerActor()->mCenterPos;
    }

    void getPlayerTakePos(Vec3f* pPos) {
        return MarioAccess::getTakePos(pPos);
    }

    void setPlayerPos(const Vec3f& rPos) {
        MarioAccess::setTrans(rPos, 0);
    }

    void setPlayerUpperRotateY(f32 angle) {
        MarioAccess::getPlayerActor()->setUpperRotateY(angle);
    }

    Vec3f* getPlayerRotate() {
        return &MarioAccess::getPlayerActor()->mRotation;
    }

    Vec3f* getPlayerShadowRotate() {
        return &MarioAccess::getPlayerActor()->mShadowRotate;
    }

    Vec3f* getPlayerVelocity() {
        return MarioAccess::getVelocity();
    }

    Vec3f* getPlayerLastMove() {
        return MarioAccess::getLastMove();
    }

    void setPlayerJumpVec(const Vec3f& rJumpVec) {
        MarioAccess::setJumpVec(rJumpVec);
    }

    f32 getPlayerHitRadius() {
        return MarioAccess::getPlayerActor()->mPlayerMode == (s16)6 ? 100.0f : 60.0f;
    }

    void setPlayerWalkingResist(f32 resist) {
        return MarioAccess::setWalkingResist(resist);
    }

    const Vec3f* getPlayerGravity() {
        return &MarioAccess::getPlayerActor()->getGravityVec();
    }

    void calcPlayerSpinPullVelocity(Vec3f* pVelocity, const Vec3f& rPos) {
        MarioAccess::calcSpinPullVelocity(pVelocity, rPos);
    }

    bool checkPlayerActionTrigger() {
        return MarioAccess::getPlayerActor()->mActionTrigger;
    }

    bool checkPlayerSwingTrigger() {
        return MarioAccess::getPlayerActor()->mSwingTrigger;
    }

    f32 calcDistanceToPlayer(const Vec3f& rPos) {
        return getPlayerPos()->distance(rPos);
    }

    void getPlayerUpVec(Vec3f* pVec) {
        MarioAccess::getPlayerActor()->getUpVec(pVec);
    }

    void getPlayerFrontVec(Vec3f* pVec) {
        MarioAccess::getPlayerActor()->getFrontVec(pVec);
    }

    void getPlayerSideVec(Vec3f* pVec) {
        MarioAccess::getPlayerActor()->getSideVec(pVec);
    }

    void getPlayerThrowVec(Vec3f* pVec) {
        MarioAccess::getThrowVec(pVec);
    }

    void getPlayerGroundPos(Vec3f* pPos) {
        MarioAccess::getPlayerActor()->getGroundPos(pPos);
    }

    const Vec3f* getPlayerGroundNormal() {
        return getPlayerGroundingPolygon()->getFaceNormal();
    }

    void setPlayerFrontTargetVec(const Vec3f& rTarget, s32 frontMode) {
        MarioAccess::setFrontVecTarget(rTarget, frontMode);
    }

    void setPlayerFrontVec(const Vec3f& rVec, s32 frontMode) {
        MarioAccess::setFrontVecKeepUp(rVec, frontMode);
    }

    void setPlayerSwingInhibitTimer(u16 timer) {
        MarioAccess::getPlayerActor()->mSwingInhibitTimer = timer;
    }

    void setPlayerSwingPermission(bool permission) {
        MarioAccess::getPlayerActor()->mSwingPermission = permission;
    }

    void setPlayerStateWait() {
        MarioAccess::setStateWait();
    }

    void startBckPlayer(const char* pName, const char* pBrkName) {
        if (pBrkName != nullptr) {
            MarioAccess::changeAnimationE(pName, pBrkName);
        } else {
            MarioAccess::changeAnimationE(pName, pName);
        }
    }

    void startBckPlayer(const char* pName, const BckCtrlData& rCtrlData) {
        MarioAccess::changeAnimationE(pName, rCtrlData);
    }

    void startBckPlayer(const char* pName, s32 interpole) {
        MarioAccess::changeAnimationE(pName, interpole);
    }

    bool isBckStoppedPlayer() {
        return MR::isBckStopped(MarioAccess::getPlayerActor());
    }

    bool isBckOneTimeAndStoppedPlayer() {
        return MR::isBckOneTimeAndStopped(MarioAccess::getPlayerActor());
    }

    f32 getBckFrameMaxPlayer() {
        return MarioAccess::getAnimationFrameMax();
    }

    s32 getBckFrameMaxPlayer(const char* pName) {
        return MR::getBckFrameMax(MarioAccess::getPlayerActor(), pName);
    }

    void startBckPlayerJ(const char* pName) {
        MarioAccess::changeAnimationJ(pName);
    }

    void becomeContinuousBckPlayer() {
        MarioAccess::keepCurrentAnimation();
    }

    void progressPlayerBckOnPause() {
        MarioAccess::progressAnimation();
    }

    const char* getPlayerCurrentBckName() {
        return MarioAccess::getCurrentBckName();
    }

    void setBckBlendWeight(f32 weight0, f32 weight1) {
        const f32 blendWeight[] = {weight0, weight1};

        MarioAccess::setAnimationBlendWeight(blendWeight);
    }

    void setBckBlendWeight(f32 weight0, f32 weight1, f32 weight2) {
        const f32 blendWeight[] = {weight0, weight1, weight2};

        MarioAccess::setAnimationBlendWeight(blendWeight);
    }

    void setBckBlendWeight(f32 weight0, f32 weight1, f32 weight2, f32 weight3) {
        const f32 blendWeight[] = {weight0, weight1, weight2, weight3};

        MarioAccess::setAnimationBlendWeight(blendWeight);
    }

    void setBckRatePlayer(f32 rate) {
        MR::setBckRate(MarioAccess::getPlayerActor(), rate);
    }

    XanimeResourceTable* getPlayerXanimeResource() {
        return MarioAccess::getPlayerActor()->getResourceTable();
    }

    void jumpPlayer(const Vec3f& rJumpVec) {
        MarioAccess::freeJump(rJumpVec, 0);
    }

    void forceJumpPlayer(const Vec3f& rJumpVec) {
        MarioAccess::forceJump(rJumpVec, 0);
    }

    void forceFlyPlayer(const Vec3f& rTarget, const Vec3f& rDir, s32 frames) {
        MarioAccess::forceFly(rTarget, rDir, frames);
    }

    void tornadoJumpPlayer() {
        MarioAccess::tornadoJump();
    }

    void miniTornadoJumpPlayer() {
        MarioAccess::tornadoJumpMini();
    }

    void becomePlayerNormalJumpStatus() {
        MarioAccess::becomeNormalJumpStatus();
    }

    bool isOnPlayer(const HitSensor* pSensor) {
        Triangle* polygon = getPlayerGroundingPolygon();

        if (polygon != nullptr) {
            return polygon->mSensor == pSensor;
        }

        return false;
    }

    bool isActorOnPlayer(const LiveActor* pActor) {
        return MarioAccess::isOnActor(pActor);
    }

    bool isOnPlayerShadow(const LiveActor* pActor) {
        Triangle* polygon = MarioAccess::getShadowingPolygon();

        if (polygon != nullptr) {
            return polygon->mSensor->mHost == pActor;
        }

        return false;
    }

    f32 getPlayerShadowHeight() {
        return MarioAccess::getShadowHeight();
    }

    void setPlayerPos(const char* pPosName) {
        Pos3f mtx;
        mtx.identity();

        MR::findNamePos(pPosName, mtx.toMtxPtr());
        MarioAccess::setBaseMtx(mtx.toMtxPtr());

        Vec3f trans;
        MR::extractMtxTrans(mtx.toMtxPtr(), &trans);

        setPlayerPos(trans);
    }

    void setPlayerPosAndWait(const Vec3f& rPos) {
        setPlayerPos(rPos);
        MarioAccess::getPlayerActor()->resetCondition();
        MarioAccess::setStateWait();
    }

    void setPlayerPosAndWait(const char* pPosName) {
        setPlayerPos(pPosName);
        MarioAccess::getPlayerActor()->resetCondition();
        MarioAccess::setStateWait();
    }

    void setPlayerLinkPosAndWait(const NameObj* pLinkObj, const char* pPosName) {
        Pos3f mtx;
        mtx.identity();

        MR::findLinkNamePos(pLinkObj, pPosName, mtx.toMtxPtr());
        MarioAccess::setBaseMtx(mtx.toMtxPtr());
        MarioAccess::getPlayerActor()->resetCondition();
        MarioAccess::setStateWait();
    }

    void setPlayerPosOnGround(const char* pPosName) {
        Pos3f mtx;
        mtx.identity();

        MR::findNamePosOnGround(pPosName, mtx.toMtxPtr());
        MarioAccess::setBaseMtx(mtx.toMtxPtr());
    }

    void setPlayerPosOnGroundAndWait(const char* pPosName) {
        Pos3f mtx;
        mtx.identity();

        // A name the stage has no position of, from a mod's cutscene: the player stays where they are.
        Pos3f found;
        if (!MR::tryFindNamePos(pPosName, found.toMtxPtr())) {
            return;
        }

        MR::findNamePosOnGround(pPosName, mtx.toMtxPtr());
        MarioAccess::setBaseMtx(mtx.toMtxPtr());
        MarioAccess::getPlayerActor()->resetCondition();
        MarioAccess::setStateWait();
    }

    bool isPlayerHipDropFalling() {
        return MarioAccess::isHipDropFalling();
    }

    bool isPlayerHipDropLand() {
        return MarioAccess::isHipDropLand();
    }

    void incPlayerLife(u32 add) {
        MarioAccess::incLife(add);
    }

    void incPlayerOxygen(u32 add) {
        MarioAccess::incOxygen(add);
    }

    bool isPlayerConfrontDeath() {
        return MarioAccess::isConfrontDeath();
    }

    void getStarPiecePlayer() {
        MarioAccess::addStarPiece();
    }

    void notifyDirectGetStarPiecePlayer() {
        MarioAccess::getStarPieceDirect();
    }

    void scatterStarPiecePlayer(u32 num) {
        MarioAccess::scatterStarPiece(num);
    }

    bool isPlayerSwingAction() {
        return MarioAccess::isSwingAction();
    }

    bool isPlayerPointedBy2POnTriggerButton() {
        return MR::isStarPointerPointing2POnTriggerButton(MarioAccess::getPlayerActor(), "弱", true, false);
    }

    bool isPlayerSquat() {
        return MarioAccess::isSquat();
    }

    bool isPlayerInRush() {
        return MarioAccess::isInRush();
    }

    bool isPlayerNeedBrakingCamera() {
        return MarioAccess::isNeedBrakingCamera();
    }

    bool isPlayerFlying() {
        return MarioAccess::isFlying();
    }

    bool isPlayerElementMode(s32 mode) {
        return MarioAccess::getPlayerActor()->mPlayerMode == mode;
    }

    bool isPlayerElementModeTornado() {
        return isPlayerElementMode(PlayerMode_Tornado);
    }

    bool isPlayerElementModeInvincible() {
        return isPlayerElementMode(PlayerMode_Invincible);
    }

    bool isPlayerElementModeBee() {
        return isPlayerElementMode(PlayerMode_Bee);
    }

    bool isPlayerElementModeHopper() {
        return isPlayerElementMode(PlayerMode_Hopper);
    }

    bool isPlayerElementModeTeresa() {
        return isPlayerElementMode(PlayerMode_Teresa);
    }

    bool isPlayerElementModeIce() {
        return isPlayerElementMode(PlayerMode_Ice);
    }

    bool isPlayerElementModeNormal() {
        return isPlayerElementMode(PlayerMode_Normal);
    }

    bool isPlayerSkating() {
        return MarioAccess::isSkating();
    }

    void changePlayerItemStatus(s32 status) {
        MarioAccess::changeItemStatus(status);
    }

    void curePlayerElementMode() {
        changePlayerItemStatus(PlayerItemStatus_Cure);
    }

    bool isPlayerParalyzing() {
        return MarioAccess::isParalyzing();
    }

    bool isPlayerDamaging() {
        return MarioAccess::getPlayerActor()->isDamaging();
    }

    bool isPlayerStaggering() {
        return MarioAccess::getPlayerActor()->isStaggering();
    }

    bool isPlayerSwimming() {
        return MarioAccess::isSwimming();
    }

    bool isPlayerSleeping() {
        return MarioAccess::getPlayerActor()->isSleeping();
    }

    bool isPlayerJumpRising() {
        return MarioAccess::getPlayerActor()->isJumpRising();
    }

    void validatePlayerSensor() {
        return MarioAccess::validateSensor();
    }

    bool isPlayerInBind() {
        return MarioAccess::isInRush();
    }

    void endBindAndPlayerWait(LiveActor* pActor) {
        Vec3f velocity(0.0f, 0.0f, 0.0f);
        RushEndInfo info(pActor, RushEndInfo::Mode_Wait, velocity, false, 0);

        MarioAccess::endRush(&info);
    }

    void endBindAndPlayerJump(LiveActor* pActor, const Vec3f& rJumpVec, u32 blendFrames) {
        RushEndInfo info = RushEndInfo(pActor, RushEndInfo::Mode_Jump, rJumpVec, true, blendFrames);

        MarioAccess::endRush(&info);
    }

    void endBindAndPlayerForceJump(LiveActor* pActor, const Vec3f& rJumpVec, u32 blendFrames) {
        RushEndInfo info = RushEndInfo(pActor, RushEndInfo::Mode_Jump, rJumpVec, true, blendFrames);
        info.mForced = true;

        MarioAccess::endRush(&info);
    }

    void endBindAndPlayerWeakGravityJump(LiveActor* pActor, const Vec3f& rJumpVec) {
        RushEndInfo info = RushEndInfo(pActor, RushEndInfo::Mode_PowerJump, rJumpVec, true, 0);

        MarioAccess::endRush(&info);
    }

    void endBindAndPlayerForceWeakGravityJump(LiveActor* pActor, const Vec3f& rJumpVec) {
        RushEndInfo info = RushEndInfo(pActor, RushEndInfo::Mode_PowerJump, rJumpVec, true, 0);
        info.mForced = true;

        MarioAccess::endRush(&info);
    }

    void endBindAndPlayerForceWeakGravityJumpInputOff(LiveActor* pActor, const Vec3f& rJumpVec) {
        RushEndInfo info = RushEndInfo(pActor, RushEndInfo::Mode_PowerJump, rJumpVec, true, 0);
        info.mForced = true;
        info.mInputOff = true;

        MarioAccess::endRush(&info);
    }

    void endBindAndPlayerWeakGravityLimitJump(LiveActor* pActor, const Vec3f& rJumpVec) {
        RushEndInfo info = RushEndInfo(pActor, RushEndInfo::Mode_PowerJump, rJumpVec, true, 0);
        info.mLimitJump = true;

        MarioAccess::endRush(&info);
    }

    void endBindAndSpinDriverJump(LiveActor* pActor, const Vec3f& rJumpVec) {
        RushEndInfo info = RushEndInfo(pActor, RushEndInfo::Mode_PowerJump, rJumpVec, true, 0);
        info.mForced = true;
        info.mInputOff = true;

        MarioAccess::endRush(&info);
    }

    void endBindAndPlayerDamage(LiveActor* pActor, const Vec3f& rDir) {
        RushEndInfo info = RushEndInfo(pActor, RushEndInfo::Mode_PowerJump, rDir, true, 0);
        info.mForced = true;
        info.mInputOff = true;
        info.mDamage = RushEndInfo::Damage_Hit;

        MarioAccess::endRush(&info);
    }

    void endBindAndPlayerFlip(LiveActor* pActor, const Vec3f& rDir) {
        RushEndInfo info = RushEndInfo(pActor, RushEndInfo::Mode_PowerJump, rDir, true, 0);
        info.mForced = true;
        info.mInputOff = true;
        info.mDamage = RushEndInfo::Damage_Flip;

        MarioAccess::endRush(&info);
    }

    void endBindAndPlayerJumpWithRollLanding(LiveActor* pActor, const Vec3f& rJumpVec, u32 blendFrames) {
        RushEndInfo info = RushEndInfo(pActor, RushEndInfo::Mode_PowerJump, rJumpVec, true, blendFrames);
        info.mRollLanding = true;

        MarioAccess::endRush(&info);
    }

    void endBindAndPlayerDamageMsg(LiveActor* pActor, u32 msg) {
        endBindAndPlayerDamageMsg(pActor, msg, Vec3f(0.0f, 0.0f, 0.0f));
    }

    void endBindAndPlayerDamageMsg(LiveActor* pActor, u32 msg, const Vec3f& rDir) {
        switch (msg) {
        case ACTMES_ENEMY_ATTACK_FIREBACK:
        case ACTMES_ENEMY_ATTACK_FIRERUN:
        case ACTMES_ENEMY_ATTACK_FIRERUN_STRONG:
            endBindAndPlayerFireDamage(pActor);
            break;
        case ACTMES_ENEMY_ATTACK_ACID:
            endBindAndPlayerAcidDamage(pActor);
            break;
        case ACTMES_ENEMY_ATTACK_FREEZE:
            endBindAndPlayerFreezeDamage(pActor);
            break;
        case ACTMES_ENEMY_ATTACK_ELECTRIC:
        case ACTMES_ENEMY_ATTACK_HEATBEAM:
            endBindAndPlayerElectricDamage(pActor);
            break;
        case ACTMES_ENEMY_ATTACK_FLIP_VERYWEAK:
        case ACTMES_ENEMY_ATTACK_FLIP_WEAK:
        case ACTMES_ENEMY_ATTACK_FLIP_WEAK_JUMP:
        case ACTMES_ENEMY_ATTACK_FLIP_ROT:
        case ACTMES_ENEMY_ATTACK_FLIP_MAXIMUM:
            endBindAndPlayerWeakGravityLimitJump(pActor, rDir);
            break;
        case ACTMES_ENEMY_ATTACK_FLIP:
            endBindAndPlayerFlip(pActor, rDir);
            break;
        default:
            endBindAndPlayerDamage(pActor, rDir);
            break;
        }
    }

    void endBindAndPlayerAcidDamage(LiveActor* pActor) {
        RushEndInfo info = RushEndInfo(pActor, RushEndInfo::Mode_PowerJump, Vec3f(0.0f, 0.0f, 0.0f), true, 0);
        info.mForced = true;
        info.mInputOff = true;
        info.mDamage = RushEndInfo::Damage_Acid;

        MarioAccess::endRush(&info);
    }

    void endBindAndPlayerFreezeDamage(LiveActor* pActor) {
        RushEndInfo info = RushEndInfo(pActor, RushEndInfo::Mode_PowerJump, Vec3f(0.0f, 0.0f, 0.0f), true, 0);
        info.mForced = true;
        info.mInputOff = true;
        info.mDamage = RushEndInfo::Damage_Freeze;

        MarioAccess::endRush(&info);
    }

    void endBindAndPlayerFireDamage(LiveActor* pActor) {
        RushEndInfo info = RushEndInfo(pActor, RushEndInfo::Mode_PowerJump, Vec3f(0.0f, 0.0f, 0.0f), true, 0);
        info.mForced = true;
        info.mInputOff = true;
        info.mDamage = RushEndInfo::Damage_Fire;

        MarioAccess::endRush(&info);
    }

    void endBindAndPlayerElectricDamage(LiveActor* pActor) {
        RushEndInfo info = RushEndInfo(pActor, RushEndInfo::Mode_PowerJump, Vec3f(0.0f, 0.0f, 0.0f), true, 0);
        info.mForced = true;
        info.mInputOff = true;
        info.mDamage = RushEndInfo::Damage_Electric;

        MarioAccess::endRush(&info);
    }

    LiveActor* getCurrentRushActor() {
        if (isPlayerInRush() == false) {
            return nullptr;
        }

        return MarioAccess::getPlayerActor()->mRushSensor->mHost;
    }

    HitSensor* getCurrentRushSensor() {
        if (isPlayerInRush() == false) {
            return nullptr;
        }

        return MarioAccess::getPlayerActor()->mRushSensor;
    }

    void tryPlayerCoinPull() {
        MarioAccess::tryCoinPull();
    }

    void tryPlayerPullActor(HitSensor* pSensor) {
        MarioAccess::getPlayerActor()->tryTornadoPull(pSensor);
    }

    void tryPlayerDropTakingActor() {
        MarioAccess::dropTakingActor();
    }

    void tryPlayerKillTakingActor() {
        MarioAccess::killTakingActor();
    }

    bool isPlayerTakingActor(const char* pName) {
        if (MarioAccess::getTakingSensor() != nullptr) {
            if (MR::isEqualString(MarioAccess::getTakingSensor()->mHost->mName, pName)) {
                return true;
            }
        }

        return false;
    }

    bool isPlayerCarryAny() {
        return MarioAccess::getPlayerActor()->mTakeCount != 0;
    }

    void startSoundPlayer(const char* pName, s32 level) {
        MR::startSound(MarioAccess::getPlayerActor(), pName, level);
    }

    void startLevelSoundPlayer(const char* pName, s32 level) {
        MR::startLevelSound(MarioAccess::getPlayerActor(), pName, level);
    }

    void stopSoundPlayer(const char* pName, u32 fadeTime) {
        MR::stopSound(MarioAccess::getPlayerActor(), pName, fadeTime);
    }

    void startSoundPlayerJ(const char* pName) {
        MarioAccess::getPlayerActor()->playSound(pName, -1);
    }

    void showPlayer() {
        MarioAccess::show();
    }

    void hidePlayer() {
        MarioAccess::hide();
    }

    void showPlayerJoint(const char* pName) {
        MR::showJoint(MarioAccess::getPlayerActor(), pName);
    }

    void hidePlayerJoint(const char* pName) {
        MR::hideJoint(MarioAccess::getPlayerActor(), pName);
    }

    void startPlayerDownWipe() {
        MarioAccess::startDownWipe();
    }

    void setCameraTargetToPlayer(CameraTargetArg* pTarget) {
        pTarget->mMarioActor = MarioAccess::getPlayerActor();
        pTarget->mTargetObj = nullptr;
        pTarget->mTargetMtx = nullptr;
        pTarget->mLiveActor = nullptr;
    }

    bool isPlayerDisableFpView() {
        return MarioAccess::isDisableFpView();
    }

    bool isFpViewChangingFailure() {
        return MarioAccess::isFpViewChangingFailure();
    }

    void stopPlayerFpView() {
        return MarioAccess::stopFpView();
    }

    void setRasterScroll(s32 amp, s32 phase, s32 wave) {
        MarioAccess::getPlayerActor()->setRasterScroll(amp, phase, wave);
    }

    void noticePlayerDashChance() {
        MarioAccess::noticeDashChance();
    }

    void startPlayerTalk(const LiveActor* pActor) {
        MarioAccess::startTalk(pActor);
    }

    void endPlayerTalk() {
        MarioAccess::endTalk();
    }

    void preventPlayerRush() {
        MarioAccess::preventRush();
    }

    bool isExistMario() {
        return MR::isExistSceneObj(SceneObj_MarioHolder);
    }

    void startPlayerEvent(const char* pName) {
        EventSequencer* eventSequencer;

        eventSequencer = MR::getSceneObj< EventSequencer >(SceneObj_EventSequencer);
        eventSequencer->startEvent(pName);

        requestMovementOn(eventSequencer);
        requestMovementOnPlayer();
    }

    void offPlayerControl() {
        MarioAccess::offControl();
    }

    void onPlayerControl(bool resetCondition) {
        MarioAccess::onControl(resetCondition);
    }

    bool isOffPlayerControl() {
        return MarioAccess::isOffControl();
    }

    void unlockPlayerAnimation() {
        MarioAccess::getPlayerActor()->setAnimationFrozen(false);
    }

    void resetPlayerStatus() {
        MarioAccess::getPlayerActor()->resetCondition();
    }

    void resetPlayerEffect() {
        MR::forceDeleteEffectAll(MarioAccess::getPlayerActor());
        resetChasingStarPiece();
    }

    void setPlayerBaseMtx(MtxPtr pMtx) {
        MarioAccess::setBaseMtx(pMtx);
    }

    MtxPtr getPlayerBaseMtx() {
        return MarioAccess::getBaseMtx();
    }

    LiveActor* getPlayerDemoActor() {
        return MarioAccess::getPlayerActor();
    }

    void initPlayerAfterOpeningDemo() {
        MarioAccess::getPlayerActor()->initAfterOpeningDemo();
    }

    void readyPlayerDemo() {
        MarioAccess::readyDemo();
    }

    void onFollowDemoEffect() {
        MarioAccess::onFollowDemo();
    }

    void requestMovementOnPlayer() {
        requestMovementOn(MarioAccess::getPlayerActor());
    }

    void calcPlayerJointMtx(Pos3f* pMtx, const char* pName) {
        pMtx->set(MarioAccess::getJointMtx(pName));
    }

    bool isPlayerOnPress() {
        return MarioAccess::isOnPress();
    }

    void pushPlayer(const Vec3f& rAccel) {
        MarioAccess::addVelocity(rAccel);
    }

    void pushPlayerFromArea(const Vec3f& rAccel) {
        MarioAccess::addVelocityFromArea(rAccel);
    }

    bool isPlayerInWaterMode() {
        return MarioAccess::isInWaterMode();
    }

    bool isPlayerOnWaterSurface() {
        return MarioAccess::isOnWaterSurface();
    }

    bool isPlayerHidden() {
        return isPlayerElementMode(6);
    }

    void calcPlayerWorldPadDir(Vec3f* pDir, f32 stickX, f32 stickY) {
        MarioAccess::calcWorldPadDir(pDir, stickX, stickY);
    }

    GameTexture* getFullScreenBlurTexture() {
        return MarioAccess::getPlayerActor()->mBlurTexture;
    }

    u16 getPlayerMovementTimer() {
        return MarioAccess::getPlayerActor()->mMovementTimer;
    }

    CubeCameraArea* getCameraCube() {
        return MarioAccess::getCameraCubeCode();
    }
};  // namespace MR
