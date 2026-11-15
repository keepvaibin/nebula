#pragma once

#include <nebula_geometry.hpp>
#include "Game/LumaMarks.hpp"

class BckCtrlData;
class CameraTargetArg;
class CubeCameraArea;
class HitSensor;
class GameTexture;
class LiveActor;
class NameObj;
class Triangle;
class XanimeResourceTable;

// What MR::changePlayerItemStatus gives the player; the power-up items carry theirs.
enum PlayerItemStatus {
    PlayerItemStatus_Normal = 0,
    PlayerItemStatus_Hopper = 1,
    PlayerItemStatus_Bee = 2,
    PlayerItemStatus_Teresa = 3,
    PlayerItemStatus_Ice = 4,
    PlayerItemStatus_Fire = 5,
    PlayerItemStatus_Foo = 6,
    PlayerItemStatus_Invincible = 7,
    PlayerItemStatus_Cure = 8,  // back to normal from any element mode
    PlayerItemStatus_LifeUp = 10,
};

namespace MR {
    bool isOnGroundPlayer();
    Triangle* getPlayerGroundingPolygon();
    void forceKillPlayerByAbyss();
    void forceKillPlayerByWaterRace();
    void forceKillPlayerByGroundRace();
    void forceKillPlayerByGhostRace();
    bool isPlayerDead();
    bool isPlayerRefuseTalk();
    bool isPlayerTeresaDisappear();
    bool isPlayerInAreaObj(const char*);
    Vec3f* getPlayerPos();
    Vec3f* getPlayerCenterPos();
    void getPlayerTakePos(Vec3f*);
    void setPlayerPos(const Vec3f&);
    void setPlayerUpperRotateY(f32);
    Vec3f* getPlayerRotate();
    Vec3f* getPlayerShadowRotate();
    Vec3f* getPlayerVelocity();
    Vec3f* getPlayerLastMove();
    void setPlayerJumpVec(const Vec3f&);
    f32 getPlayerHitRadius();
    void setPlayerWalkingResist(f32);
    const Vec3f* getPlayerGravity();
    void calcPlayerSpinPullVelocity(Vec3f*, const Vec3f&);
    bool checkPlayerActionTrigger();
    bool checkPlayerSwingTrigger();
    f32 calcDistanceToPlayer(const Vec3f&);
    void getPlayerUpVec(LUMA_OUT Vec3f*);
    void getPlayerFrontVec(LUMA_OUT Vec3f*);
    void getPlayerSideVec(Vec3f*);
    void getPlayerThrowVec(Vec3f*);
    void getPlayerGroundPos(Vec3f*);
    const Vec3f* getPlayerGroundNormal();
    void setPlayerFrontTargetVec(const Vec3f&, s32);
    void setPlayerFrontVec(const Vec3f&, s32);
    void setPlayerSwingInhibitTimer(u16);
    void setPlayerSwingPermission(bool);
    void setPlayerStateWait();
    void startBckPlayer(const char*, const char* pBrkName = nullptr);
    void startBckPlayer(const char*, const BckCtrlData&);
    void startBckPlayer(const char*, s32);
    bool isBckStoppedPlayer();
    bool isBckOneTimeAndStoppedPlayer();
    f32 getBckFrameMaxPlayer();
    s32 getBckFrameMaxPlayer(const char*);
    void startBckPlayerJ(const char*);
    void becomeContinuousBckPlayer();
    void progressPlayerBckOnPause();
    const char* getPlayerCurrentBckName();
    void setBckBlendWeight(f32, f32);
    void setBckBlendWeight(f32, f32, f32);
    void setBckBlendWeight(f32, f32, f32, f32);
    void setBckRatePlayer(f32);
    XanimeResourceTable* getPlayerXanimeResource();
    void jumpPlayer(const Vec3f&);
    void forceJumpPlayer(const Vec3f&);
    void forceFlyPlayer(const Vec3f&, const Vec3f&, s32);
    void tornadoJumpPlayer();
    void miniTornadoJumpPlayer();
    void becomePlayerNormalJumpStatus();
    bool isOnPlayer(const HitSensor*);
    bool isActorOnPlayer(const LiveActor*);
    bool isOnPlayerShadow(const LiveActor*);
    f32 getPlayerShadowHeight();
    void setPlayerPos(const char*);
    void setPlayerPosAndWait(const Vec3f&);
    void setPlayerPosAndWait(const char*);
    void setPlayerLinkPosAndWait(const NameObj*, const char*);
    void setPlayerPosOnGround(const char*);
    void setPlayerPosOnGroundAndWait(const char*);
    bool isPlayerHipDropFalling();
    bool isPlayerHipDropLand();
    void incPlayerLife(u32);
    void incPlayerOxygen(u32);
    bool isPlayerConfrontDeath();
    void getStarPiecePlayer();
    void notifyDirectGetStarPiecePlayer();
    void scatterStarPiecePlayer(u32);
    bool isPlayerSwingAction();
    bool isPlayerPointedBy2POnTriggerButton();
    bool isPlayerSquat();
    bool isPlayerInRush();
    bool isPlayerNeedBrakingCamera();
    bool isPlayerFlying();
    bool isPlayerElementMode(s32);
    bool isPlayerElementModeTornado();
    bool isPlayerElementModeInvincible();
    bool isPlayerElementModeBee();
    bool isPlayerElementModeHopper();
    bool isPlayerElementModeTeresa();
    bool isPlayerElementModeIce();
    bool isPlayerElementModeNormal();
    bool isPlayerSkating();
    void changePlayerItemStatus(s32 status);
    void curePlayerElementMode();
    bool isPlayerParalyzing();
    bool isPlayerDamaging();
    bool isPlayerStaggering();
    bool isPlayerSwimming();
    bool isPlayerSleeping();
    bool isPlayerJumpRising();
    void validatePlayerSensor();
    bool isPlayerInBind();
    void endBindAndPlayerWait(LiveActor*);
    void endBindAndPlayerJump(LiveActor*, const Vec3f&, u32);
    void endBindAndPlayerForceJump(LiveActor*, const Vec3f&, u32);
    void endBindAndPlayerWeakGravityJump(LiveActor*, const Vec3f&);
    void endBindAndPlayerForceWeakGravityJump(LiveActor*, const Vec3f&);
    void endBindAndPlayerForceWeakGravityJumpInputOff(LiveActor*, const Vec3f&);
    void endBindAndPlayerWeakGravityLimitJump(LiveActor*, const Vec3f&);
    void endBindAndSpinDriverJump(LiveActor*, const Vec3f&);
    void endBindAndPlayerDamage(LiveActor*, const Vec3f&);
    void endBindAndPlayerFlip(LiveActor*, const Vec3f&);
    void endBindAndPlayerJumpWithRollLanding(LiveActor*, const Vec3f&, u32);
    void endBindAndPlayerDamageMsg(LiveActor*, u32);
    void endBindAndPlayerDamageMsg(LiveActor*, u32, const Vec3f&);
    void endBindAndPlayerAcidDamage(LiveActor*);
    void endBindAndPlayerFreezeDamage(LiveActor*);
    void endBindAndPlayerFireDamage(LiveActor*);
    void endBindAndPlayerElectricDamage(LiveActor*);
    LiveActor* getCurrentRushActor();
    HitSensor* getCurrentRushSensor();
    void tryPlayerCoinPull();
    void tryPlayerPullActor(HitSensor*);
    void tryPlayerDropTakingActor();
    void tryPlayerKillTakingActor();
    bool isPlayerTakingActor(const char*);
    bool isPlayerCarryAny();
    void startSoundPlayer(const char*, s32 = -1);
    void startLevelSoundPlayer(const char*, s32 = -1);
    void stopSoundPlayer(const char*, u32);
    void startSoundPlayerJ(const char*);
    void showPlayer();
    void hidePlayer();
    void showPlayerJoint(const char*);
    void hidePlayerJoint(const char*);
    void startPlayerDownWipe();
    void setCameraTargetToPlayer(CameraTargetArg*);
    bool isPlayerDisableFpView();
    bool isFpViewChangingFailure();
    void stopPlayerFpView();
    void setRasterScroll(s32, s32, s32);
    void noticePlayerDashChance();
    void startPlayerTalk(const LiveActor*);
    void endPlayerTalk();
    void preventPlayerRush();
    bool isExistMario();
    void startPlayerEvent(const char*);
    void offPlayerControl();
    void onPlayerControl(bool);
    bool isOffPlayerControl();
    void unlockPlayerAnimation();
    void resetPlayerStatus();
    void resetPlayerEffect();
    void setPlayerBaseMtx(LUMA_IN MtxPtr);
    MtxPtr getPlayerBaseMtx();
    LiveActor* getPlayerDemoActor();
    void initPlayerAfterOpeningDemo();
    void readyPlayerDemo();
    void onFollowDemoEffect();
    void requestMovementOnPlayer();
    void calcPlayerJointMtx(Pos3f*, const char*);
    bool isPlayerOnPress();
    void pushPlayer(const Vec3f&);
    void pushPlayerFromArea(const Vec3f&);
    bool isPlayerInWaterMode();
    bool isPlayerOnWaterSurface();
    bool isPlayerHidden();
    void calcPlayerWorldPadDir(Vec3f*, f32, f32);
    GameTexture* getFullScreenBlurTexture();
    u16 getPlayerMovementTimer();
    CubeCameraArea* getCameraCube();
};  // namespace MR
