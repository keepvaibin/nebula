# Fields

3429 fields a script reads (`object.mVelocity`) or writes. Handles, text and pointers are read-only; a class has its bases' fields too.

## NameObj

- `mDrawBufferPlace`: s32
- `mDrawSlot`: u32
- `mExecutorIdx`: s32
- `mHolderPlace`: s32
- `mListPlace`: s32 (array)
- `mLumaClass`: u16
- `mMotionEpoch`: u32
- `mMovementFlag`: u16
- `mName`: const char *

## Air

- `mDistance`: f32
- `mIsChanged`: u8
- `mIsWaitStartCamera`: _Bool

## AirBubble

- `mBobOffset`: f32
- `mIsFollowRail`: _Bool
- `mIsItemMoving`: _Bool
- `mLife`: s32
- `mRailMoveSpeed`: f32

## AirBubbleGenerator

- `mBubbleLifeTime`: s32
- `mWaitTime`: s32

## NameObjGroup

- `mObjNum`: s32
- `mObjNumMax`: s32

## AllLiveActorGroup

- `mActorNum`: s32

## MapObjActor

- `mBloomModel`: ModelObj *
- `mIsMirrorFromModel`: u8
- `mIsProjmapEveryFrame`: u8
- `mModelObj`: ModelObj *
- `mObjectName`: const char *
- `mPlayRiddleSE`: u8
- `mRailGuideDrawer`: MapPartsRailGuideDrawer *
- `mRailMover`: MapPartsRailMover *
- `mRailPosture`: MapPartsRailPosture *
- `mRailRotator`: MapPartsRailRotator *
- `mRotator`: MapPartsRotatorBase *
- `mStageEffectMovingSe`: const char *
- `mStageEffectName`: const char *

## AnmModelObj

- `mDoneLevelSe`: const char *

## AreaObj

- `mFormType`: int
- `mIsAwake`: _Bool
- `mIsFollowValid`: _Bool
- `mIsValid`: _Bool
- `mObjArg0`: s32
- `mObjArg1`: s32
- `mObjArg2`: s32
- `mObjArg3`: s32
- `mObjArg4`: s32
- `mObjArg5`: s32
- `mObjArg6`: s32
- `mObjArg7`: s32

## AreaObjContainer

- `mHashesShared`: _Bool

## AreaObjMgr

- `mFirstCapacity`: s32

## DynamicCollisionObj

- `mPositionNum`: s32
- `mTriangleNum`: s32

## AreaPolygon

- `mOffset`: f32

## ArrowSwitch

- `mIsPunch`: _Bool
- `mIsRotPlus`: _Bool
- `mLockAfterRotate`: _Bool
- `mRotAngle`: f32
- `mRotSpeed`: f32
- `mRotYTargetIndex`: s32
- `mSwitchType`: s32

## ArrowSwitchMulti

- `mActiveTargetIndex`: u32
- `mIsRotPlus`: _Bool
- `mRotAngle`: f32
- `mRotSpeed`: f32
- `mRotYTargetIndex`: s32
- `mTargetArray`: ArrowSwitchTarget * (array)

## ArrowSwitchTarget

- `mTargetIndex`: s32

## AssemblyBlock

- `mBloomModel`: PartsModel *
- `mFloatRotateSpeed`: f32
- `mIsTimerType`: _Bool
- `mObjArg7`: s32
- `mPlayerSearchDistance`: f32
- `mTimer`: s32

## AstroChangeStageCube

- `mDestNo`: u32
- `mWaitFrame`: s32

## AstroCountDownPlate

- `mIsLightOn`: _Bool

## AstroDomeBlueStar

- `mCaptureRibbon`: GCaptureRibbon *

## AstroDomeCameraController

- `mEaseRate`: f32

## AstroDomeComet

- `mBloomModel`: PartsModel *
- `mMiniGalaxy`: MiniatureGalaxy *

## AstroDomeDemoStarter

- `mPathDrawer`: SpinDriverPathDrawer *

## AstroDomeGalaxySelector

- `mBackButton`: GalaxySelectBackButton *
- `mCameraController`: AstroDomeCameraController *
- `mConfirmLayout`: GalaxyConfirmLayout *
- `mInfo`: GalaxySelectInfo *
- `mIsBackButtonPointed`: _Bool

## AstroDomeOrbit

- `mAngle`: f32
- `mOrbitRadius`: f32
- `mRingCos`: f32 (array)
- `mRingSin`: f32 (array)

## AstroDomeSky

- `mBrkFrame`: f32

## SimpleEffectObj

- `mObjName`: const char *

## AstroEffectObj

- `mDomeId`: s32

## AstroMapObj

- `mDomeId`: s32
- `mEngineSound`: const char *
- `mIsDomeEntrance`: _Bool
- `mIsDoor`: _Bool
- `mIsKitchenEntrance`: _Bool
- `mIsStarPlate`: _Bool
- `mNamePlate`: GalaxyNamePlate *
- `mObjName`: const char *
- `mRevivalSound`: const char *

## AstroOverlookObj

- `mIsLayoutHidden`: u8

## AstroSimpleObj

- `mNamePlate`: GalaxyNamePlate *

## AudBgmConductor

- `mFadeInDelay`: s32
- `mStartDelay`: s32
- `mState`: s32
- `mStateBits`: u32
- `mStopImmediately`: _Bool

## AudEffectDirector

- `mAppliedType`: s32
- `mEffectType`: s32
- `mPriority`: s32

## AudioEffectArea

- `mEffectParam`: s32
- `mEffectType`: s32

## BackButton

- `mIsSelected`: _Bool

## BallBeamer

- `mIsSwitchOn`: _Bool

## BallOpener

- `mBoundSensor`: HitSensor *
- `mSettledFrames`: s32

## BallRail

- `mAcceleration`: f32
- `mBindSensor`: HitSensor *
- `mBindSensorRadius`: f32
- `mDeceleration`: f32

## Balloonfish

- `mNotBoundStep`: s32
- `mRotateAngle`: f32

## Banekiti

- `mRailMover`: MapPartsRailMover *

## BasaBasa

- `mAttackDistance`: f32
- `mChaseDistance`: f32
- `mChaseTurnAngle`: f32
- `mClipStep`: s32
- `mHangModel`: ModelObj *
- `mHomeDistance`: f32
- `mIsIceModel`: _Bool
- `mIsOnCeiling`: _Bool

## BeamGoRoundBeam

- `mBloomModel`: ModelObj *
- `mModelDrawer`: VolumeModelDrawer *

## BeeFlowerHover

- `mRailMover`: MapPartsRailMover *
- `mRailPosture`: MapPartsRailPosture *
- `mRotateSpeed`: f32

## BegomanAttackPermitter

- `mBegoman`: BegomanBase *
- `mDistToPlayer`: f32
- `mHasRequest`: _Bool

## BegomanBase

- `mCanTrySetReturn`: _Bool
- `mElectricCounter`: s32
- `mInvertSideVec`: _Bool
- `mIsTouchElectricRail`: _Bool
- `mTiredCounter`: s32

## BegomanBaby

- `mAppearThreeStarPiece`: _Bool
- `mHost`: NameObj *

## PartsModel

- `mHost`: LiveActor *
- `mIsCalcOwnMtx`: _Bool
- `mIsDead`: _Bool
- `mIsShownWhileHostHidden`: _Bool

## BegomanLauncher

- `mAppearDelay`: s32
- `mBegomanCount`: s32
- `mLaunchType`: s32

## BegomanSpike

- `mBrokenModel`: ModelObj *
- `mHead`: BegomanHead *
- `mTurnAfterBlow`: _Bool

## BegomanSpring

- `mBaby`: BegomanBaby *
- `mHead`: BegomanSpringHead *
- `mIsCameraActive`: _Bool
- `mIsHideType`: _Bool

## BenefitItemObj

- `mAppearType`: u8
- `mCameraFrames`: u32
- `mEscapeLife`: s32
- `mEscapeType`: u8
- `mFlashCounter`: s32
- `mFlashCtrl`: FlashingCtrl *
- `mFlashHideTime`: s32
- `mFlashPeriod`: s32
- `mHasAppearCamera`: _Bool
- `mHasAppearPicture`: _Bool
- `mHideCounter`: s32
- `mHitSensorActor`: LiveActor *
- `mIsActive`: _Bool
- `mIsDemoPending`: _Bool
- `mIsDisabled`: _Bool
- `mIsInAir`: _Bool
- `mIsItemMoving`: _Bool
- `mIsOnRail`: _Bool
- `mIsThrownHigh`: _Bool
- `mIsWaitingAppear`: _Bool
- `mModelName`: const char *
- `mRailSpeed`: f32
- `mRotateFlags`: u8
- `mRotateY`: f32
- `mSensorInvalidStep`: s32
- `mShadowType`: u8

## BgmProhibitArea

- `mIsMuteSet`: _Bool
- `mIsUnmuteSet`: _Bool

## BigBubble

- `mAppearRadius`: f32
- `mBinderRadius`: f32
- `mDeformCoeff`: f32 (array)
- `mDeformSpeed`: f32 (array)
- `mDrawZ`: f32
- `mHost`: LiveActor *
- `mInterpolateRate`: f32
- `mIsAttached`: _Bool
- `mIsBroken`: _Bool
- `mIsExitLimitter`: _Bool
- `mIsHidden`: _Bool
- `mIsObstruct`: _Bool
- `mIsShrinkable`: _Bool
- `mMergeBubble`: BigBubble *
- `mMergeBubbles`: BigBubble * (array)
- `mMergeIndex`: s32
- `mMoveLimitter`: const BigBubbleMoveLimitter *
- `mPointAccel`: f32
- `mPushInvalidTime`: s32
- `mReduceVolumeTimer`: s32
- `mRider`: LiveActor *
- `mShapeType`: s32
- `mVolume`: f32

## CubeCameraArea

- `mCategoryMask`: s32
- `mZoneID`: u32

## BigBubbleGenerator

- `mAttachedBubble`: BigBubble *
- `mIsObstruct`: const _Bool
- `mLimitterID`: s32
- `mMaxSize`: f32
- `mMinSize`: f32
- `mShape`: s32
- `mSpawnTimer`: s32

## BigBubbleGoalArea

- `mMeterAmount`: f32
- `mMeterCapacity`: f32
- `mMeterDisplayAmount`: f32

## BigBubbleHolder

- `mIsMarioBinded`: _Bool
- `mNumBubbles`: s32
- `mNumLimitters`: s32

## BigBubbleMoveLimitter

- `mCylinderHeight`: f32
- `mCylinderRadius`: f32
- `mID`: s32
- `mLimitMarginType`: s32
- `mPlaneHeight`: f32
- `mPlaneWidth`: f32
- `mType`: s32

## BigFan

- `mIsTeresaGalaxy`: _Bool
- `mWindLength`: f32
- `mWindModel`: ModelObj *
- `mWindSpeed`: f32

## Birikyu

- `mCircleAngle`: f32
- `mHasRail`: _Bool
- `mHasShadow`: _Bool
- `mObjName`: const char *
- `mSpeed`: f32

## BirikyuWithFace

- `mIsFaceReversed`: _Bool

## BlackHole

- `mBlackHoleModel`: ModelObj *
- `mModelScale`: f32
- `mRadius`: f32

## ImageEffectArea

- `mEffectType`: s32

## BloomArea

- `mIntensity`: u8
- `mIntensity1`: s32
- `mIntensity2`: s32
- `mThreshold`: u8

## ImageEffectBase

- `mFadeLevel`: f32
- `mIsOn`: _Bool
- `mIsShown`: _Bool

## BloomEffect

- `mBloomIntensity`: u8
- `mIntensity1`: f32
- `mIntensity2`: f32
- `mThreshold`: u8

## BloomEffectSimple

- `mIntensity`: f32
- `mMaskFilter`: s32
- `mThreshold`: u8

## ChipBase

- `mAirBubble`: PartsModel *
- `mChipName`: const char *
- `mChipType`: s32
- `mFlashingCtrl`: FlashingCtrl *
- `mGroupID`: s32
- `mHost`: LiveActor *
- `mIsCalcShadow`: _Bool
- `mRailMover`: MapPartsRailMover *

## ChipGroup

- `mAlreadyDoneFlag`: s32
- `mChipType`: s32
- `mCounterTimer`: s32
- `mFlashFrame`: s32
- `mGotCount`: s32
- `mIsCounterVisible`: u8
- `mIsDeactivated`: u32
- `mIsListeningAppear`: u8
- `mStageSwitchArg`: s32
- `mTotalCount`: s32
- `mUIRange`: f32
- `mUIRangeFixed`: f32

## BlueStarCupsulePlanet

- `mIsStepUpdatable`: _Bool
- `mName`: const char *
- `mRailMover`: MapPartsRailMover *
- `mRotator`: MapPartsRotator *

## BombBird

- `mHeldBomb`: BombBirdBomb *
- `mHoldBomb`: _Bool
- `mSearchPlayer`: _Bool

## BombHei

- `mCarrySensor`: HitSensor *
- `mCountdown`: s32
- `mExplodeRadius`: f32
- `mExplosionIgnoreCollision`: _Bool
- `mFuseTime`: s32

## BombHeiLauncher

- `mBomb`: BombHei *
- `mCountdown`: s32
- `mIsActive`: _Bool

## BombTeresa

- `mDisableRespawning`: _Bool
- `mDriftPullSpeed`: f32
- `mDriftSpinSpeed`: f32
- `mDriftStep`: s32
- `mIsDriftRelease`: _Bool
- `mIsWanderAfterWait`: _Bool
- `mTongueRate`: f32

## BombTimerLayout

- `mDangerTransFrame`: u32
- `mFrame`: u32
- `mIsSuspend`: _Bool
- `mTimeLimit`: u32

## BossAccessor

- `mBoss`: LiveActor *

## BossBegoman

- `mBabyFollowerNum`: s32
- `mHead`: BossBegomanHead *
- `mHealth`: s32
- `mPursueSpeedRate`: f32
- `mSpikeFollowerNum`: s32

## BossBegomanHead

- `mAngle`: f32

## BossKameck

- `mKameckHolder`: KameckHolder *
- `mModelName`: const char *
- `mMoveRailNum`: s32
- `mObjArg7`: s32

## BossKameckBarrier

- `mModelName`: const char *

## BossKameckMoveRail

- `mRailNo`: s32
- `mStoppablePointNum`: s32

## BossStinkBug

- `mBombHolder`: BossStinkBugBombHolder *
- `mBombLauncher`: PartsModel *
- `mCameraRegisterId`: s32
- `mIsLegBound`: _Bool
- `mIsPoseUpdated`: _Bool
- `mLegFriction`: f32
- `mLegIKRate`: f32
- `mWingModel`: PartsModel *

## BreakableCage

- `mBreakModel`: ModelObj *
- `mDelayDeadActivate`: _Bool
- `mIgnoreGravity`: _Bool
- `mItemModel`: DummyDisplayModel *
- `mRotationSpeed`: f32

## BrightObj

- `mRadius`: f32

## BrightSun

- `mSun`: Sun *

## BrosButton

- `mIsSelectedMario`: _Bool

## NPCActor

- `mGoods0`: PartsModel *
- `mGoods1`: PartsModel *
- `mIsJetTurtleHit`: u8
- `mIsPointed`: u8
- `mIsPointingStart`: u8
- `mIsRailOnGround`: u8
- `mIsReaction`: u8
- `mIsReactionStart`: u8
- `mIsSpinAttacked`: u8
- `mIsSpinAttackedStart`: u8
- `mIsTrampled`: u8
- `mIsTrampledStart`: u8
- `mMoveAction`: const char *
- `mMoveTalkAction`: const char *
- `mMoveTalkBckRate`: f32
- `mMsgCtrl`: TalkMessageCtrl *
- `mPointingAction`: const char *
- `mPointingSeStep`: s32
- `mRailAccel`: f32
- `mRailPoseRate`: f32
- `mRailSpeed`: f32
- `mReactionAction`: const char *
- `mSpinAction`: const char *
- `mSwingReactDist`: f32
- `mTrampleAction`: const char *
- `mTrampleCoolDown`: s32
- `mUseReactionActions`: u8
- `mWasPointed`: u8
- `mWasReaction`: u8
- `mWasSpinAttacked`: u8
- `mWasTrampled`: u8

## Butler

- `mIsAstroDome`: _Bool
- `mIsPointed`: _Bool
- `mIsStarPieceLecture`: _Bool
- `mReplaceStep`: s32
- `mStarPieceHitCount`: s32

## ButlerMap

- `mObjArg0`: _Bool

## Butterfly

- `mColorIndex`: s32
- `mPerchTarget`: HitSensor *
- `mStarPieceAppeared`: _Bool

## Camera

- `mCameraMan`: CameraMan *
- `mIsLOfsErpOff`: _Bool
- `mVPan`: CameraHeightArrange *

## CameraAnim

- `mCurrentFrame`: f32
- `mIsPaused`: _Bool
- `mNrFrames`: u32
- `mShownFrame`: f32
- `mSpeed`: f32

## CameraBehind

- `mAngleX`: f32
- `mAngleY`: f32
- `mDist`: f32
- `mRate`: f32

## CameraBlackHole

- `mFovy`: f32
- `mRoll`: f32

## CameraCharmedTripodBoss

- `mAngleX`: f32
- `mAngleY`: f32
- `mJointId`: s32

## CameraCharmedVecReg

- `mAngleX`: f32
- `mAngleYMax`: f32
- `mAngleYMin`: f32
- `mCamAngleRate`: f32
- `mCamDistRatio`: f32
- `mDist`: f32
- `mName`: const char *

## CameraCharmedVecRegTower

- `mAngleX`: f32
- `mAngleYMax`: f32
- `mArrange`: _Bool
- `mDist`: f32
- `mName`: const char *

## CameraContext

- `mFarZ`: f32
- `mFovy`: f32
- `mNearZ`: f32

## CameraCover

- `mActor`: CaptureScreenActor *
- `mCoverFrames`: u32
- `mCoverFramesLeft`: s32
- `mIsCoverRequested`: _Bool

## CameraCubePlanet

- `mAngleX`: f32
- `mAngleY`: f32
- `mDist`: f32

## CameraDead

- `mCameraType`: s32
- `mDeadFrame`: s32
- `mDeadTime`: s32
- `mDist`: f32
- `mFovy`: f32

## CameraDirector

- `mCameraManEvent`: CameraManEvent *
- `mCameraManGame`: CameraManGame *
- `mCameraManSubjective`: CameraManSubjective *
- `mCameraTargetMtx`: CameraTargetMtx *
- `mChunkHolder`: CameraParamChunkHolder *
- `mCover`: CameraCover *
- `mEventNum`: u32
- `mHolder`: CameraHolder *
- `mIsCameraNG`: _Bool
- `mIsStartCameraActive`: _Bool
- `mIsStartSubjectiveCamera`: _Bool
- `mIsSubjectiveCalced`: _Bool
- `mIsSubjectiveCamera`: _Bool
- `mNearZ`: f32
- `mOnlyCamera`: OnlyCamera *
- `mRailHolder`: CameraRailHolder *
- `mRegisterHolder`: CameraRegisterHolder *
- `mRequestCameraLocalOffsetReset`: _Bool
- `mRequestCameraManReset`: _Bool
- `mShaker`: CameraShaker *
- `mStartCameraCreated`: _Bool
- `mStartTime`: s32
- `mSubjectiveFrame`: s32
- `mTargetObj`: CameraTargetObj *

## CameraFix

- `mTarget`: CameraTargetMtx *

## CameraFixedPoint

- `mCameraType`: u32

## CameraFixedThere

- `mCameraType`: u32
- `mIsFovyFixed`: _Bool
- `mRoll`: f32

## CameraFollow

- `mAngleXMin`: f32
- `mAngleXRange`: f32
- `mAngleXSpeed`: f32
- `mAngleYRoundSpeed`: f32
- `mAngleYSpeed`: f32
- `mAngleYTarget`: f32
- `mCheckWall`: _Bool
- `mDistMax`: f32
- `mDistMin`: f32
- `mIsRoundLeftValid`: _Bool
- `mIsRoundRightValid`: _Bool
- `mIsRounding`: _Bool

## CameraFooFighter

- `mAngleXRate`: f32
- `mAngleXRateMin`: f32
- `mCollideCount`: s32
- `mDistMax`: f32
- `mDistMin`: f32

## CameraFooFighterPlanet

- `mDistMax`: f32
- `mDistMin`: f32
- `mPitchMax`: f32

## CameraFrontAndBack

- `mDist`: f32
- `mMaxDipAngle`: f32
- `mRotateRate`: f32

## CameraGround

- `mAngleA`: f32
- `mAngleB`: f32
- `mDist`: f32
- `mIsNotInitialized`: _Bool

## CameraHeightArrange

- `mCamera`: Camera *
- `mCameraPosHeight`: f32
- `mCameraPosOffset`: f32
- `mCameraTargetPosHeight`: f32
- `mCameraWatchPosBaseHeight`: f32
- `mCameraWatchPosHeight`: f32
- `mCameraWatchPosOffset`: f32
- `mChaseTime`: s32
- `mDropDelay`: s32
- `mDropTime`: s32
- `mFocalScaleLower`: f32
- `mFocalScaleUpper`: f32
- `mGndInt`: s32
- `mIsCatchup`: _Bool
- `mIsJumping`: _Bool
- `mIsStartJump`: _Bool
- `mJumpState`: u32
- `mMaxRiseEaseTime`: s32
- `mOffsetScale`: f32
- `mOffsetScaleMax`: f32
- `mPosOffsetMinDropLag`: f32
- `mPosOffsetMinRiseLag`: f32
- `mRiseDelay`: s32
- `mRiseEaseTime`: s32
- `mRiseTime`: s32
- `mStateTime`: s32
- `mUpdateGlobalAxis`: _Bool
- `mVPanUse`: _Bool

## CameraHolder

- `mDefaultCameraIndex`: s32

## CameraInfo

- `mCanSkipAppearAnim`: _Bool
- `mIsActive`: _Bool
- `mStep`: s32

## CameraInnerCylinder

- `mAngleA`: f32
- `mAngleB`: f32
- `mDist`: f32
- `mIsNotInitialized`: _Bool
- `mSideDist`: f32

## CameraInwardSphere

- `mDistBase`: f32
- `mDistMax`: f32
- `mDistMin`: f32

## CameraInwardTower

- `mAngleMax`: f32
- `mAngleX`: f32
- `mAngleY`: f32
- `mDist`: f32
- `mIsResetting`: _Bool

## CameraMan

- `mDirector`: CameraDirector *
- `mIsActivated`: _Bool
- `mRequestLOfsReset`: _Bool

## CameraManEvent

- `mCamera`: Camera *
- `mChunkHolder`: CameraParamChunkHolder *
- `mHolder`: CameraHolder *
- `mRequestReset`: _Bool

## CameraManGame

- `mCamera`: Camera *
- `mChunkHolder`: CameraParamChunkHolder *
- `mHolder`: CameraHolder *
- `mInterpolateOffCounter`: s32
- `mIsStartPosActive`: _Bool
- `mRequestReset`: _Bool
- `mTypeState`: u32
- `mZoomedIn`: _Bool

## CameraManSubjective

- `mCamera`: CameraSubjective *

## CameraMedianPlanet

- `mAngleConstrainRate`: f32
- `mAngleMin`: f32
- `mDist`: f32
- `mDistMax`: f32
- `mDistMin`: f32
- `mPartialRate`: f32
- `mPeepAngle`: f32
- `mString`: const char *

## CameraMedianTower

- `mAngleA`: f32
- `mDist`: f32
- `mInterpolateTime`: s32
- `mIsResetting`: _Bool
- `mMaxFocusDist`: f32
- `mPartialRate`: f32
- `mString`: const char *

## CameraMtxRegParallel

- `mAngleX`: f32
- `mAngleY`: f32
- `mDist`: f32
- `mString`: const char *

## CameraObjParallel

- `mAngleX`: f32
- `mAngleY`: f32
- `mDist`: f32

## CameraParallel

- `mAngleX`: f32
- `mAngleY`: f32
- `mDist`: f32
- `mIsEnableValid`: _Bool
- `mIsRounding`: _Bool
- `mRoundAngle`: f32
- `mRoundTarget`: f32

## CameraParamChunkHolder

- `mCameraHolder`: CameraHolder *
- `mCameraVersion`: u32
- `mChunkCapacity`: s32
- `mIsSorted`: _Bool
- `mNrChunks`: s32

## CameraPointer

- `mAngleX`: f32
- `mAngleXRange`: f32
- `mAngleYMin`: f32
- `mAngleYRange`: f32
- `mBlendFriction`: f32
- `mCameraType`: s32
- `mDist`: f32
- `mFovy`: f32
- `mPointerDistance`: f32
- `mResetPointingPos`: _Bool
- `mSpeedBlendRate`: f32

## CameraRaceFollow

- `mAngleX`: f32
- `mDistMax`: f32
- `mDistMin`: f32
- `mIsForcePitch`: _Bool
- `mRoundAngleSpeed`: f32

## CameraRailDemo

- `mCalcType`: s32
- `mCoord`: f32
- `mDampRatio`: f32
- `mDemoTime`: s32
- `mDemoTimer`: s32

## CameraRailFollow

- `mMarginScale`: s32
- `mRailAccel`: f32
- `mRailCoord`: f32
- `mRailSpeed`: f32
- `mRailSpeedMax`: f32
- `mTargetMargin`: f32

## CameraRailWatch

- `mAngleX`: f32
- `mDirection`: s32
- `mDist`: f32
- `mRailCoordOffset`: f32
- `mSetDirection`: s32

## CameraRegisterHolder

- `mDummyFloat`: f32

## CameraSlide

- `mAngleA`: f32
- `mDist`: f32

## CameraSpiral

- `mAngleEnd`: f32
- `mAngleStart`: f32
- `mDistEnd`: f32
- `mDistStart`: f32
- `mEaseType`: s32
- `mEndTime`: s32
- `mOffsetYEnd`: f32
- `mOffsetYStart`: f32
- `mStartTime`: s32
- `mTimer`: s32

## CameraTalk

- `mDist`: f32
- `mHeight`: f32

## CameraTargetActor

- `mActor`: const LiveActor *
- `mCameraArea`: CubeCameraArea *

## CameraTargetMtx

- `mCameraArea`: CubeCameraArea *
- `mInvalidLastMove`: _Bool

## CameraTargetPlayer

- `mActor`: const MarioActor *
- `mCameraArea`: CubeCameraArea *
- `mIsPlayerMoving`: _Bool
- `mPlayerMovementTimer`: s32

## CameraTower

- `mAngleX`: f32
- `mAngleYRoundSpeed`: f32
- `mAngleYSpeed`: f32
- `mDist`: f32
- `mIsRoundLeftValid`: _Bool
- `mIsRoundRightValid`: _Bool
- `mIsRounding`: _Bool
- `mRoundIntervalNum`: s32

## CameraTowerPos

- `mAngleX`: f32
- `mAngleXRate`: f32
- `mAngleYMaxSpeed`: f32
- `mDist`: f32

## CameraTripodBoss

- `mAngleY`: f32

## CameraTripodBossJoint

- `mAngleX`: f32
- `mAngleY`: f32
- `mDist`: f32
- `mIsRounding`: _Bool
- `mJointID`: s32
- `mRoundAngle`: f32
- `mRoundTarget`: f32

## CameraTripodPlanet

- `mAngleX`: f32
- `mAngleY`: f32
- `mDist`: f32

## CameraTrundle

- `mAngleX`: f32
- `mAngleY`: f32
- `mDist`: f32
- `mDotMax`: f32

## CameraTwistedPassage

- `mDistMax`: f32
- `mDistMin`: f32
- `mIsReversed`: _Bool

## CameraWaterFollow

- `mCollideCount`: s32
- `mDistMax`: f32
- `mDistMin`: f32
- `mIsRounding`: _Bool
- `mRoundingFrame`: s32
- `mSideBlendRate`: f32
- `mSideBlendRateMin`: f32

## CameraWaterPlanet

- `mAngleX`: f32
- `mDistMax`: f32
- `mDistMin`: f32

## CameraWaterPlanetBoss

- `mAngleXMax`: f32
- `mAngleXMin`: f32
- `mCollideCount`: s32
- `mDistMax`: f32
- `mDistMin`: f32
- `mDotFrontMax`: f32
- `mDotFrontMin`: f32
- `mIsBlendUpEnabled`: _Bool
- `mIsRounding`: _Bool
- `mRoundingFrame`: s32
- `mSideBlendRate`: f32
- `mSideBlendRateMin`: f32

## CameraWonderPlanet

- `mAngleX`: f32
- `mDistMax`: f32
- `mDistMin`: f32
- `mIsRotating`: _Bool
- `mRotateFrame`: s32

## Candlestand

- `mHasItemAppear`: _Bool
- `mItem`: s32

## CaptureScreenActor

- `mCameraName`: const char *

## CaptureScreenDirector

- `mIsCaptureRequested`: _Bool
- `mRequestedTiming`: const char *
- `mTimingType`: const char *

## Caretaker

- `mBombTimerLayout`: BombTimerLayout *
- `mTidyTimeLimit`: s32

## CenterScreenBlur

- `mAlpha`: u8
- `mBlendRate`: f32
- `mFadeIn`: s32
- `mFadeOut`: s32
- `mOffset`: f32
- `mTime`: s32

## ChangeBgmCube

- `mIsInside`: u8

## ChipCounter

- `mCollectCounter`: CollectCounter *
- `mCount`: s32
- `mGroupId`: s32
- `mShowRate`: f32
- `mType`: s32

## ChipHolder

- `mChipCounter`: ChipCounter *
- `mChipType`: s32
- `mNumChipGroups`: s32

## ChooChooTrain

- `mRailSpeed`: f32
- `mWhistleTimer`: s32

## CinemaFrame

- `mStartAt`: f32

## CoinGroup

- `mCoinCount`: u32
- `mIsPurpleCoinGroup`: _Bool
- `mTimeLimit`: s32

## CircleCoinGroup

- `mCoinRadius`: f32

## ClipAreaDrop

- `mBaseSize`: f32

## ClipAreaDropLaser

- `mDrawCount`: int
- `mNumPointsToDraw`: f32
- `mPointIndexToSkipDraw`: int
- `mPoints`: Vec3f (array)
- `mSpeed`: f32

## ClipAreaHolder

- `mIsActive`: _Bool

## ClipAreaMovable

- `mMapPartsRailMover`: MapPartsRailMover *
- `mMapPartsRotator`: MapPartsRotator *

## ClipFieldFillDraw

- `mFillBlendOp`: u8
- `mFillDstFactor`: u8
- `mFillInsideVolume`: _Bool
- `mFillSrcFactor`: u8

## GeneralMapParts

- `mAppearController`: MapPartsAppearController *
- `mGuideDrawer`: MapPartsRailGuideDrawer *
- `mIsCameraEnded`: u8
- `mIsPaused`: u8
- `mMoveConditionType`: s32
- `mRailMover`: MapPartsRailMover *
- `mRailPosture`: MapPartsRailPosture *
- `mRailRotator`: MapPartsRailRotator *
- `mRotator`: MapPartsRotator *
- `mShadowType`: s32
- `mSignMotionType`: s32

## ClippingActorHolder

- `mActorNum`: s32
- `mPlanetNum`: s32
- `mTableMask`: u32

## ClippingDirector

- `mActorHolder`: ClippingActorHolder *
- `mGroupHolder`: ClippingGroupHolder *
- `mJudge`: ClippingJudge *

## ClippingGroupHolder

- `mMaxGroups`: s32
- `mNumGroups`: s32

## ClippingInfoGroup

- `mCount`: s32
- `mIsClipped`: _Bool

## CocoNut

- `mContinueRolling`: _Bool
- `mFallSpeed`: f32
- `mIsBindOff`: _Bool
- `mIsGroupClipping`: _Bool
- `mIsKicked`: _Bool
- `mIsOnMoveArea`: _Bool
- `mMoveSpeed`: f32
- `mOffGroundStep`: s32
- `mRadius`: f32
- `mRespawnWhenOutOfView`: _Bool
- `mSphericalShadow`: _Bool

## CocoNutBall

- `mFreezeStep`: s32
- `mHeightMax`: f32
- `mHeightMin`: f32
- `mHost`: LiveActor *
- `mIsHeightLimited`: _Bool
- `mIsRallyBgm`: _Bool
- `mIsThrowRight`: _Bool
- `mKillDistance`: f32
- `mRallyNum`: s32

## CocoSambo

- `mHead`: CocoSamboHead *
- `mPlayerSearchDistance`: f32

## CoconutTreeLeaf

- `mGroup`: const CoconutTreeLeafGroup *
- `mWindStartStep`: s32
- `mWindStep`: s32

## CoconutTreeLeafGroup

- `mDistanceToPlayer`: f32
- `mNumLeaves`: s32

## Coin

- `mAirBubble`: PartsModel *
- `mCannotTime`: s32
- `mFlashCtrl`: FlashingCtrl *
- `mIsCalcShadowAlways`: _Bool
- `mIsInWater`: _Bool
- `mIsNeedBubble`: _Bool
- `mIsPoseFromRotation`: _Bool
- `mIsPurpleCoin`: _Bool
- `mLife`: int

## CoinCounter

- `mCoinDisplayNum`: s32
- `mCoinNum`: s32
- `mInvalidCountUpFrame`: s32
- `mIsForceAppear`: _Bool

## CoinReplica

- `mCoin`: Coin *

## CoinRotater

- `mHiSpeedRotateY`: f32
- `mRotateY`: f32
- `mWaterRotateY`: f32

## CoinSpot

- `mCoinCount`: s32
- `mIsInTornado`: _Bool
- `mSpawnTimer`: s32
- `mSpawnedCount`: s32

## CollapsePlane

- `mCollapseStep`: s32
- `mCollapseTime`: s32

## CollectTico

- `mIsDemoCast`: u8
- `mTicoNum`: s32

## CollisionArea

- `mHitRadius`: s32
- `mInsideDepth`: f32
- `mIsTeresaPass`: _Bool
- `mIsValid`: _Bool
- `mOutsideAxisCount`: u32
- `mPolygon`: AreaPolygon *
- `mPushType`: s32
- `mSurfaceMask`: s32
- `mTeresaType`: s32

## CollisionCategorizedKeeper

- `mCategory`: s32
- `mHitInfoRoom`: s32
- `mNumHits`: s32
- `mPartsCount`: s32
- `mUpdateZoneBounds`: _Bool
- `mZonesInitialized`: _Bool

## CollisionDirector

- `mCategoryKeeper`: CollisionCategorizedKeeper * (array)

## CollisionShadow

- `mCaptureOn`: u8
- `mCheckOffset`: f32
- `mDrawFlags`: u16
- `mDrawOffset`: f32
- `mEdgePolygonCount`: u32
- `mLength`: f32
- `mListCapacity`: u32
- `mMode`: u8
- `mPolygonCount`: u32
- `mProjJumps`: u32
- `mRadius`: f32
- `mSnapDirection`: _Bool
- `mUpdate`: u8
- `mVertexCount`: u32
- `mVertsPerPolygon`: u16
- `mViewDone`: u8
- `mVolumeHeight`: f32
- `mVolumeWidth`: f32

## CometEventExecutorTimeLimit

- `mTimeLimit`: u32
- `mTimeLimitLayout`: TimeLimitLayout *

## CometRetryButton

- `mPlayerMissLeft`: PlayerMissLeft *

## CounterLayoutController

- `mCoinCounter`: CoinCounter *
- `mHPMeter`: MarioMeter *
- `mIsTicoEatRequested`: _Bool
- `mPlayerLeft`: PlayerLeft *
- `mPlayerNotMovingFrame`: s32
- `mStarCounter`: StarCounter *
- `mStarPieceCounter`: StarPieceCounter *

## Creeper

- `mBendRate`: f32
- `mBendRateBee`: f32
- `mCoord`: f32
- `mFlowerModel`: PartsModel *
- `mHangSpeed`: f32
- `mJumpSpeedFront`: f32
- `mJumpSpeedUp`: f32
- `mLeafModel`: PartsModel *
- `mNumPoints`: s32
- `mRider`: LiveActor *

## CrystalCage

- `mBreakObj`: ModelObj *
- `mBreakOnlyIfShown`: _Bool
- `mDisplayModel`: DummyDisplayModel *
- `mHasBinding`: _Bool
- `mHitInvalidTime`: s32
- `mHitPoint`: s32
- `mPlayRiddleSFX`: _Bool
- `mPowerStarId`: s32
- `mVanishOnBreak`: _Bool

## CrystalCageMoving

- `mIsCoreShown`: u8
- `mTicoModel`: ModelObj *

## CrystalSwitch

- `mIsHit`: u8
- `mRotateSpeed`: f32

## DarknessRoomDoor

- `mIsNormalDraw`: _Bool

## DashRing

- `mActiveTime`: s32
- `mBoostTime`: s32
- `mDoTurn`: _Bool
- `mMaxDuration`: s32
- `mRevivalTime`: s32
- `mSpeedScale`: f32

## DeadLeaves

- `mItemType`: s32

## DemoCastGroup

- `mGroup`: LiveActorGroup *

## DemoCastSubGroup

- `mExecutor`: DemoExecutor *

## DemoDirector

- `mCastGroupHolder`: DemoCastGroupHolder *
- `mCastSubGroupHolder`: DemoCastGroupHolder *
- `mDemoName`: const char *
- `mDemoStarter`: NameObj *
- `mExecutor`: DemoExecutor *
- `mIsActive`: _Bool
- `mIsWaitingCamera`: _Bool
- `mUseCinemaFrame`: _Bool

## DemoExecutor

- `mDemoName`: const char *
- `mDemoStarter`: NameObj *
- `mSheetName`: const char *

## DemoKoopaJrShip

- `mAnimCameraIndex`: s32
- `mKoopaJrObj`: KoopaJr *

## DemoRabbit

- `mAirTimer`: s32

## DepthOfFieldArea

- `mIsPlayerSync`: _Bool

## DepthOfFieldBlur

- `mBlurMaxDist`: s32
- `mBlurMinDist`: s32
- `mIntensity`: f32

## DesertLandMoveSwitch

- `mIsHipDropped`: _Bool
- `mIsOnPlayer`: _Bool
- `mObjName`: const char *
- `mTimer`: s32
- `mTimerLead`: s32
- `mWasOnPlayer`: _Bool

## DesertMovingLand

- `mMoveDownTime`: s32
- `mMoveUpTime`: s32

## DharmaSambo

- `mHideTimer`: s32
- `mHitSensor`: HitSensor *
- `mPartsCount`: s32

## DharmaSamboParts

- `mHitDistance`: f32
- `mHost`: DharmaSambo *
- `mIndex`: s32
- `mIsHead`: _Bool
- `mOriginalIndex`: s32

## DinoPackun

- `mAimTailRate`: f32
- `mAimTailRateSpeed`: f32
- `mBall`: DinoPackunBall *
- `mCamTargetMtx`: CameraTargetMtx *
- `mCameraRegisterId`: s32
- `mDamageCameraState`: s32
- `mDemoPos`: DinoPackunDemoPosition *
- `mFootPrint`: FootPrint *
- `mIsPreCalcAnim`: _Bool
- `mShell`: DinoPackunEggShell *
- `mShellBreakModel`: PartsModel *
- `mTailBall`: PartsModel *

## DinoPackunTailNode

- `mIsLocked`: _Bool
- `mKeepBendPower`: f32
- `mLinkLength`: f32
- `mNextNode`: LiveActor *
- `mParent`: DinoPackun *
- `mPrevNode`: LiveActor *

## DinoPackunBall

- `mDragSoundStep`: s32
- `mIsBlackDrag`: _Bool
- `mIsOnFire`: _Bool
- `mWeakSensor`: HitSensor *

## DinoPackunEggShell

- `mSensor`: HitSensor *

## DinoPackunTailPart

- `mRadius`: f32

## Dodoryu

- `mBank`: DodoryuBank *
- `mClosedAreaObj`: AreaObj *
- `mDemoCameraTarget`: CameraTargetMtx *
- `mHill`: DodoryuHill *
- `mIsRumbling`: _Bool
- `mIsSwitchOn`: _Bool
- `mLeadHill`: DodoryuLeadHill *
- `mRabbit`: DodoryuRabbit *
- `mSpinOutCameraTarget`: CameraTargetMtx *
- `mStateIndex`: u32

## DodoryuStateBase

- `mHost`: Dodoryu *

## MogucchiHill

- `mAppearDistance`: f32
- `mAppearDistanceMax`: f32
- `mAppearNum`: s32
- `mAutoEffectName`: const char *
- `mHasLightCtrl`: _Bool
- `mHasReserveMtx`: _Bool
- `mHost`: LiveActor *
- `mJointName`: const char *
- `mModelNum`: u32
- `mPieceIndex`: s32
- `mPieceNum`: s32
- `mPieces`: MogucchiHillPiece *
- `mSizeAnimState`: s32
- `mSizeStep`: s32

## DodoryuHill

- `mHost`: Dodoryu *

## DodoryuLeadHill

- `mSpinAngle`: f32

## DodoryuRabbit

- `mHost`: Dodoryu *
- `mIsDisplayMessage`: _Bool
- `mRailCoord`: f32
- `mTalkCtrl`: TalkMessageCtrl *
- `mTalkNode`: s32
- `mTalkSwitchStep`: s32

## DodoryuStateLv1

- `mAppearStep`: s32
- `mHideStep`: s32
- `mIsSpinCountered`: _Bool
- `mIsValidPop`: _Bool
- `mPopAccel`: f32
- `mPopSpeed`: f32
- `mRailSpeed`: f32
- `mRailSpeedMax`: f32

## DodoryuStateLv2

- `mChaseAppearEndCounter`: s32
- `mChaseAppearStep`: s32
- `mChaseMoreCounter`: s32
- `mEscapeStepMaxLeft`: s32
- `mEscapeStepMinLeft`: s32
- `mFindPosStep`: s32
- `mIsChaseMoreBrake`: _Bool
- `mIsPlayerStaggering`: _Bool
- `mReadyAppearStep`: s32

## DodoryuStateWait

- `mNextStateCounter`: s32

## Dossun

- `mFallingTime`: s32
- `mHasShadow`: _Bool
- `mHoldTime`: s32
- `mMovementDist`: f32
- `mRisenWaitTime`: s32
- `mShadowType`: s32

## DragonHeadFlower

- `mParabolaAccel`: f32
- `mParabolaDistance`: f32
- `mParabolaProgress`: f32
- `mParabolaSteps`: u32
- `mParabolaVel`: f32
- `mPlayerSensor`: HitSensor *

## DriftWood

- `mWaveSoundTimer`: s32

## DummyDisplayModel

- `mBrkFrame`: s32
- `mIsInCrystal`: _Bool
- `mItemType`: s32

## EarthenPipe

- `mHorizExitForce`: f32
- `mHostActor`: LiveActor *
- `mIsIgnoreGravity`: _Bool
- `mIsRabbitWait`: _Bool
- `mIsWaterFlowing`: _Bool
- `mMusicChangeIdx`: s32
- `mMusicState`: s32
- `mPipeLength`: f32
- `mPipeMode`: s32
- `mPipeStreamModel`: PartsModel *
- `mTargetPipe`: EarthenPipe *
- `mVertExitForce`: f32

## EarthenPipeMediator

- `mNumEntries`: s32
- `mPipeEntryRoom`: s32

## ElectricBall

- `mAngularSpeed`: f32
- `mRadius`: f32
- `mRailMover`: MapPartsRailMover *

## ElectricPressureBullet

- `mSpeed`: f32

## ElectricRail

- `mEaseIn`: f32
- `mIsCalcGravity`: _Bool
- `mPointCount`: s32
- `mPoints`: ElectricRailPoint *
- `mRailHeight`: s32
- `mSeparatorCount`: s32
- `mShadowDrawer`: ElectricRailShadowDrawer *

## ElectricRailHolder

- `mIsActive`: _Bool

## ElectricRailMoving

- `mAlpha`: u8
- `mCoord`: f32
- `mMovementSpeed`: f32
- `mMovingPoints`: ElectricRailMovingPoint *
- `mPointNum`: s32
- `mRailPosNum`: s32
- `mRailWidth`: f32
- `mSegmentLength`: f32
- `mSegmentNum`: s32
- `mStackHeight`: s32

## ElectricRailMovingPoint

- `mHasShadow`: _Bool
- `mIsElectric`: _Bool

## ElectricRailPoint

- `mHasShadow`: _Bool
- `mIsActive`: _Bool

## ShadowDrawer

- `mAnchorJumps`: u32
- `mHasAnchor`: _Bool

## ShadowVolumeDrawer

- `mEndDrawShapeOffset`: f32
- `mIsCutDropShadow`: _Bool
- `mStartDrawShapeOffset`: f32

## ElectricRailShadowDrawer

- `mHost`: const LiveActor *

## ErrorMessageWindow

- `mMessageId`: const char *

## EventDirector

- `mPowerStarHolder`: PowerStarHolder *

## EventSequencer

- `mNumSequences`: u32
- `mSequenceFrame`: u16

## ExterminationChecker

- `mGroup`: LiveActorGroup *
- `mIsKeySwitch`: u8
- `mIsPowerStar`: u8
- `mKeySwitch`: KeySwitch *

## EyeBeamer

- `mBeamBloom`: ModelObj *
- `mBeamLength`: f32
- `mBeamMdl`: ModelObj *
- `mBeamVolumeDrawer`: VolumeModelDrawer *
- `mIsInMercatorCube`: _Bool
- `mRailMover`: MapPartsRailMover *
- `mWaterSurfaceHeight`: f32

## FallDownBridge

- `mAngle`: f32
- `mAngleSpeed`: f32
- `mIsFallen`: _Bool
- `mMaxAngle`: f32
- `mMinAngle`: f32

## FallOutFieldDraw

- `mFillInsideVolume`: _Bool

## FileSelectButton

- `mGuidancePlain`: GalaxyMapGalaxyPlain *

## FileSelectCameraController

- `mFovy`: f32
- `mPrevFovy`: f32

## FileSelectEffect

- `mEffectFrame`: f32

## FileSelectInfo

- `mIsSelectedMario`: _Bool
- `mIsSelectedMarioPrev`: _Bool
- `mIsViewCompleteEnding`: _Bool
- `mIsViewNormalEnding`: _Bool
- `mMissNum`: s32
- `mNameBufferSize`: s32
- `mNumber`: s32
- `mStarNum`: s32
- `mStarPieceNum`: s32

## FileSelectItem

- `mFaceParts`: MiiFaceParts *
- `mFileNo`: s32
- `mIsComplete`: u8
- `mIsDragging`: u8
- `mIsInvalidRotate`: _Bool
- `mIsInvalidateSelect`: _Bool
- `mIsMiiBroken`: u8
- `mIsNewFile`: _Bool
- `mIsPointed`: u8
- `mIsPointerOut`: u8
- `mNumber`: FileSelectNumber *
- `mPlanetMapObj`: PartsModel *
- `mRotateSpeed`: f32
- `mTurnStep`: s32
- `mTurnTime`: s32

## FileSelectNumber

- `mNumber`: s32

## FileSelectSky

- `mAngleX`: f32
- `mAngleY`: f32

## FileSelector

- `mBackButton`: BackButton *
- `mBgmState`: s32
- `mBrosButton`: BrosButton *
- `mCameraController`: FileSelectCameraController *
- `mDecidedItem`: FileSelectItem *
- `mFileInfo`: FileSelectInfo *
- `mInfoMessage`: InformationMessage *
- `mInitItem`: unsigned int
- `mInitStep`: unsigned int
- `mIsAskingQuit`: _Bool
- `mIsMiiSelectStartFirst`: _Bool
- `mIsQuitFocusWanted`: _Bool
- `mManual`: Manual2P *
- `mMiiConfirmIcon`: MiiConfirmIcon *
- `mMiiSelect`: MiiSelect *
- `mOperationButton`: FileSelectButton *
- `mPointCandidate`: FileSelectItem *
- `mPointedItem`: FileSelectItem *
- `mQuitButton`: NebulaQuitButton *
- `mQuitFrames`: s32
- `mQuitWindow`: SysInfoWindow *
- `mSelectEffect`: FileSelectEffect *
- `mSelectedItem`: FileSelectItem *
- `mSky`: FileSelectSky *
- `mSysInfoWindow`: SysInfoWindow *
- `mSysInfoWindowMini`: SysInfoWindow *

## FireBall

- `mHost`: LiveActor *

## FireBar

- `mFireBallCount`: s32
- `mFireBarSpeed`: f32
- `mStickCount`: s32
- `mStickDistance`: f32

## FireBarBall

- `mFireBarParent`: LiveActor *
- `mIsEffectNear`: _Bool

## FireBubble

- `mActFrame`: s32
- `mChaseCounter`: s32
- `mIsValidInfo`: _Bool

## FireMarioBall

- `mBindTimer`: u16

## PressureBase

- `mBallSpeed`: f32
- `mBaseAngle`: f32
- `mGroup`: MsgSharedGroup *
- `mIsShortShot`: _Bool
- `mMessenger`: PressureMessenger *
- `mRelaxAngle`: f32
- `mShotType`: s32
- `mWaitTime`: s32

## FirePressure

- `mIsDisableCollision`: _Bool

## FirePressureBullet

- `mCanCrash`: _Bool
- `mIsHoming`: _Bool
- `mSpeed`: f32

## FirePressureRadiate

- `mCannonRotation`: f32
- `mGroup`: MsgSharedGroup *
- `mIsGroupLeader`: _Bool
- `mRadiateLength`: f32
- `mRadiateMaxLength`: f32
- `mRadiateStep`: s32
- `mShootTime`: s32
- `mWaitTime`: s32

## Fish

- `mCanEscape`: _Bool
- `mDistanceToApproach`: f32
- `mFishType`: int
- `mGroup`: FishGroup *
- `mStepToMove`: s32
- `mTimeToSearchPlayer`: s32

## FishGroup

- `mFishNum`: s32
- `mSpeed`: f32

## Flag

- `mAlpha`: u8
- `mAlphaDistanceFar`: f32
- `mAlphaDistanceNear`: f32
- `mDisableLighting`: _Bool
- `mDisableSound`: _Bool
- `mFrictionRateMax`: f32
- `mFrictionRateMin`: f32
- `mGravityScale`: f32
- `mIsVerticalFlag`: _Bool
- `mLightColorMin`: f32
- `mNumPointsU`: s32
- `mNumPointsV`: s32
- `mObjName`: const char *
- `mPointIntervalU`: f32
- `mPointIntervalV`: f32
- `mRandomWindAccelMax`: f32
- `mRandomWindAccelMin`: f32
- `mSeStep`: s32
- `mStickLength`: f32
- `mUseAlpha`: _Bool
- `mWaitTime`: s32
- `mWavePhase`: f32
- `mWindAccelMin`: f32
- `mWindAccelRate`: f32

## FlameGun

- `mState`: s32

## FlashingCtrl

- `mActor`: LiveActor *
- `mFlashStartTime`: s32
- `mIsEnded`: _Bool
- `mOverrideInterval`: s32
- `mTimer`: s32
- `mToggleDraw`: _Bool

## FlexibleSphere

- `mEnabled`: _Bool
- `mIsGrowing`: _Bool
- `mMaxScale`: f32
- `mMinScale`: f32
- `mScaleRate`: f32
- `mShakeTimer`: s32
- `mTimer`: s32
- `mVanishOnShrinkEnd`: _Bool

## FlipPanel

- `mIsFlipped`: u8
- `mIsPlayerOn`: _Bool
- `mIsReverse`: _Bool
- `mJumpCheckCount`: s32

## FlipPanelObserver

- `mDemoDelay`: s32
- `mFlippedNum`: s32
- `mGroup`: MsgSharedGroup *
- `mIsHeavensDoor`: u8
- `mPowerStarId`: s32

## MapPartsFunction

- `mHost`: LiveActor *
- `mIsActive`: _Bool

## FloaterFloatingForce

- `mMaxSinkDepth`: f32
- `mObjName`: const char *

## FloaterFloatingForceTypeNormal

- `mRiseAccel`: f32
- `mSinkAccel`: f32
- `mSinkDepth`: f32
- `mSinkSpeed`: f32

## FloaterFloatingForceTypeSpring

- `mRideSinkSpeed`: f32

## Fluff

- `mCameraTargetMtx`: CameraTargetMtx *
- `mModel`: ModelObj *
- `mRider`: LiveActor *
- `mSpinsRemaining`: s32
- `mWindDistance`: f32
- `mWindSpinTimer`: s32

## FluffWind

- `mNumEffects`: s32

## FluffWindEffect

- `mEffectName`: const char *
- `mTimer`: s32

## FollowCollisionArea

- `mHitRadius`: s32
- `mInsideDepth`: f32
- `mIsTeresaPass`: _Bool
- `mOffset`: f32
- `mOutsideAxisCount`: u32
- `mPolygon`: AreaPolygon *
- `mPushType`: s32
- `mTeresaType`: s32

## FootPrint

- `mHeight`: f32
- `mIsFading`: _Bool
- `mMinPrintDistance`: f32
- `mPrintMaxNum`: s32
- `mPrintNum`: s32
- `mRotatingPrintIndex`: s32
- `mSideOffset`: f32
- `mWidth`: f32

## Fountain

- `mFountainName`: const char *

## FountainBig

- `mSpoutTimer`: s32

## FullnessMeter

- `mHost`: LiveActor *
- `mNumber`: s32
- `mNumberMax`: s32
- `mTargetCounter`: SimpleLayout *

## PlanetMap

- `mBloomModel`: ModelObj *
- `mModelName`: const char *

## GCapture

- `mBindActor`: LiveActor *
- `mCaptureRibbon`: GCaptureRibbon *
- `mEffectBlend`: f32
- `mGuidanceTime`: s32
- `mRibbonRate`: f32
- `mTargetDist`: f32
- `mTargetKeepTime`: s32
- `mTargetMtx`: CameraTargetMtx *
- `mTractPower`: f32

## GCaptureRibbon

- `mIsLineSet`: _Bool

## GCaptureTarget

- `mFarAwayColor`: _Bool
- `mPointableRange`: f32
- `mRailMover`: MapPartsRailMover *
- `mReleaseDistance`: f32
- `mStarAnimSpeed`: f32

## GalaxyConfirmLayout

- `mBackButton`: BackButton *

## GalaxyMap

- `mGalaxyDetail`: GalaxyMapGalaxyDetail *
- `mGalaxyName`: const char *
- `mGalaxyPlain`: GalaxyMapGalaxyPlain *
- `mIsFocusEnabled`: _Bool
- `mIsPointingNew`: _Bool
- `mMarioIcon1`: GalaxyMapMarioIcon *
- `mMarioIcon2`: GalaxyMapMarioIcon *
- `mPointingIcon`: GalaxyMapIcon *
- `mTitle`: GalaxyMapTitle *

## GalaxyMapCometIcon

- `mGalaxyName`: const char *
- `mHost`: LayoutActor *
- `mPaneName`: const char *

## GalaxyMapController

- `mBackButton`: BackButton *
- `mMap`: GalaxyMap *
- `mMapBackground`: GalaxyMapBackground *
- `mMapSelectButton`: GalaxyMapSelectButton *
- `mMode`: s32
- `mPowerStarList`: PowerStarList *
- `mWipe`: WipeFade *
- `mWipeFrame`: s32

## GalaxyMapDomeIcon

- `mDomeNo`: int
- `mHost`: LayoutActor *
- `mMode`: u8
- `mNamePaneName`: const char *
- `mNamePlate`: GalaxyNamePlate *
- `mPaneName`: const char *

## GalaxyMapGalaxyDetail

- `mAButtonIcon`: IconAButton *

## GalaxyMapGalaxyPlain

- `mHost`: const LayoutActor *
- `mIsShow`: _Bool
- `mNamePlate`: GalaxyNamePlate *
- `mPaneName`: const char *

## GalaxyMapIcon

- `mGalaxyName`: const char *
- `mHost`: LayoutActor *
- `mMode`: u8
- `mPaneName`: const char *

## GalaxyMapMarioIcon

- `mTargetLayout`: LayoutActor *
- `mTargetPaneName`: const char *

## GalaxyMapTicoIcon

- `mExclamationGalaxyIndex`: int
- `mGalaxyName`: const char *
- `mHost`: LayoutActor *
- `mMode`: u8
- `mPaneName`: char *

## GalaxyMapTitle

- `mMessageID`: const char *

## GalaxyNamePlate

- `mGalaxyName`: const char *
- `mIsAppearNow`: _Bool
- `mIsShownThisFrame`: _Bool
- `mPlayNewSe`: _Bool
- `mShowBalloonNozzle`: _Bool
- `mStateFrame`: s32

## GalaxySelectInfo

- `mIsOnPause`: _Bool
- `mIsShownThisFrame`: _Bool
- `mShownGalaxyName`: const char *

## GamePauseSequence

- `mPauseMenu`: PauseMenu *

## GameSceneLayoutHolder

- `mAchievementBoard`: NebulaAchievementBoard *
- `mAchievementToast`: NebulaAchievementToast *
- `mCameraInfo`: CameraInfo *
- `mCounterLayoutCtrl`: CounterLayoutController *
- `mInformationMessage`: InformationMessage *
- `mIsActiveDefaultGameLayout`: _Bool
- `mIsStatusInvalidatePending`: _Bool
- `mMarioMeter`: MarioMeter *
- `mMarioSubMeter`: MarioSubMeter *
- `mMissLayout`: MissLayout *
- `mModText`: NebulaModText *
- `mNoteCounter`: NoteCounter *
- `mOneUpBoard`: OneUpBoard *
- `mPurpleCoinCounter`: PurpleCoinCounter *
- `mYesNoLayout`: YesNoLayout *

## GameStageClearSequence

- `mEffectLayout`: SimpleEffectLayout *

## GameSystemRestartProcess

- `mResetOperation`: s32

## Gesso

- `mClipStep`: s32
- `mIsHighSpeedMode`: _Bool
- `mIsMarioLeft`: _Bool

## GhostPlayer

- `mAnimTrackWeights`: f32 (array)
- `mAppearStarPieceCooldown`: u16
- `mCameraTargetMtx`: CameraTargetMtx *
- `mGhostNames`: const char *
- `mHasJetTurtle`: _Bool
- `mIsDemoCameraActive`: _Bool
- `mIsHidden`: _Bool
- `mJetTurtleShadow`: JetTurtleShadow *
- `mKilledByStar`: _Bool
- `mPlayerTrampleCooldown`: u16
- `mPowerStarTarget`: LiveActor *
- `mRaceManagerLayout`: RaceManagerLayout *
- `mSkippingLeadIn`: _Bool
- `mWaitingToStart`: _Bool

## JetTurtle

- `mHasReviveLight`: _Bool
- `mHomingTarget`: HitSensor *
- `mIsAppeared`: _Bool
- `mIsResetRequested`: _Bool
- `mIsRevive`: _Bool
- `mShellType`: u16
- `mSpeedType`: u16
- `mTakenSensor`: HitSensor *
- `mThrowSpeedBonus`: f32

## GrandStarReturnDemoStarter

- `mPowerStar`: ModelObj *
- `mStageResultInformer`: StageResultInformer *

## GravityLight

- `mLightCylinder`: LightCylinder *
- `mMapPartsRailMover`: MapPartsRailMover *
- `mMapPartsRotator`: MapPartsRotator *
- `mRadius`: f32

## GravityLightRoad

- `mHost`: LiveActor *
- `mRadius`: f32
- `mSpeed`: f32

## GreenCaterpillarBig

- `mBodyArrayLength`: s32
- `mCurBodyParts`: s32
- `mIsEnteringHole`: u8
- `mIsLeavingApple`: u8
- `mRailPointNo`: s32

## GreenCaterpillarBigBody

- `mCaterpillar`: LiveActor *

## GroundChecker

- `mFriction`: f32

## GroupCheckManager

- `mGroups`: GroupChecker * (array)

## GroupSwitchWatcher

- `mActionType`: s32
- `mLogicType`: s32
- `mNumSwitches`: s32

## Halo

- `mDistance`: f32

## HammerHeadPackun

- `mImmunityTime`: s32
- `mLeafModel`: PartsModel *
- `mNumbTime`: s32

## Hanachan

- `mIsChasePlayer`: _Bool

## HanachanParts

- `mActionStartStep`: s32
- `mHost`: Hanachan *
- `mIsLanded`: _Bool
- `mPartsIndex`: s32

## HazeCube

- `mDistance`: f32

## HeatHazeDirector

- `mEffect`: HeatHazeEffect *

## HeatHazeEffect

- `mHazeDistance`: f32

## HeavensDoorDemoObj

- `mIsInsideCageDemo`: _Bool

## HipDropMoveObj

- `mModelName`: const char *

## HipDropRock

- `mBreakModel`: ModelObj *
- `mItemType`: s32
- `mObjArg7`: _Bool
- `mStarPieceNum`: s32

## HipDropSwitch

- `mIsHipDropped`: _Bool
- `mIsOnPlayer`: _Bool
- `mWasOnPlayer`: _Bool

## HipDropTimerSwitch

- `mIsLightPressed`: _Bool
- `mTimerMax`: s32
- `mTimerSFXType`: s32
- `mWasLightPressed`: _Bool

## HitWallTimerSwitch

- `mIsHit`: u8
- `mTimer`: s32

## HomeButtonLayout

- `mForcedClosed`: _Bool
- `mIsInitialized`: _Bool

## HomingKiller

- `mChaseEndDist`: f32
- `mChaseInvalidTime`: s32
- `mChaseRotateSpeed`: f32
- `mChaseStartAngle`: f32
- `mChaseStartDist`: f32
- `mDisableChase`: _Bool
- `mFreezeTime`: s32
- `mIsLinearShot`: _Bool
- `mMoveTime`: s32
- `mPropeller`: PartsModel *
- `mTargetSensor`: HitSensor *
- `mTorpedoLight`: ModelObj *
- `mUseFullSightAngle`: _Bool

## HomingKillerLauncher

- `mKiller`: HomingKiller *

## HoneyBee

- `mFloatOffset`: f32
- `mInitAction`: s32
- `mRailPointNo`: s32

## HoneyQueen

- `mWing`: ModelObj *

## IceMerameraKing

- `mBreakModel`: LiveActor *
- `mCameraTarget`: CameraTargetDemoActor *
- `mEscapeStep`: u32
- `mExtinguishHeight`: f32
- `mHitPoint`: s32
- `mIsPinchBgmAllowed`: _Bool
- `mIsPinchBgmOn`: _Bool
- `mMeramera`: Meramera *
- `mShockWave`: LiveActor *
- `mThrowNum`: s32
- `mThrowingIce`: ThrowingIce *
- `mWeaponIndex`: s32
- `mWeaponNum`: s32

## IceStep

- `mTimer`: s32

## RailMoveObj

- `mWasRailMoving`: u8

## IconAButton

- `mFollowActor`: LayoutActor *

## ImageEffectDirector

- `mCurrentEffect`: ImageEffectBase *
- `mDepthOfFieldIntensity`: f32
- `mIsAuto`: _Bool
- `mIsDOFOffInSubjective`: _Bool
- `mIsPlayerSync`: _Bool
- `mPlayerSyncIntensity`: s32
- `mWasDOFSuppressed`: _Bool

## ImageEffectSystemHolder

- `mDirector`: ImageEffectDirector *

## InformationMessage

- `mAButtonIcon`: IconAButton *
- `mIsCenter`: _Bool

## InformationObserver

- `mDemoActor`: LiveActor *
- `mDisplayFrame`: s32
- `mIsTimeKeepDemo`: _Bool

## IronCannonLauncherPoint

- `mBulletSpeed`: f32
- `mShotInterval`: s32

## KoopaJrShipCannonShell

- `mFreezeStep`: s32

## ItemBlock

- `mCalcShadow`: _Bool
- `mItemCount`: s32
- `mKind`: s32
- `mNoCalcAnim`: _Bool
- `mTimer`: s32

## ItemBubble

- `mItemCount`: u32
- `mItemType`: u32
- `mRailSpeed`: f32
- `mSwing`: f32
- `mUseRail`: _Bool

## Jellyfish

- `mIsConnectedRail`: _Bool
- `mIsRailTurnBack`: _Bool
- `mRailGoalWaitStep`: s32
- `mRailSpeed`: f32
- `mWaveStep`: s32

## JellyfishElectric

- `mIsConnectedRail`: _Bool
- `mIsDanger`: _Bool
- `mIsRailTurnBack`: _Bool
- `mRailGoalWaitStep`: s32
- `mRailSpeed`: f32
- `mWaveStep`: s32

## Jiraira

- `mExplodeRadius`: f32

## JumpEmitter

- `mHeadModel`: PartsModel *
- `mIsEventCamera`: _Bool

## JumpBeamer

- `mBeams`: RingBeam * (array)

## JumpBranch

- `mRider`: LiveActor *
- `mSwingReverse`: _Bool

## JumpGuarder

- `mBabies`: JumpGuarderBaby *
- `mBabyVelocity`: f32
- `mBumpCooldown`: s32
- `mNumActiveBabies`: s32
- `mNumBabies`: s32
- `mNumCoins`: s32

## JumpHole

- `mBoundSensor`: HitSensor *
- `mLaunchDistance`: f32
- `mLaunchDuration`: u32
- `mLaunchLinear`: f32
- `mLaunchQuadratic`: f32
- `mLaunchTime`: f32
- `mSettledFrames`: s32

## JumpStand

- `mBindedActor`: LiveActor *
- `mIsMarioJumpingHigh`: _Bool

## Kabokuri

- `mBreakModel`: ModelObj *
- `mIsFloating`: _Bool
- `mIsValidDropFire`: _Bool
- `mKuribo`: Kuribo *

## Kameck

- `mActiveDistance`: f32
- `mBeam`: KameckBeam *
- `mBeamType`: s32
- `mMoveStep`: s32
- `mRailCoord`: f32
- `mRailNextPointCoord`: f32

## KameckBeam

- `mBeamKind`: s32
- `mKameckFireBalls`: KameckFireBall * (array)
- `mKameckTurtle`: KameckTurtle *

## KameckTurtle

- `mTurtle`: JetTurtle *

## Kanina

- `mKinokoOneUp`: BenefitItemObj *
- `mRunAwayBreakStep`: u32
- `mRunAwaySide`: f32
- `mRunAwayStep`: u32
- `mWalkCount`: s32

## Karikari

- `mCanAttack`: _Bool
- `mFrozenTime`: s32
- `mIsPushable`: _Bool
- `mIsReadyToLandTornado`: _Bool

## KarikariDirector

- `mClingNum`: s32
- `mLowestReleaseDist`: f32
- `mNoReleaseTime`: s32
- `mReleaseKarikari`: Karikari *
- `mRumbleTime`: s32

## Karon

- `mStopSwAnim`: _Bool

## KeySwitch

- `mCurDemoFrame`: s32

## Kinopio

- `mAppearBehavior`: s32
- `mBehavior`: s32
- `mGoodsIndex`: s32
- `mIsBgmStarted`: _Bool
- `mIsSpawnPowerStarMode`: _Bool
- `mObjArg0`: s32

## KinopioAstro

- `mHasLuigiLetter`: _Bool
- `mHasPeachLetter`: _Bool
- `mIsPlayerLuigi`: _Bool
- `mLetterIconTalkCtrl`: TalkMessageCtrl *
- `mLuigiLetter`: LuigiLetter *
- `mPeachLetter`: PeachLetter *

## Kiraira

- `mChain`: KirairaChain *
- `mDriftStep`: f32
- `mEyesOpen`: _Bool
- `mIsForceDetonated`: _Bool
- `mIsRail`: _Bool
- `mRailCoordSpeed`: f32
- `mSharedGroup`: MsgSharedGroup *

## KirairaChain

- `mAccelPointIdx`: s32
- `mAccelTimer`: s32
- `mFixPointBottom`: ModelObj *
- `mFixPointTop`: ModelObj *
- `mHost`: const LiveActor *
- `mIsCut`: _Bool
- `mModelDrawer`: SimpleModelDrawer *
- `mPointCount`: s32

## KoopaBattleMapPlanet

- `mCoinPlateGroup`: LiveActorGroup *
- `mDamagePlate`: LiveActor *
- `mDamagePlateGroup`: LiveActorGroup *
- `mIsFinalPlanet`: _Bool
- `mIsNoPlate`: _Bool
- `mModelName`: const char *
- `mPlateGroup`: LiveActorGroup *

## KoopaBattleMapStair

- `mFallStep`: s32
- `mFireAttackStep`: s32
- `mIsBig`: _Bool
- `mIsJumpAfterAttack`: _Bool
- `mIsKoopaAttack`: _Bool
- `mIsTurn`: _Bool
- `mLevel`: s32
- `mTargetCorner`: s32
- `mTimeToBreak`: s32
- `mType`: s32
- `mWaitFallStep`: s32

## KoopaFigureBall

- `mAngle`: f32
- `mKoopa`: Koopa *
- `mRadius`: f32

## KoopaFireShort

- `mDuration`: s32
- `mKoopa`: const Koopa *
- `mOffset`: f32
- `mSpeed`: f32

## KoopaFireStairs

- `mBreakModel`: ModelObj *
- `mIsKoopaJr`: _Bool
- `mStair`: const KoopaBattleMapStair *

## KoopaJr

- `mIsInvalidAppearStarPiece`: _Bool
- `mIsShipBattleTalk`: _Bool

## KoopaJrShip

- `mCoordSpeed`: f32
- `mHitPoint`: s32
- `mJr`: KoopaJr *
- `mPodModel`: ModelObj *
- `mPropAngle`: f32
- `mPropRotateSpeed`: f32
- `mShipBreakModel`: ModelObj *
- `mSpeedEaseStep`: s32

## KoopaPlanetShadow

- `mKoopa`: const Koopa *

## KoopaShockWave

- `mAngle`: f32
- `mKoopa`: Koopa *
- `mPartsModel`: PartsModel *
- `mShadow`: ModelObj *

## Kuribo

- `mHasItem`: _Bool
- `mIsNoGravitySupport`: _Bool

## KuriboChief

- `mKeySwitch`: KeySwitch *

## LargeChain

- `mBottomFixParts`: LargeChainParts *
- `mChainArray`: LargeChainParts *
- `mChainCount`: u32
- `mTopFixParts`: LargeChainParts *

## LavaBreakColumn

- `mIsPillarFallen`: _Bool

## LavaFloater

- `mFloatingForce`: FloaterFloatingForceTypeNormal *
- `mGroundOffset`: f32
- `mObjectName`: const char *

## LavaGeyser

- `mArg0`: s32
- `mArg1`: s32

## LavaHomeSeesaw

- `mSeesawRotator`: LavaHomeSeesawRotator *
- `mYOffset`: f32

## LavaHomeSeesawRotator

- `mAngularSpeedMax`: f32
- `mAngularVelocity`: f32
- `mInertia`: f32
- `mIsHipDropped`: _Bool
- `mMoveSound`: const char *
- `mMoveSoundSpeed`: f32
- `mRestoreForce`: f32
- `mRotateAngle`: f32

## LavaJamboSunPlanet

- `mBloomModel`: PartsModel *
- `mMoveSpeed`: f32

## LavaProminence

- `mBloomModel`: PartsModel *
- `mEmitDropEffect`: _Bool
- `mMoveRailCoord`: f32
- `mMoveRailEndOffset`: f32
- `mMoveSpeed`: f32
- `mWaitTime`: s32

## LavaProminenceTriple

- `mAppearTime`: s32
- `mBloomModel`: PartsModel *
- `mDefaultAppearTime`: s32
- `mIsAppearDrop1`: _Bool
- `mIsAppearDrop2`: _Bool
- `mIsAppearDrop3`: _Bool
- `mIsEnvironment`: _Bool
- `mRotateVel`: f32

## LavaStrangeRock

- `mHitType`: s32
- `mRockType`: s32

## LavaSunPlanet

- `mObjName`: const char *
- `mRotator`: MapPartsRotator *

## LensFlareDirector

- `mAsker`: u32
- `mBrightLevel`: f32
- `mBrightObjCount`: u32
- `mGlow`: LensFlareGlow *
- `mLine`: LensFlareLine *
- `mProbeCapacity`: u32
- `mRing`: LensFlareRing *

## LensFlareModel

- `mFadeLevel`: f32
- `mFadeStep`: f32
- `mFlareStrength`: f32

## LensFlareRing

- `mRingFrame`: f32

## LightArea

- `mPlacedZoneID`: s32

## LightCylinder

- `mLightVolume`: VolumeModelDrawer *

## LightDirector

- `mAreaHolder`: LightAreaHolder *

## LotusLeaf

- `mShakePeriod`: f32
- `mShakeSpeed`: f32

## LuigiLetter

- `mAButtonIcon`: IconAButton *
- `mGalaxyName`: const char *
- `mIsForTalk`: _Bool

## LuigiNPC

- `mType`: s32

## Manual2P

- `mBackButton`: BackButton *
- `mCanScrollLeft`: _Bool
- `mCanScrollRight`: _Bool
- `mPageIndex`: s32
- `mPageNum`: s32

## MapPartsAppearController

- `mIsAppeared`: u8
- `mSignMotionType`: s32

## MapPartsFloatingForce

- `mMaxDepth`: f32
- `mObjectName`: const char *
- `mRideSinkSpeed`: f32
- `mRotateAccelType`: s32
- `mRotateAngle`: f32
- `mRotateSpeed`: f32
- `mSinkDepth`: f32
- `mSinkSpeed`: f32

## MapPartsRailGuideDrawer

- `mGuideType`: s32
- `mModelName`: const char *
- `mRailId`: s32

## MapPartsRailGuideHolder

- `mNumRailGuides`: s32

## MapPartsRailGuidePoint

- `mCoord`: f32
- `mIsPointModel`: _Bool

## MapPartsRailMover

- `mAccelTime`: s32
- `mAcceleration`: f32
- `mInitCoord`: f32
- `mMoveConditionType`: s32
- `mMoveStopType`: s32
- `mRailPointPassChecker`: MapPartsRailPointPassChecker *
- `mSignCoord`: f32
- `mSignMotionType`: s32
- `mSpeed`: f32
- `mStopTime`: s32

## MapPartsRailPointPassChecker

- `mRailPointNo`: s32

## MapPartsRailPosture

- `mMovePosture`: s32

## MapPartsRailRotator

- `mAngle`: f32
- `mRotateAxis`: s32
- `mRotateSpeed`: f32
- `mRotateType`: s32
- `mTargetAngle`: f32

## MapPartsRotator

- `mAngle`: f32
- `mIsOnReverse`: _Bool
- `mRotateAccelType`: s32
- `mRotateAngle`: f32
- `mRotateAxis`: s32
- `mRotateSpeed`: f32
- `mRotateStopTime`: s32
- `mRotateType`: s32
- `mSignAngle`: f32
- `mSignMotionType`: s32
- `mTargetAngle`: f32
- `mTargetRotateSpeed`: f32

## MapPartsSeesaw1AxisRotator

- `mAngularAccel`: f32
- `mAngularSpeedMax`: f32
- `mAngularVelocity`: f32
- `mHipDrop`: _Bool
- `mInertiaConst`: f32
- `mMoveSound`: const char *
- `mMoveSoundSpeed`: f32
- `mRestoreForce`: f32
- `mRotateAngle`: f32

## MapPartsSeesaw2AxisRotator

- `mHipDrop`: _Bool
- `mInertiaConst`: f32
- `mMoveSound`: const char *
- `mMoveSoundSpeed`: f32
- `mRestoreForce`: f32
- `mRotateAngle`: f32
- `mRotateSpeed`: f32

## MarblePlanet

- `mCorePlanetModel`: ModelObj *
- `mNumElectrons`: s32
- `mRemainingElectrons`: s32

## MarblePlanetElectron

- `mElectronShadow`: MarblePlanetElectronShadow *
- `mParentPlanet`: MarblePlanet *

## MarblePlanetElectronShadow

- `mParentElectron`: MarblePlanetElectron *

## MarioActor

- `mActionHeld`: _Bool
- `mActionTrigger`: _Bool
- `mAirPunchHitCount`: u32
- `mAnimationFrozen`: _Bool
- `mAttacking`: _Bool
- `mAutoBindTable`: MatrixControl *
- `mBaseMtxForced`: _Bool
- `mBaseMtxSet`: _Bool
- `mBaseMtxStale`: _Bool
- `mBeeModel`: ModelHolder *
- `mBeeStickInhibit`: u16
- `mBeeWallWalk`: u8
- `mBeeWing`: MarioParts *
- `mBeeWingAnim`: u8
- `mBindOn`: _Bool
- `mBindRatio`: f32
- `mBlackHole`: BlackHole *
- `mBlendMtxRate`: f32
- `mBlendMtxTimer`: u16
- `mBlinkFrame`: u8
- `mBlinkTimer`: u16
- `mBlueFireDamage`: u8
- `mBlurPoseIndex`: u16
- `mBlurPoseSet`: u16
- `mBlurState`: u8
- `mBodyHeight`: f32
- `mBodyRadius`: f32
- `mBombTeresaTimer`: u16
- `mBoneModel`: ModelHolder *
- `mCameraCut`: _Bool
- `mCameraLiftTimer`: u16
- `mCameraUpTimer`: u16
- `mCareMovingObject`: _Bool
- `mClapCatchStart`: _Bool
- `mClapCoinCount`: u32
- `mCollisionShadow`: CollisionShadow *
- `mCommonEffectCount`: u16
- `mCurrModel`: u8
- `mDamageFlashTimer`: u16
- `mDarkMaskOn`: _Bool
- `mDarkMaskTimer`: u16
- `mDemoBaseKept`: _Bool
- `mDisplayModel`: u8
- `mEyeLidFrame`: u16
- `mEyeLidReset`: u8
- `mEyeLidTexBase`: u32
- `mFaceHeightTable`: MatrixValueGetter *
- `mFaceModel`: ModelHolder *
- `mFacePose`: u8
- `mFacePoseCount`: u8
- `mFireBalls`: FireMarioBall * (array)
- `mFirstMorphCameraEnd`: u16
- `mFirstMorphCameraOn`: u16
- `mFlashFogOn`: _Bool
- `mFlashFogRate`: f32
- `mFlashFogTimer`: u16
- `mFloorMaterialHold`: u16
- `mFogRate`: f32
- `mFogWaveTimer`: u16
- `mFootPrint`: FootPrint *
- `mForceKilled`: _Bool
- `mFpViewHidden`: u8
- `mFrameCount`: u32
- `mFreezeModel`: MarioParts *
- `mGameOverKind`: u8
- `mGravityBlendTimer`: u16
- `mGravityJumpTimer`: u16
- `mGravityRatio`: f32
- `mGravitySameAhead`: _Bool
- `mHandL`: ModelHolder *
- `mHandPoseL`: u8
- `mHandPoseR`: u8
- `mHandR`: ModelHolder *
- `mHandsHidden`: _Bool
- `mHealth`: u32
- `mHidden`: _Bool
- `mHiddenByDemo`: u8
- `mHipDropButton`: u16
- `mHomingSensor`: HitSensor *
- `mHopperModel`: ModelHolder *
- `mIceHandL`: ModelHolder *
- `mIceHandR`: ModelHolder *
- `mIceModel`: ModelHolder *
- `mIceStepIndex`: u16
- `mInDemo`: _Bool
- `mInRush`: _Bool
- `mInitializing`: u8
- `mInvincibleHandL`: ModelHolder *
- `mInvincibleHandR`: ModelHolder *
- `mInvincibleModel`: ModelHolder *
- `mItemPickupBlock`: u16
- `mJumpButton`: u16
- `mLastRushEndFrame`: u32
- `mLastRushSensor`: HitSensor *
- `mLifeDamageQueue`: u32
- `mLifeGainBlocked`: _Bool
- `mLockOnAngle`: f32
- `mMainSpinBlock`: u16
- `mMainSpinHeld`: _Bool
- `mMainSpinTrigger`: _Bool
- `mMaskIndex`: u16
- `mMaxHealth`: u32
- `mMessageEffectTable`: MatrixControl *
- `mModHidden`: _Bool
- `mMorphActionTable`: MatrixControl *
- `mMorphAnimDone`: u8
- `mMorphEndTimer`: u16
- `mMorphFlashDone`: u8
- `mMorphHidden`: _Bool
- `mMorphItemSensor`: HitSensor *
- `mMorphTimer`: u16
- `mMovementTimer`: u16
- `mMudWalkTimer`: u8
- `mNullAnimation`: MarioNullBck *
- `mNullAnimationKind`: s8
- `mNumModPowerUps`: s32
- `mPlayerMode`: u16
- `mPlayerModeTimer`: u16
- `mPolygonAttackDone`: _Bool
- `mPowerupCollected`: _Bool
- `mPreWipeOn`: _Bool
- `mPreWipeTimer`: u16
- `mPressCeilingSensor`: HitSensor *
- `mPressEndTimer`: u32
- `mPressKind`: u8
- `mPressScale`: f32
- `mPressSwellTimer`: u32
- `mPressTimer`: u32
- `mPrevPlayerMode`: u16
- `mPunchFrame`: u8
- `mPunchHitTimer`: u8
- `mPunchSensorOn`: _Bool
- `mPunchTimerSensor`: _Bool
- `mPushSensorAngle`: f32
- `mRasterScrollAmp`: f32
- `mRasterScrollPhase`: u16
- `mRasterScrollSet`: u8
- `mRasterScrollWave`: f32
- `mReflectAlpha`: u8
- `mReflectSimpleAlpha`: u8
- `mRemoteDemo`: _Bool
- `mRushButton`: u16
- `mRushEndFrontMode`: u16
- `mRushJustEnded`: _Bool
- `mRushSensor`: HitSensor *
- `mRushSensorCount`: u32
- `mScoutPrevRange`: f32
- `mScoutRange`: f32
- `mScoutSensor`: HitSensor *
- `mScreenBlurAlpha`: u8
- `mScreenBlurGrow`: f32
- `mScreenBlurOn`: u16
- `mScreenBlurTrailAlpha`: u8
- `mScreenBlurTrailGrow`: f32
- `mSearchLight`: MarioSearchLight *
- `mSearchLightTimer`: u32
- `mShadowDrawer`: DrawAdaptor *
- `mShadowMode`: u8
- `mSimpleModelHolder`: ModelHolder *
- `mSmokeEntryCount`: u16
- `mSpinButton`: u16
- `mSpinEffectHeight`: f32
- `mSpinEffectRadius`: f32
- `mSpinIntervalTimer`: u16
- `mSpinStarEndTimer`: u16
- `mSpinStarTimer`: u16
- `mSpinTico`: LiveActor *
- `mSpinTicoTimer`: u16
- `mStunTimer`: u16
- `mSubSpinBlock`: u16
- `mSubSpinHeld`: _Bool
- `mSubSpinTrigger`: _Bool
- `mSuperKinokoCollected`: _Bool
- `mSwingInhibitTimer`: u16
- `mSwingPermission`: _Bool
- `mSwingTrigger`: _Bool
- `mTakeCandidate`: HitSensor *
- `mTakeCount`: u32
- `mTakenSensor`: HitSensor *
- `mTaking`: _Bool
- `mTakingWobble`: f32
- `mTeresaAlpha`: f32
- `mTeresaBlinkTimer`: u16
- `mTeresaFlyWeight`: f32
- `mTeresaModel`: MarioParts *
- `mTeresaTilt`: f32
- `mThrowAimHold`: u16
- `mThrowAimSensor`: HitSensor *
- `mThrowMemoSensor`: HitSensor *
- `mThrowTarget`: HitSensor *
- `mThrowVecTimer`: u32
- `mTornadoEnd`: u8
- `mTornadoMario`: TornadoMario *
- `mTrampleCombo`: u8
- `mTrampleCounter`: CollectCounter *
- `mTrampleJumpStep`: u8
- `mTrampling`: _Bool
- `mTransforming`: _Bool
- `mTurtleShadow`: JetTurtleShadow *
- `mWaitTimer`: u16
- `mWallShadeArea`: const AreaObj *
- `mWallShadeAreaKind`: u8
- `mWallShadeDist`: f32
- `mWallShadeOn`: u8
- `mWaterColumn`: ModelObj *
- `mWaterLife`: u32
- `mWaterReflectAlpha`: u8

## MarioHolder

- `mActor`: MarioActor *

## MarioLauncher

- `mHost`: LiveActor *
- `mIsInAttractor`: _Bool
- `mLauncherType`: s32
- `mLayout`: MarioLauncherLayout *
- `mShell`: MarioLauncherShell *

## MarioLauncherLayout

- `mLayout`: SimpleLayout *

## MarioMeter

- `mHitPointMeter`: MeterLayout *
- `mSuddenDeathMeter`: SuddenDeathMeter *
- `mUseSuddenDeath`: _Bool

## MarioNullBck

- `mLastFrame`: u16

## MarioParts

- `mEffectName`: const char *

## MarioSearchLight

- `mBeamDistance`: f32
- `mHitTimer`: u16
- `mTarget`: HitSensor *

## MarioSubMeter

- `mActiveMeter`: SubMeterLayout *
- `mAirMeter`: SubMeterLayout *
- `mFlyMeter`: SubMeterLayout *

## MechaKoopaPartsArm

- `mIsRightArm`: _Bool

## MechanicKoopaMini

- `mIsAppearJumping`: _Bool
- `mIsForbidPause`: _Bool

## Meramera

- `mAppearStatus`: s32
- `mBodyEffectType`: s32
- `mCanDive`: _Bool
- `mChaseDistance`: f32
- `mEffectType`: s32
- `mElementType`: s32
- `mIsValidRestart`: _Bool
- `mRunawayTimer`: s32
- `mTargetDistance`: f32

## MercatorFixParts

- `mAppearController`: MapPartsAppearController *

## MercatorRailMoveParts

- `mAppearController`: MapPartsAppearController *
- `mIsNotMoving`: _Bool
- `mRailMover`: MapPartsRailMover *
- `mRailRotator`: MapPartsRailRotator *

## MercatorRotateParts

- `mAppearController`: MapPartsAppearController *
- `mRotator`: MapPartsRotator *

## MessageArea

- `mZoneID`: s32

## Metbo

- `mIsClipped`: _Bool

## MeteorStrike

- `mBrokenModel`: ModelObj *
- `mStepSize`: f32
- `mStepsLeft`: s32
- `mTotalSteps`: s32
- `mType`: s32

## MeteorStrikeLauncher

- `mCreateStep`: s32
- `mIntervalStep`: s32
- `mIsMeteorStrike`: _Bool
- `mIsValidMultiMeteor`: _Bool
- `mMeteorStrikeCount`: s32

## MeterLayout

- `mAlpha`: f32
- `mCount`: s32
- `mVisibleRate`: f32

## MiiFaceIcon

- `mHeight`: u16
- `mIsCreated`: _Bool
- `mIsFavoriteColorBG`: _Bool
- `mIsRequestMakeIcon`: _Bool
- `mWidth`: u16

## MiiFaceIconHolder

- `mIconNum`: u32
- `mIconNumMax`: u32

## MiiFaceParts

- `mWaiting`: _Bool

## MiiFacePartsHolder

- `mReady`: _Bool

## MiiSelect

- `mFavoriteMiiNum`: u32
- `mFellowIconNum`: u32
- `mIsIconProhibited`: u8
- `mMiiIndexRoom`: u32
- `mMiiNum`: u32
- `mPageBaseIndex`: s32
- `mSelectedIndex`: s32

## MiiSelectIcon

- `mIcon`: MiiFaceIcon *
- `mIsEnabled`: _Bool
- `mIsMiiDummy`: _Bool

## MiniatureGalaxy

- `mGalaxyName`: const char *
- `mIsZoomOut`: _Bool
- `mMysteryModel`: ModelObj *
- `mNamePlate`: GalaxyNamePlate *
- `mObjectName`: const char *
- `mOrbit`: AstroDomeOrbit *
- `mSelectModel`: ModelObj *
- `mShadowModel`: ModelObj *
- `mStarPlateModel`: ModelObj *
- `mState`: s32
- `mType`: s32
- `mZoomStep`: s32

## MiniatureGalaxyHolder

- `mCometGalaxy`: MiniatureGalaxy *
- `mCometID`: s32
- `mIsRegistered`: _Bool
- `mMiniatureGalaxyGroup`: LiveActorGroup *

## MirrorActor

- `mHost`: LiveActor *
- `mIsDisconnected`: _Bool
- `mRadius`: f32

## MirrorCamera

- `mPlaneD`: f32

## MirrorReflectionModel

- `mHost`: const LiveActor *
- `mIsHostGone`: _Bool

## Mogu

- `mHole`: ModelObj *
- `mIsCannonFleet`: _Bool
- `mStone`: MoguStone *
- `mThrowOnGravity`: _Bool

## MoguStone

- `mIsThrowHorizontal`: _Bool
- `mThrowSpeed`: f32

## Mogucchi

- `mHill`: MogucchiHill *
- `mHole`: ModelObj *
- `mIsStoppedByP2`: _Bool
- `mMaxStrollSpeed`: f32
- `mScatterUpSpeed`: f32
- `mStrollSpeed`: f32

## MogucchiHillPiece

- `mAutoEffectName`: const char *
- `mHasLightCtrl`: _Bool
- `mHost`: MogucchiHill *
- `mIsHostLeft`: _Bool
- `mIsHostTouching`: _Bool
- `mJointName`: const char *
- `mModelName`: const char *

## MorphItemObjNeo

- `mAngle`: f32
- `mAppearFrames`: s32
- `mBoundCount`: u8
- `mCameraMode`: u8
- `mCameraTargetMtx`: CameraTargetMtx *
- `mContainerBreakModel`: ModelObj *
- `mContainerModel`: ModelObj *
- `mContainerType`: u8
- `mIsBind`: s8
- `mIsDemoPending`: _Bool
- `mIsFloating`: _Bool
- `mIsSwitchAppearOnce`: _Bool
- `mIsUseSwitch`: _Bool
- `mMorphType`: s32
- `mRotateMode`: s8
- `mSpiralTime`: s32

## MoviePlayerSimple

- `mInputCheckedBefore`: _Bool
- `mLoop`: _Bool
- `mMovieName`: const char *

## MovieStarter

- `mMovieType`: s32

## MovieSubtitles

- `mAppearTime`: s32

## MsgSharedGroup

- `mMsg`: u32
- `mSensor`: HitSensor *
- `mSensorName`: const char *

## MultiSceneActor

- `mEffectSystem`: EffectSystem *
- `mIsDead`: _Bool
- `mIsScenarioSelectSky`: _Bool

## NameObjExecuteHolder

- `mExecuteArrayMaxSize`: s32
- `mExecuteArraySize`: s32

## NamePosHolder

- `mPosNum`: s32

## NebulaAchievementBoard

- `mBackground`: GalaxyMapBackground *
- `mIsOpen`: _Bool

## Tico

- `mBehavior`: u32
- `mFloatOffset`: f32
- `mTicoTalkCtrl`: TalkMessageCtrl *

## NebulaAchievementToast

- `mFrame`: s32
- `mHeight`: f32
- `mHoldFrames`: s32
- `mStep`: s32
- `mWidth`: f32

## NebulaModText

- `mAny`: _Bool
- `mHeight`: f32
- `mRevision`: u32
- `mScaleX`: f32
- `mWidth`: f32

## NebulaQuitButton

- `mIsDecided`: _Bool
- `mShownFrames`: s32

## NeedlePlant

- `mObjArg0`: s32

## NokonokoLand

- `mJetTurtle`: JetTurtle *
- `mRailPointPassChecker`: MapPartsRailPointPassChecker *
- `mTurtleType`: s32

## NormalMapBase

- `mAmbAlpha`: u8
- `mAmbColor`: u8
- `mBackLightMode`: u8
- `mDrawFlags`: u16
- `mGradTexMode`: s32
- `mHardLightAlphaMask`: u16
- `mHardLightColorMask`: u16
- `mLightAMode`: u8
- `mLightBMode`: u8
- `mLightingHighLevel`: u8
- `mLightingLowLevel`: u8
- `mMatAlpha`: u8
- `mMatColor`: u8
- `mTexFlags`: u16
- `mUseModelLightReg`: u16
- `mUseModelStageColor`: u16

## Note

- `mBrkFrame`: f32
- `mCounter`: int
- `mFlashCtrl`: FlashingCtrl *
- `mIsCountdown`: _Bool
- `mParentFairy`: NoteFairy *

## NoteCounter

- `mHost`: LiveActor *
- `mMelodyNo`: s32
- `mNoteAddNum`: s32
- `mNoteMissNum`: s32
- `mNoteNum`: s32
- `mRailLength`: f32

## NoteFairy

- `mAppearanceSpeed`: f32
- `mAppearanceType`: s32
- `mAppearedNoteNum`: s32
- `mCompletionJingle`: s32
- `mHasDemo`: _Bool
- `mIsDemoUntilAllNotes`: _Bool
- `mIsLowerBgm`: _Bool
- `mIsNoDemo`: _Bool
- `mIsNoRiddleSE`: _Bool
- `mIsNoteAtRailPoints`: _Bool
- `mLastGotNote`: Note *
- `mMelodyNoteNum`: s32
- `mNoteCoord`: f32
- `mPowerStarSpawnType`: s32
- `mSong`: s32
- `mTimeLimit`: s32

## NoteGroup

- `mRotation`: f32

## OceanBowl

- `mBloomDrawer`: OceanBowlBloomDrawer *
- `mIsClipped`: _Bool
- `mRadius`: f32
- `mTexStep`: f32
- `mTexU0`: f32
- `mTexU1`: f32
- `mTexU2`: f32
- `mTexV0`: f32
- `mTexV1`: f32
- `mTexV2`: f32
- `mWaveX`: f32
- `mWaveZ`: f32

## OceanBowlBloomDrawer

- `mHost`: OceanBowl *

## OceanFloaterLandParts

- `mAlreadyDoneFlag`: s32
- `mIsMoving`: _Bool
- `mIsRegisteredDemo`: _Bool
- `mObjectName`: const char *
- `mRailMover`: MapPartsRailMover *

## OceanHomeMapCtrl

- `mOceanHomePlanet`: PlanetMap *
- `mOceanRingPlanet`: PlanetMap *
- `mOceanRingPlanetLowInWater`: ModelObj *

## OceanRing

- `mBloomDrawer`: OceanRingBloomDrawer *
- `mIsClipped`: _Bool
- `mOceanRingPipe`: OceanRingPipe *
- `mPlayerCoord`: f32
- `mSegCount`: s32
- `mStride`: s32
- `mUpdateSegsAhead`: s32
- `mWaterPointNum`: s32
- `mWaveHeight1`: f32
- `mWaveHeight2`: f32
- `mWaveTheta0`: f32
- `mWaveTheta1`: f32
- `mWidthMax`: f32

## OceanRingBloomDrawer

- `mRing`: OceanRing *

## OceanRingPipe

- `mIsCameraInside`: _Bool
- `mNumPoints`: u32
- `mNumSegments`: s32
- `mNumSides`: s32
- `mOceanRing`: const OceanRing *
- `mPipeInside`: OceanRingPipeInside *
- `mPipeOutside`: OceanRingPipeOutside *
- `mWidth`: f32
- `mWidthArg`: f32

## OceanRingPipeInside

- `mRingPipe`: const OceanRingPipe *
- `mTexU0`: f32
- `mTexU1`: f32
- `mTexV0`: f32
- `mTexV1`: f32

## OceanRingPipeOutside

- `mRingPipe`: const OceanRingPipe *
- `mTexU`: f32

## OceanSmallTurtle

- `isBound`: u8
- `isStartFloat`: u8
- `mAccelRate`: f32
- `mArg1`: f32
- `mBoundDisplacement`: f32
- `mBoundTimer`: s32
- `mFloatDelayTimer`: f32
- `mFloatUpSpeed`: f32
- `mIsOnPlayer`: u8
- `mRailClippingRadius`: f32
- `mRailMover`: MapPartsRailMover *
- `mStartRotationX`: f32

## OceanSphere

- `mAlwaysUseRealDrawing`: _Bool
- `mEnableStartPosCameraSwitch`: _Bool
- `mIsCameraInside`: _Bool
- `mIsStartPosCamera`: _Bool
- `mPointCount`: s32
- `mRadius`: f32
- `mRadiusTarget`: f32
- `mTexOffs0X`: f32
- `mTexOffs0Y`: f32
- `mTexOffs1X`: f32
- `mTexOffs1Y`: f32
- `mUseMesh`: _Bool
- `mWaveTime1`: f32
- `mWaveTime2`: f32

## OceanWaveFloater

- `mCanRipple`: _Bool
- `mOffset`: f32
- `mSoundDelay`: s32
- `mSpring`: FloaterFloatingForceTypeSpring *

## OnimasuJump

- `mCurNormal`: s32

## OnimasuPivot

- `mCurNormal`: s32

## OnlyCamera

- `mCalcIdeal`: _Bool
- `mIsResetting`: _Bool
- `mSpeed`: f32
- `mStartPose`: _Bool

## OtaKing

- `mCocoNutBallArray`: CocoNutBall *
- `mDownCocoNut`: CocoNutBall *
- `mFeet`: PartsModel * (array)
- `mFireBallArray`: FireBall *
- `mFireBubbleArray`: FireBubble *
- `mHits`: s32
- `mIsLv2`: _Bool
- `mLongFeet`: OtaKingLongFoot * (array)
- `mMagma`: OtaKingMagma *
- `mThrowNum`: s32
- `mTurnSpeed`: f32

## OtaKingLongFoot

- `mDemoBckStep`: s32

## OtaKingMagma

- `mBloomModel`: PartsModel *

## OtaRock

- `mCocoNutArray`: CocoNutBall *
- `mFireBallArray`: FireBall *
- `mNoThrowCocoNut`: _Bool
- `mThrowCocoNutCounter`: s32
- `mWaitFrame`: u32

## PTimerSwitch

- `mIsPressed`: _Bool
- `mPressStep`: s32

## PackunPetit

- `mBlownModel`: ModelObj *
- `mDontTurn`: _Bool

## PalmIsland

- `mFloatStartStep`: s32
- `mIsRising`: s32

## PauseMenu

- `mConfirmWindow`: SysInfoWindow *
- `mLuigiLetter`: LuigiLetter *
- `mSettingsButton`: NebulaPauseSettingsButton *
- `mWasDefaultLayoutActive`: _Bool

## PeachLetter

- `mAButtonIcon`: IconAButton *

## Penguin

- `mBehavior`: s32
- `mStepToDive`: s32

## PenguinCoach

- `mActor`: LiveActor *
- `mBehavior`: s32
- `mIsRaceComplete`: _Bool

## PenguinRacer

- `mMoveSpeed`: f32
- `mRaceDisabled`: s32
- `mRailPointNo`: s32
- `mSpeed`: f32

## PenguinSkater

- `mCrossPoint`: s32
- `mCurrentRail`: NPCSupportRail *
- `mNumRails`: s32
- `mRail`: NPCSupportRail *
- `mSwitchRail`: NPCSupportRail *

## PenguinStudent

- `mActor`: LiveActor *
- `mObjArg0`: s32
- `mRailOffset`: f32

## Petari

- `mFootPrint`: FootPrint *
- `mFootprintYOffs`: f32
- `mMeanderAngle`: f32
- `mMeanderStep`: s32
- `mRequestSmoke`: _Bool
- `mStarPieceRemaining`: s32
- `mStarPieceTotal`: s32

## PhantomShipBoxFloater

- `mFloatingForce`: FloaterFloatingForceTypeSpring *
- `mObjectName`: const char *
- `mRippleSeCounter`: s32
- `mSinkDepthOver`: f32
- `mSinkDepthUnder`: f32

## PhantomShipBridge

- `mIsNutShipBridge`: s32

## PhantomShipHandle

- `mRotator`: MapPartsSeesaw1AxisRotator *

## PhantomTorch

- `mBurnSe`: const char *
- `mIsDecorative`: _Bool
- `mItem`: s32
- `mParticle`: const char *

## PictureBookCloseButton

- `mCanCloseWithPad`: _Bool

## PictureBookLayout

- `mChapterMax`: s32
- `mChapterMin`: s32
- `mChapterNo`: s32
- `mChapterRosettaMax`: s32
- `mCloseButton`: PictureBookCloseButton *
- `mIconAButton`: IconAButton *
- `mIsNextItemFast`: _Bool
- `mNextItemDir`: s32
- `mNotReadedChapterNo`: s32
- `mNotReadedPageNo`: s32
- `mNotReadedTextIndex`: s32
- `mPageNo`: s32
- `mTextIndex`: s32

## PlacementStateChecker

- `mCurrentZoneId`: int

## PlanetGravityManager

- `mMaxGravities`: s32
- `mNumGravities`: s32

## PlanetMapCreator

- `mTableCount`: s32

## Plant

- `mAccelTimer`: s32
- `mGrabbedTop`: _Bool
- `mJumpSpeed`: f32
- `mJumpSpeedOffsetY`: f32
- `mNumLeaves`: s32
- `mPlayAppearDemo`: _Bool
- `mRailCoord`: f32
- `mRideVelocity`: f32
- `mRider`: LiveActor *
- `mSeedPartsModel`: PartsModel *
- `mStalk`: PlantStalk *
- `mTopPartsModel`: PartsModel *

## PlantGroup

- `mHintIndex`: s32
- `mHintTimer`: s32
- `mIsStarPiece`: _Bool
- `mMemberCount`: s32
- `mPlantType`: s32

## PlantLeaf

- `mLeafCoord`: f32
- `mLeafSize`: f32
- `mSpringCoord`: f32
- `mSpringSpeed`: f32

## PlantMember

- `mHasItem`: _Bool
- `mTouchType`: s32

## PlantStalk

- `mGrowAccelTime`: s32
- `mGrowSpeed`: f32
- `mGrownPlantPoints`: s32
- `mGrowthPercent`: f32
- `mNumPlantPoints`: s32
- `mStalkLength`: f32

## PlayerActionGuidance

- `mCurrentLayout`: LayoutActor *
- `mGuidancePrevState`: s32
- `mGuidanceState`: s32
- `mShow`: _Bool
- `mSpinLayout`: SimpleLayout *
- `mTamakoroForced`: _Bool
- `mTamakoroLayout`: SimpleLayout *

## PlayerLeft

- `mIsForceAppear`: _Bool
- `mPlayerLeft`: s32

## PlayerSeArea

- `mSeType`: s32

## Poihana

- `mBehavior`: s32
- `mBindedActor`: LiveActor *
- `mBoundTimer`: s32
- `mCanDrown`: _Bool
- `mIsActive`: _Bool
- `mLaunchIntensity`: f32
- `mRandDir`: s32
- `mWaterColumn`: ModelObj *

## Pole

- `mClimbCoord`: f32
- `mClimbSpeed`: f32
- `mDisableHandstand`: _Bool
- `mHeight`: f32
- `mInvertStick`: _Bool
- `mIsSquare`: _Bool
- `mNoModel`: _Bool
- `mRider`: LiveActor *
- `mUsePole9mCollision`: _Bool

## Polta

- `mBombTeresaHolder`: BombTeresaHolder *
- `mBreakModel`: ModelObj *
- `mFormationModel`: ModelObj *
- `mGroundRockHolder`: PoltaGroundRockHolder *
- `mLeftArm`: PoltaArm *
- `mRightArm`: PoltaArm *
- `mRockHolder`: PoltaRockHolder *
- `mRotateSpeed`: f32

## PoltaArm

- `mBreakModel`: ModelObj *
- `mBrokenCounter`: s32
- `mFormationModel`: ModelObj *
- `mHitPoint`: s32
- `mHitPointMax`: s32
- `mIsLeftArm`: _Bool

## PoltaGroundRock

- `mBreakModel`: ModelObj *
- `mOwner`: LiveActor *

## PoltaRock

- `mOwner`: Polta *

## PomponPlant

- `mCanGenItem`: _Bool
- `mItemCount`: s32
- `mItemType`: s32

## PowerStar

- `mCameraActor`: PowerStarAppearPoint *
- `mColor`: s32
- `mIsCameraActorPose`: _Bool
- `mIsForceAppear`: _Bool
- `mIsGrandStar`: _Bool
- `mIsInDemo`: _Bool
- `mIsNoPostureFront`: _Bool
- `mIsRiseOnGravity`: _Bool
- `mIsWeak`: _Bool
- `mLuigiNPC`: ModelObj *
- `mPowerStarId`: s32
- `mPowerStarModelObj`: ModelObj *

## PowerStarHalo

- `mAppearDuringOpeningCam`: s32

## PowerStarHolder

- `mNumInfos`: s32

## PowerStarList

- `mPageNo`: s32
- `mPageNum`: s32
- `mSysInfoWindow`: SysInfoWindow *

## PressureMessenger

- `mSharedGroup`: MsgSharedGroup *
- `mWaitSyncTime`: s32

## PriorDrawAirHolder

- `mAirCount`: s32

## PrizeRing

- `mLifeTime`: s32

## Sky

- `mReflectionModel`: MirrorReflectionModel *
- `mSpaceInner`: SpaceInner *

## PrologueDirector

- `mCameraTarget`: CameraTargetMtx *
- `mLetter`: PrologueLetter *
- `mMarioPosDummyModel`: ModelObj *
- `mPictureBook`: ProloguePictureBook *
- `mScenery`: ModelObj *
- `mUseSwitchA`: _Bool

## PrologueHolder

- `mDirector`: PrologueDirector *

## PrologueLetter

- `mAButtonIcon`: IconAButton *

## ProloguePictureBook

- `mIconAButton`: IconAButton *
- `mPage`: u32

## Pukupuku

- `mBckBeforeBind`: const char *
- `mBckFrameBeforeBind`: f32
- `mBtpFrameBeforeBind`: f32
- `mIsTrampled`: _Bool

## PunchBox

- `mCoinCount`: s32
- `mFloorTouchTimer`: s32
- `mHasShadow`: _Bool
- `mHitCoolDown`: s32
- `mHitPoint`: u16
- `mIsPlayerNear`: _Bool
- `mShadowStep`: u32

## PunchingKinoko

- `mCameraVecId`: s32
- `mGroundChecker`: GroundChecker *
- `mInvincibleHitCoolDown`: s32
- `mIsPointerHeld`: _Bool
- `mStarPointerHitCoolDown`: s32

## PurpleCoinCounter

- `mInvalidCountUpFrame`: s32
- `mIsValid`: _Bool
- `mPurpleCoinDisplayNum`: s32
- `mPurpleCoinNum`: s32

## PurpleCoinHolder

- `mStarter`: PurpleCoinStarter *

## PurpleCoinStarter

- `mHost`: PurpleCoinHolder *

## QuestionCoin

- `mBoundActor`: LiveActor *
- `mCalcShadow`: _Bool
- `mIsWeakGravity`: _Bool
- `mLaunchSpeed`: f32
- `mPoseType`: s32
- `mShadowDropLength`: f32
- `mShadowDropLengthType`: s32

## Rabbit

- `mBehavior`: s32
- `mIsFalling`: _Bool
- `mIsLanded`: u16
- `mJumpHeight`: f32
- `mJumpScale`: f32
- `mJumpSpeed`: f32
- `mMoveSpeed`: f32
- `mWaitStep`: s32

## RaceManager

- `mAudienceNum`: u32
- `mBestTime`: u32
- `mCurrentRace`: s32
- `mLayout`: RaceManagerLayout *
- `mPlayerRacer`: PlayerRacer *
- `mRacerNum`: u32
- `mRank`: u32
- `mTime`: u32

## RaceRail

- `mPlayerRacer`: PlayerRacer *

## RailBlock

- `mIsConnectedToRail`: _Bool
- `mMoveTime`: s32
- `mRailSpeed`: s32

## RainCloud

- `mCloudCylinder`: ModelObj *
- `mIsOnPlayer`: _Bool
- `mNoRain`: _Bool
- `mRailMover`: MapPartsRailMover *
- `mRainLength`: f32
- `mShadowDropLength`: f32

## RandomEffectObj

- `mIsFireworks`: _Bool
- `mLifeTime`: s32
- `mRandWidth`: s32
- `mSeStep`: s32
- `mStep`: s32

## RestartCube

- `mBgmIndex`: s32
- `mBgmStateIndex`: s32
- `mIsBgmChanged`: _Bool

## RevolvingWay

- `mRadius`: f32

## RingBeam

- `mBloomModel`: ModelObj *
- `mHasShadow`: _Bool
- `mHost`: LiveActor *
- `mIsFollowGravity`: _Bool
- `mLife`: s32
- `mShadowDrawer`: RingBeamShadowDrawer *
- `mShadowModel`: ModelObj *
- `mSpeed`: f32

## RingBeamShadowDrawer

- `mHost`: const LiveActor *
- `mRadius`: f32

## Rock

- `mAirTime`: u32
- `mAppearAngle`: f32
- `mAppearTime`: s32
- `mBreakModel`: ModelObj *
- `mBreakModelOnRailGoal`: _Bool
- `mCreator`: RockCreator *
- `mCurrentRailPoint`: s32
- `mFreezeTime`: s32
- `mGravityRate`: f32
- `mInvalidBindTime`: s32
- `mIsNormalGravity`: _Bool
- `mMoveSpeed`: f32
- `mRadius`: f32
- `mRollSoundTimer`: s32
- `mRotateSpeed`: f32
- `mSlowDownOnAttack`: _Bool
- `mSlowDownTimer`: s32
- `mWanwanVoiceTimer`: s32

## RockCreator

- `mBreakModelOnRailGoal`: _Bool
- `mIsValid`: _Bool
- `mNumRocks`: s32
- `mRockSpeed`: f32
- `mRocks`: Rock *
- `mSpawnDelay`: s32
- `mSpawnTimer`: u32

## Rosetta

- `mObjArg0`: s32

## RosettaMonologue

- `mIconAButton`: IconAButton *
- `mMessageNo`: s32

## RosettaPictureBook

- `mIconAButton`: IconAButton *
- `mIsValidOpenIconAButton`: _Bool
- `mLayout`: PictureBookLayout *

## RosettaReading

- `mPictureBookLayout`: PictureBookLayout *

## RotateMoveObj

- `mIsGear`: _Bool

## RunawayRabbit

- `mCollect`: RunawayRabbitCollect *
- `mFootPrint`: FootPrint *
- `mGroupId`: s32
- `mHideLocation`: s32
- `mIsActive`: _Bool
- `mIsLinked`: _Bool
- `mMsgCtrl`: TalkMessageCtrl *
- `mNearSoundRadius`: f32
- `mNotCaughtableTimer`: s32
- `mPressureTimer`: s32
- `mRunawayLevel`: s32
- `mSpotMarkLight`: SpotMarkLight *

## RunawayRabbitCollect

- `mBgmState`: s32
- `mCaughtRabbitCount`: s32
- `mCompleteRabbitCount`: s32
- `mIsAppearTico`: _Bool (array)
- `mRabbitNum`: s32
- `mTicoNum`: s32

## RunawayTico

- `mDemoCastID`: s32
- `mIsAllCaught`: _Bool
- `mIsStartRunaway`: _Bool
- `mRole`: s32

## SamboHead

- `mIsPursueAlways`: _Bool

## SandUpDownEffectObj

- `mLineLength`: f32
- `mLineOffset`: f32

## SandUpDownTriRock

- `mRestStep`: s32

## Sandstorm

- `mAdjustStartRadius`: f32
- `mBindActor`: LiveActor *
- `mBindHeight`: f32
- `mBindOffsetY`: f32
- `mBindRadius`: f32
- `mBindStartStep`: f32
- `mBindStep`: s32
- `mSensorHeight`: f32
- `mSensorMin`: f32
- `mSizeUpRate`: f32
- `mSpiralAngle`: f32
- `mSpiralRadius`: f32
- `mSpiralRot`: f32
- `mStepAdjust`: s32
- `mStepEnd`: s32
- `mStepFix`: s32
- `mStepStorm`: s32
- `mType`: s32

## SaveIcon

- `mActor`: const LayoutActor *

## ScenarioDataParser

- `mScenarioDataNum`: s32

## ScenarioSelectLayout

- `mBackButton`: BackButton *
- `mCameraContext`: const CameraContext *
- `mEffectSystem`: EffectSystem *
- `mIsDecided`: _Bool
- `mListNum`: s32
- `mListPage`: s32
- `mMarioPaneName`: const char *
- `mPageKeyHeld`: _Bool
- `mScenarioSky`: MultiSceneActor *
- `mSelectedScenarioNo`: s32
- `mTextFadeState`: s32
- `mTextScenarioNo`: s32

## ScenarioSelectStar

- `mAppearFrame`: int
- `mIsPointing`: _Bool
- `mRotateSpeed`: f32
- `mRotateTime`: s32
- `mScaleOnSelect`: f32
- `mScenarioNo`: s32
- `mStarCollectedStatus`: int
- `mStarId`: s32

## ScenarioStarter

- `mBindActor`: LiveActor *
- `mFallAngle`: f32
- `mFallMotionStartStep`: s32
- `mFlightTime`: s32
- `mFlyEndStep`: s32
- `mFlyMotionStartStep`: s32
- `mInitalPlayerRotation`: f32
- `mIsSkipped`: _Bool
- `mSpinAngle`: f32
- `mSpinAngleMax`: f32
- `mTitle`: ScenarioTitle *
- `mWelcomeLayout`: SimpleLayout *

## SceneDataInitializer

- `mDataHolder`: StageDataHolder *

## SceneNameObjMovementController

- `mIsSyncRequested`: _Bool

## ScenePlayingResult

- `mCoinNum`: s32
- `mPurpleCoinNum`: s32

## WipeHolderBase

- `mCurrentWipeLayout`: WipeLayoutBase *
- `mWipeLayoutCount`: u32

## ScreenBlurArea

- `mIsPlayerSync`: _Bool

## ScreenBlurEffect

- `mIntensity`: f32
- `mScale`: f32
- `mSkipDraw`: _Bool

## ScreenPreserver

- `mIsActive`: _Bool

## ScrewSwitch

- `mBindActor`: LiveActor *

## ScrewSwitchReverse

- `mHost`: LiveActor *
- `mIsForceJump`: _Bool

## SeaGull

- `mBankAngle`: f32
- `mChirpTimer`: s32
- `mGlideTimer`: s32
- `mHoverTimer`: s32
- `mIsReverse`: _Bool
- `mLiftTimer`: s32
- `mPointIndex`: s32
- `mSeaGullGroup`: SeaGullGroup *
- `mTargetUpdateTimer`: s32

## SeaGullGroup

- `mPointCount`: s32

## SearchBeamer

- `mBeamAimSpeed`: f32
- `mBeamLength`: f32
- `mPropellerAngle`: f32
- `mPropellerSpeed`: f32
- `mTerritoryRadius`: f32

## SeesawMoveNut

- `mBackHomeStep`: s32
- `mPrevAngularSpeed`: f32
- `mSpeed`: f32

## ShadowClipActor

- `mHost`: LiveActor *

## ShadowSurfaceCircle

- `mRadius`: f32

## ShadowVolumeBox

- `mPoints`: Vec3f (array)
- `mVariant`: u32

## ShadowVolumeCylinder

- `mRadius`: f32

## ShadowVolumeFlatModel

- `mDropJoint`: u32
- `mRootJoint`: u32

## ShadowVolumeLine

- `mFromWidth`: f32
- `mHasSides`: _Bool
- `mSideFlips`: u32
- `mToWidth`: f32

## ShadowVolumeSphere

- `mRadius`: f32

## Shellfish

- `mIsItemBound`: _Bool
- `mItem`: LiveActor *
- `mItemType`: s32

## ShootingStar

- `mFallHeight`: f32
- `mNumBurstStarPiece`: s32
- `mWaitForNextShootTime`: s32

## SimpleBloomArea

- `mIntensity`: u8
- `mMaskFilterColor`: u32
- `mThreshold`: u8

## SimpleBreakableObj

- `mBreakEnded`: _Bool
- `mBreakFlags`: u16
- `mBreakOffsetFrame`: s32
- `mItemNum`: s32
- `mItemType`: s32
- `mKinokoOneUp`: BenefitItemObj *
- `mPlayRiddleSound`: _Bool
- `mSwitchDeadDelay`: s32

## SimpleFloaterObj

- `mFloatingForce`: MapPartsFloatingForce *
- `mObjName`: const char *

## SimpleTimerObj

- `mFlashingCtrl`: FlashingCtrl *
- `mTimeLeft`: s32
- `mTimeLimit`: s32

## SkeletalFishBaby

- `mSpeed`: f32
- `mSpeedMax`: f32
- `mStarPieceTargets`: LiveActor * (array)

## SkeletalFishBoss

- `mBgmHoldStep`: s32
- `mBgmType`: s32
- `mBossDirector`: SkeletalFishBossBattleDirector *
- `mBossHead`: SkeletalFishBossHead *
- `mBossInfo`: SkeletalFishBossInfo *
- `mBreakModel`: ModelObj *
- `mCameraTargetMtx`: CameraTargetMtx *
- `mCurScene`: const char *
- `mDamageBrkIndex`: u32
- `mGuardHolder`: SkeletalFishGuardHolder *
- `mGuardNum`: s32
- `mHitsLeft`: s32
- `mLevel`: s32
- `mLevelNum`: s32
- `mScarFlash`: SkeletalFishBossScarFlash *

## SkeletalFishBossBattleDirector

- `mClipActorNum`: s32
- `mColumnNum`: s32
- `mFishBoss`: SkeletalFishBoss *
- `mGuardModels`: ModelObj * (array)

## SkeletalFishBossHead

- `mBloomModels`: ModelObj * (array)
- `mIsEyeLightOn`: _Bool
- `mLightModels`: ModelObj * (array)

## SkeletalFishBossInfo

- `mFishBoss`: SkeletalFishBoss *
- `mGuardNum`: s32
- `mLevelNum`: s32

## SkeletalFishBossRail

- `mRailId`: s32

## SkeletalFishGuard

- `mApartFallSpeed`: f32
- `mAppearWaitStep`: u32
- `mAttackDelay`: s32
- `mFishBoss`: SkeletalFishBoss *
- `mSpeed`: f32

## SkeletalFishGuardHolder

- `mAppearRailCoord`: f32
- `mBossRail`: SkeletalFishBossRail *
- `mFishBoss`: SkeletalFishBoss *
- `mIsValid`: u8
- `mNumGuards`: s32

## SlingShooter

- `mCameraTargetMtx`: CameraTargetMtx *
- `mIsKinopioAttached`: _Bool
- `mIsKinopioCameraFocused`: _Bool
- `mIsKinopioTalkEnabled`: _Bool
- `mIsLastDamage`: _Bool
- `mPadChannel`: s32
- `mPassThroughEnable`: _Bool
- `mPlayerZ`: f32
- `mRider`: LiveActor *

## SmallStone

- `mIsStarPiece`: _Bool
- `mMemberCount`: s32
- `mPlacementOffset`: f32
- `mStoneType`: s32
- `mUseGravity`: _Bool

## SmallStoneMember

- `mAnimRate`: f32
- `mAnimStopped`: _Bool
- `mBreakEffectName`: const char *
- `mBreakTimer`: s32
- `mShakeRequested`: u8
- `mUseGravity`: _Bool

## Snakehead

- `mActivationRadius`: f32
- `mBackwardSpeed`: f32
- `mForwardSpeed`: f32
- `mItem`: BenefitItemOneUp *

## SnowFloor

- `mCheckCount`: u16
- `mDirtyFrames`: s32
- `mHalfAngle`: f32
- `mHalfLength`: f32
- `mRadius`: f32
- `mReduceMapSize`: u16
- `mResistFrames`: s32
- `mTextureIndex`: u8
- `mWalkingResist`: f32

## SnowFloorTile

- `mCellSize`: f32
- `mDirtyFrames`: s32
- `mReduceMapSize`: u16
- `mTextureIndex`: u8
- `mTextureShift`: s32
- `mTextureSize`: s32

## SnowplowSwitch

- `mIsDiscovered`: _Bool

## SoundEmitter

- `mSoundType`: s32

## SoundEmitterCube

- `mSoundName`: const char *

## SoundEmitterSphere

- `mSoundIndex`: s32
- `mSoundName`: const char *

## SpaceCocoon

- `mAttackTime`: s32
- `mCameraTargetMtx`: CameraTargetMtx *
- `mCocoonModel`: PartsModel *
- `mHeight`: f32
- `mIsKinopioCameraFocused`: _Bool
- `mNumPoints`: s32
- `mPadChannel`: s32
- `mRider`: LiveActor *
- `mTouchTime`: s32

## SpaceMine

- `mIsPlayerNear`: u8
- `mRotateSpeed`: f32
- `mShadowType`: s32

## SpaceShipStep

- `mHasBeenSeen`: _Bool
- `mHiddenStep`: s32

## SphereRailDash

- `mBinderSensor`: HitSensor *
- `mCoordCubic`: f32
- `mCoordLinear`: f32
- `mCoordQuadratic`: f32
- `mEndSpeed`: f32
- `mMoveStep`: s32

## SphereSelector

- `mClickStep`: s32
- `mHandle`: SphereSelectorHandle *
- `mIsPointingInvalid`: _Bool
- `mIsSePlayed`: _Bool
- `mNearestDist`: u32
- `mNearestTarget`: LiveActor *
- `mPointingTarget`: LiveActor *
- `mSelectedTarget`: LiveActor *
- `mSphereGroup`: LiveActorGroup *

## SphereSelectorHandle

- `mIsFileSelectMode`: _Bool
- `mIsRotateBgm`: _Bool
- `mNotPointingStep`: s32
- `mPrevRotateSpeedX`: f32
- `mRotateSpeed`: f32
- `mRotateSpeedX`: f32
- `mTargetRotateSpeed`: f32

## SpiderCoin

- `mCocoonModel`: PartsModel *
- `mThreadZ`: f32

## SpiderMapBlock

- `mPosZ`: f32

## SpiderThread

- `mCirclePartsBufferSize`: s32
- `mCutPointsBufferSize`: s32
- `mIsBloomOn`: _Bool
- `mNumCircleParts`: s32
- `mNumCutPoints`: s32
- `mNumHangInfos`: s32
- `mNumRadialLines`: s32
- `mNumRadialParts`: s32
- `mRadialPartsBufferSize`: s32

## SpinDriver

- `mBindActor`: LiveActor *
- `mCanBind`: _Bool
- `mCaptureRate`: f32
- `mClippingRadius`: f32
- `mCoolDown`: s32
- `mFallStep`: s32
- `mFlyEndStep`: s32
- `mFlyShortStep`: s32
- `mInitPoseAngle`: f32
- `mIsAppearSilent`: _Bool
- `mIsCoolingDown`: _Bool
- `mIsPlayerAway`: _Bool
- `mJumpSpeed`: f32
- `mShadowDropLength`: f32
- `mShootTime`: s32
- `mSpaceFlyEndFrame`: s32
- `mSpinAngle`: f32
- `mSpinSpeed`: f32

## SpinDriverOperateRing

- `mOffsetLength`: f32
- `mOffsetRate`: f32
- `mOperateState`: s32
- `mRadiusRate`: f32

## SpinDriverPathDrawInit

- `mIsPathAtOpa`: _Bool

## SpinDriverPathDrawer

- `mColor`: s32
- `mCoord`: f32
- `mEndCut`: f32
- `mFadeScale`: f32
- `mMaskLength`: f32
- `mMaxCoord`: f32
- `mPositionCount`: s32

## SpinningBox

- `mIsIceBox`: _Bool
- `mIsOnGround`: _Bool
- `mRotateSpeed`: f32
- `mSlideSpeed`: f32
- `mSpinAngle`: f32

## SpotMarkLight

- `mIsDisappeared`: _Bool

## SpringJetWater

- `mObjName`: const char *

## SpringWaterFloaterSpot

- `mBoundSize`: f32
- `mBoundStep`: s32
- `mFloatTimer`: s32
- `mHasSurfaced`: _Bool
- `mHeight`: f32
- `mIsOnPlayer`: _Bool
- `mNeedsBound`: _Bool
- `mRiseAccel`: f32
- `mRiseSpeed`: f32
- `mWaterCheckOffset`: f32

## StaffRoll

- `mBgmFrame`: s32
- `mInfo`: InformationMessage *
- `mLineStep`: s32
- `mMsgLine`: u32
- `mMsgLineNum`: u32
- `mPicture`: StaffRollPicture *

## StageDataHolder

- `mLoadCommon`: u8
- `mPlacementStage`: unsigned int
- `mStageDataHolderCount`: s32
- `mStageName`: const char *
- `mZoneID`: s32

## StageResultInformer

- `mClearedStarPieceNum`: s32
- `mHasBufferedDecide`: _Bool
- `mIconAButton`: IconAButton *
- `mIsNewBestCoin`: _Bool

## StarCounter

- `mPowerStarNum`: s32

## StarPiece

- `mChaseRate`: f32
- `mFallKillTimer`: s32
- `mGettableDelayCounter`: s32
- `mGettableEffectStep`: s32
- `mNumGift`: s32
- `mTargetSensor`: HitSensor *
- `mTurnRate`: f32

## StarPieceCounter

- `mForcedAppear`: s32
- `mInvalidCountUpFrame`: s32
- `mMode`: int
- `mStarPieceDisplayNum`: s32
- `mStarPieceNum`: s32

## StarPieceDirector

- `mColorIndex`: s32
- `mNumStarPiecesLaunched`: s32
- `mQueueNewGetSound`: _Bool
- `mResetChasingStarPiece`: _Bool
- `mSoundStep`: u32
- `mStarPieceShooters`: StarPieceShooter * (array)

## StarPieceFollowGroup

- `mAngle`: f32
- `mNumPiecesTotal`: s32
- `mNumPiecesUse`: s32
- `mRadius`: f32
- `mTimeLimit`: s32

## StarPieceGroup

- `mCircleRadius`: f32
- `mHasAppeared`: _Bool
- `mIsAppearEffect`: _Bool
- `mIsRail`: _Bool
- `mLifeStep`: s32
- `mNumPieces`: s32
- `mPlaceAtPathPoints`: _Bool
- `mRailSpeed`: f32

## StarPieceMother

- `mNextPiece`: u32
- `mNumPieces`: s32
- `mRailSpeed`: f32

## StarPieceShooter

- `mChargeStep`: s32
- `mLockOnSensor`: HitSensor *
- `mPort`: s32
- `mSearchDist`: f32
- `mSearchSensor`: HitSensor *
- `mShootSpeed`: f32

## StarPointerBlur

- `mNumHiddenPoints`: s32
- `mPadChannel`: s32

## StarPointerCommandStream

- `mOffScreenTime`: s32
- `mPadChannel`: s32

## StarPointerGuidance

- `mIsActive`: _Bool
- `mIsActiveRequest1P`: _Bool
- `mIsActiveRequest2P`: _Bool
- `mIsGuidanceDisabled`: _Bool
- `mIsTimeOutEnabled`: _Bool
- `mOutScreenTime`: s32
- `mRequestTime1P`: s32
- `mRequestTime2P`: s32
- `mTextLineNum1P`: u32
- `mTextLineNum2P`: u32
- `mTextLineNumGuidance`: u32

## StarPointerLayout

- `mActor`: const LiveActor *
- `mAppearTime`: s32
- `mBlur`: StarPointerBlur *
- `mCommandStream`: StarPointerCommandStream *
- `mIsAppear`: _Bool
- `mIsNewTouch`: _Bool
- `mIsPointerValid`: _Bool
- `mNumber`: StarPointerNumber *
- `mPadChannel`: s32
- `mRadius`: f32
- `mShootDisabled`: _Bool
- `mSingleTouch`: _Bool
- `mStartDisableShoot`: _Bool
- `mStartSingleTouch`: _Bool
- `mStartTouch`: _Bool
- `mTouchTimer`: s32

## StarPointerNumber

- `mAppearTime`: s32
- `mIsAppear`: _Bool
- `mPadChannel`: s32
- `mParent`: StarPointerLayout *

## StarReturnDemoStarter

- `mFrame`: s32
- `mHair`: LiveActor *
- `mLuma`: LiveActor *
- `mPowerStar`: LiveActor *
- `mStageResultInformer`: StageResultInformer *

## StinkBugBase

- `mIsTurnLeft`: _Bool
- `mRadius`: f32
- `mTerritoryAngle`: f32

## StinkBugParent

- `mIsChildAlive`: _Bool

## StinkBugSmall

- `mIsBodyAttack`: _Bool

## StopSceneController

- `mDelayRequestMax`: s32
- `mDelayRequestNum`: s32
- `mFrame`: s32

## StrayTico

- `mDemoAngle`: f32
- `mHost`: CollectTico *
- `mItemBubbleModel`: PartsModel *

## StringSpider

- `mIsPointerPulled`: _Bool
- `mPointerStep`: s32
- `mString`: StringSpiderString *
- `mStringLength`: f32
- `mStringLengthMax`: f32

## SubMeterLayout

- `mFullLifeFrame`: u16
- `mLifeFrame`: u16
- `mPhase`: u8

## SubmarineVolcanoBigColumn

- `mBreakModel`: ModelObj *
- `mIsSmallColumn`: _Bool

## SuddenDeathMeter

- `mAlpha`: f32
- `mCount`: s32
- `mShowHideRate`: f32

## SunakazeKunTrap

- `mHost`: LiveActor *

## SuperSpinDriver

- `mAlreadyDoneFlag`: u32
- `mAlreadyDoneFlagIdx`: s32
- `mBindActor`: LiveActor *
- `mClippingRadius`: f32
- `mColor`: s32
- `mCoolDown`: s32
- `mDrawPathRangeIdx`: s32
- `mEmptyModel`: ModelObj *
- `mFallStep`: s32
- `mFlightTime`: s32
- `mFlyEndStep`: s32
- `mFlyLoopStep`: s32
- `mFrontAngle`: f32
- `mIsCoolingDown`: _Bool
- `mIsDisableJingle`: _Bool
- `mIsPlayerAway`: _Bool
- `mIsPullPlayer`: _Bool
- `mOperateRing`: SpinDriverOperateRing *
- `mPathDrawer`: SpinDriverPathDrawer *
- `mPlayerLandRotation`: f32
- `mRollAngle`: f32
- `mShadowLength`: f32
- `mSpaceFlyEndFrame`: s32
- `mSpaceFlyStartFrame`: s32
- `mSpinSpeed`: f32
- `mSwingAngle`: f32
- `mSwingAngleMax`: f32

## SurfRay

- `mAirTime`: s32
- `mInTutorial`: _Bool
- `mInTutorialArea`: _Bool
- `mInWater`: _Bool
- `mLectureIdx`: s32
- `mPadChannel`: s32
- `mRayTilt`: f32
- `mRider`: LiveActor *
- `mShadowAlpha`: f32
- `mSteerAccel`: f32
- `mSteerRate`: f32
- `mSurfSpeed`: f32
- `mTwistBufferSize`: s32

## SurprisedGalaxy

- `mGalaxyName`: const char *

## SwingLight

- `mLightCylinder`: LightCylinder *

## SwingRope

- `mAccelTime`: s32
- `mGrabCoord`: f32
- `mGrabPointNum`: f32
- `mIsStretched`: _Bool
- `mNumPoints`: s32
- `mPrevSoundLvl`: s32
- `mRider`: LiveActor *
- `mRopeLength`: f32
- `mSlideSpeed`: f32
- `mStretchTime`: s32

## SwitchBox

- `mFloorTouchTimer`: s32
- `mHitCoolDown`: s32
- `mHitPoint`: u16

## SwitchSynchronizer

- `mIsCheckSameSwitchState`: _Bool

## SwitchWatcher

- `mFlags`: u32

## Syati

- `mBlinkTimer`: s32
- `mClippingRange`: f32
- `mCurrentRailPointNo`: s32
- `mHideOnShoreMode`: s32
- `mNumRings`: s32
- `mPrizeRingCount`: s32
- `mPrizeRingGroup`: LiveActorGroup *
- `mPrizeRingLife`: s32
- `mRaceManagerLayout`: RaceManagerLayout *
- `mStarSpawnType`: s32
- `mSwimMode`: s32
- `mTalkMessageCtrl`: TalkMessageCtrl *

## SysInfoWindow

- `mDim`: f32
- `mDimEpoch`: u32
- `mDimFloor`: f32
- `mDimHandOver`: u8
- `mDimHanding`: _Bool
- `mDimLinger`: s32
- `mExecutesIconAButton`: _Bool
- `mIconAButton`: IconAButton *
- `mIsBackAnswersNo`: _Bool
- `mTextParentPaneName`: const char *
- `mWindowParentPaneName`: const char *

## SystemWipeHolder

- `mIsCapturing`: _Bool

## TakoHei

- `mDeathItem`: u32
- `mHasMoveLimit`: u8
- `mNoPushStep`: s32

## Takobo

- `mBox`: SpinningBox *
- `mIsCheckFireGround`: _Bool
- `mIsMoveBack`: _Bool
- `mMoveEnd`: f32
- `mMoveRange`: f32
- `mMoveSpeed`: f32
- `mMoveStart`: f32
- `mMoveStep`: s32

## TalkBalloon

- `mHasBeak`: _Bool
- `mIsBeakMoving`: _Bool
- `mMessageCtrl`: TalkMessageCtrl *

## TalkBalloonEvent

- `mAButton`: IconAButton *
- `mFormMode`: u32
- `mPlaced`: _Bool

## TalkBalloonShort

- `mInterval`: u32

## TalkDirector

- `mBalloonCtrl`: TalkMessageCtrl *
- `mBranchResult`: _Bool (array)
- `mDemoType`: s32
- `mHostActor`: LiveActor *
- `mIsInvalidClipping`: _Bool
- `mIsInvalidTalk`: _Bool
- `mIsNext`: _Bool
- `mIsPlayerTalk`: _Bool
- `mIsSkipTerm`: _Bool
- `mIsTalkEnd`: _Bool
- `mIsTalkStart`: _Bool
- `mLastTalkCtrl`: TalkMessageCtrl *
- `mMsgCtrl`: TalkMessageCtrl *
- `mRequestCtrl`: TalkMessageCtrl *

## TalkMessageCtrl

- `mAlreadyDoneFlag`: u32
- `mAlreadyDoneFlags`: u32
- `mHostActor`: LiveActor *
- `mIsOnReadNodeAuto`: _Bool
- `mIsOnRootNodeAuto`: _Bool
- `mIsStartOnlyFront`: _Bool
- `mTalkDistance`: f32
- `mTalkStateKind`: u32
- `mZoneID`: s32

## Tamakoro

- `mAccelRate`: f32
- `mAirTime`: s32
- `mControlDisabled`: _Bool
- `mHasTutorial`: _Bool
- `mMarioOffset`: f32
- `mMarioOffsetVelocity`: f32
- `mMarioRotateYAngle`: f32
- `mTutorial`: TamakoroTutorial *

## TamakoroTutorial

- `mCanTalk`: _Bool
- `mHost`: Tamakoro *
- `mIsTrampled`: _Bool
- `mTalkCtrl`: TalkMessageCtrl *
- `mTalkCtrlAutomatic`: TalkMessageCtrl *

## Teresa

- `mAppearanceType`: s32
- `mDisplayModel`: PartsModel *
- `mHasCoin`: _Bool
- `mIsChief`: _Bool
- `mKeySwitch`: KeySwitch *
- `mTransparency`: f32
- `mWalkRange`: f32

## TeresaRacer

- `mBgmState`: s32
- `mIsGoal`: _Bool
- `mRacerId`: s32

## TicoEat

- `mStarPieceLeft`: s32
- `mStarPieceNeed`: s32

## TicoFat

- `mAbsorbSoundStep`: s32
- `mCurrentFed`: s32
- `mEatInterval`: u32
- `mEatSoundStep`: s32
- `mFlyCoord`: f32
- `mFlySpeed`: f32
- `mGuidanceId`: s32
- `mHungryTalkCtrl`: TalkMessageCtrl *
- `mIsCameraOn`: _Bool
- `mItem`: BenefitItemObj *
- `mItemType`: s32
- `mMeter`: FullnessMeter *
- `mNearStep`: s32
- `mPointingStep`: s32
- `mStarPieceLeft`: s32
- `mStarPieceNeed`: s32
- `mStartEat`: _Bool
- `mTicoSeedId`: s32

## TicoGalaxy

- `mGalaxyName`: const char *
- `mStarRing`: TicoStarRing *

## TicoRail

- `mTalkPartner`: LiveActor *

## TicoShop

- `mChoseLifeUp`: _Bool
- `mChoseOneUp`: _Bool
- `mLifeUp`: BenefitItemLifeUp *
- `mOneUp`: BenefitItemOneUp *

## TicoStarRing

- `mAppearActor`: LiveActor *
- `mGalaxyName`: const char *
- `mTicoGalaxy`: LiveActor *

## TimeAppearObj

- `mTimer`: s32

## TimeLimitLayout

- `mIsNormalDisplay`: _Bool
- `mIsSuspend`: _Bool
- `mTime`: u32
- `mTimeLimit`: u32

## TimerMoveWall

- `mMoveLength`: f32
- `mTimer`: s32

## TimerSwitch

- `mTimeLeft`: s32
- `mTimeLimit`: s32

## TombSpider

- `mNoctilucaActive`: _Bool

## TombSpiderThreadAttacher

- `mRadius`: f32
- `mZOffset`: f32

## TornadoMario

- `mIsVisible`: _Bool

## TrampleStar

- `mCaptureFrames`: s32
- `mCaptureVtx`: u16
- `mDeformRate`: f32
- `mIsLod`: _Bool
- `mJumpVel`: f32
- `mNumDrawVtxs`: u16
- `mNumFlags`: u16
- `mNumSurfaces`: u16
- `mNumVtxs`: u16
- `mSpringRate`: f32

## TrampleStarShare

- `mTrampleStar`: TrampleStar *

## TransparentWall

- `mIsSolid`: _Bool
- `mSolidForms`: s32

## Trapeze

- `mGrabCoord`: f32
- `mIsSwingBack`: _Bool
- `mIsSwingFront`: _Bool
- `mPrevSoundLvl`: s32
- `mRider`: LiveActor *
- `mRopeLength`: f32
- `mSwingReverse`: _Bool
- `mSwingVel`: f32
- `mTrapezeModel`: PartsModel *
- `mWasSwingBack`: _Bool
- `mWasSwingFront`: _Bool

## TreasureBoxCracked

- `mBoxType`: s32
- `mIsItemMoving`: _Bool
- `mItem`: LiveActor *
- `mItemType`: s32
- `mOpenCondition`: s32
- `mPowerStarId`: s32
- `mWaitForSwitchOn`: s32

## TreasureSpot

- `mIsCoinFlower`: _Bool
- `mIsGlowNear`: _Bool

## TrickRabbit

- `mFootPrint`: FootPrint *
- `mIsGhost`: _Bool
- `mIsRouteLevelControl`: _Bool
- `mNextNodeDistance`: f32
- `mPhaseSteps`: s32
- `mRouteLevel`: s32
- `mRouteLevelStep`: s32
- `mRouteLevelTarget`: s32
- `mSpotMarkLight`: SpotMarkLight *
- `mTalkMessageCtrl`: TalkMessageCtrl *

## TrickRabbitFreeRun

- `mFootPrint`: FootPrint *
- `mMsgCtrl`: TalkMessageCtrl *
- `mSpotMarkLight`: SpotMarkLight *

## TrickRabbitSnow

- `mFootPrint`: FootPrint *
- `mIsValidAppearStarPiece`: _Bool
- `mMsgCtrl`: TalkMessageCtrl *
- `mNotCaughtableTimer`: s32
- `mSpotMarkLight`: SpotMarkLight *

## TrickRabbitSnowCollect

- `mIsDemo`: _Bool
- `mMsgCtrl`: TalkMessageCtrl *
- `mPowerStarDemoModel`: ModelObj *
- `mRabbitNum`: s32
- `mTimeLimit`: s32
- `mTimerLayout`: BombTimerLayout *

## TripodBoss

- `mBgmState`: s32
- `mBodyHeight`: f32
- `mClipRadius`: f32
- `mCurrentStepSeq`: s32
- `mDemoBlendRate`: f32
- `mDummyModel`: DummyDisplayModel *
- `mDummyModelSpinSpeed`: f32
- `mIsBgmChangePending`: _Bool
- `mIsPartsHidden`: _Bool
- `mLegHeightRate`: f32
- `mLegLift`: f32
- `mLegMiddleLength`: f32
- `mLegReach`: f32
- `mLegRootLength`: f32
- `mLegs`: TripodBossLeg * (array)
- `mMovableAreaRadius`: f32
- `mNextStepSeq`: s32
- `mRideMarioState`: s32
- `mRideMarioTimer`: s32
- `mStepPoints`: TripodBossStepPoint * (array)
- `mStepSequenceNum`: s32
- `mWeightHeightOffset`: f32

## TripodBossAccesser

- `mBoss`: TripodBoss *
- `mPartsNum`: s32

## TripodBossBaseJointPosition

- `mID`: s32

## TripodBossBreakMovement

- `mDamping`: f32
- `mFlyDirRandomRate`: f32
- `mFlySpeed`: f32
- `mJointIndex`: s32

## TripodBossCoin

- `mCoin`: Coin *
- `mJointID`: s32

## TripodBossFixPartsBase

- `mBreakMovement`: TripodBossBreakMovement *
- `mClippingRadius`: f32
- `mIsActive`: _Bool
- `mJointID`: s32

## TripodBossCore

- `mBloomModel`: ModelObj *
- `mBreakModel`: ModelObj *

## TripodBossFixParts

- `mBreakKillStep`: s32
- `mBreakType`: s32
- `mHasBreakMovement`: _Bool
- `mHasCollision`: _Bool
- `mIsMarioOn`: _Bool
- `mIsPlayingWarning`: _Bool

## TripodBossGuardWall

- `mCameraTargetMtx`: CameraTargetMtx *
- `mJointID`: s32

## TripodBossGuardWallPart

- `mPlacementAngle`: f32
- `mStartTiming`: s32

## TripodBossKillerGenerater

- `mActiveLabel`: s32
- `mCanShootSettingPlayerDistance`: f32
- `mHasCollision`: _Bool
- `mIsWaitPlayerAway`: _Bool
- `mKiller`: HomingKiller *

## TripodBossKillerGeneraterCircle

- `mAngleNum`: s32
- `mCannonPitch`: f32
- `mCirclePitch`: f32
- `mGenerater`: TripodBossKillerGenerater *
- `mHeight`: f32
- `mRadius`: f32

## TripodBossKinokoOneUp

- `mJointID`: s32
- `mOneUp`: BenefitItemOneUp *

## TripodBossLeg

- `mBoss`: TripodBoss *
- `mDemoEffectTiming`: s32
- `mIsAttackHit`: _Bool
- `mIsPressPlayer`: _Bool
- `mLandingHeight`: f32
- `mPoseMode`: s32
- `mPrevStepPoint`: TripodBossStepPoint *
- `mStepPoint`: TripodBossStepPoint *

## TripodBossRailMoveParts

- `mRailMover`: MapPartsRailMover *

## TripodBossRotateParts

- `mRotator`: MapPartsRotator *

## TripodBossShell

- `mBreakModel`: ModelObj *

## TripodBossStepPoint

- `mLeg`: s32
- `mSequenceIndex`: s32
- `mSequencePriority`: s32
- `mWaitTime`: s32

## TripodBossStepStartArea

- `mSequenceNo`: s32

## Tsukidashikun

- `mMoveSpeed`: f32
- `mWaitTimer`: s32

## UFOBase

- `mIsRailDone`: _Bool
- `mModel`: ModelObj *
- `mMoveConditionType`: s32
- `mRailMover`: MapPartsRailMover *
- `mRotateSpeed`: f32

## Unizo

- `mBlinkCount`: s32
- `mBlinkFrame`: s32
- `mBreakModel`: ModelObj *
- `mFrame`: s32
- `mGravityRate`: f32
- `mJumpCount`: s32
- `mRadiusScale`: f32
- `mRollHeight`: f32
- `mRollSoundTimer`: s32
- `mSpinAnimTimer`: s32
- `mType`: s32

## UnizoLauncher

- `mUnizoNum`: s32

## ValveSwitch

- `mBindActor`: LiveActor *
- `mIsReverse`: _Bool

## WarpCube

- `mEventName`: char *
- `mInvalidateTimer`: s32

## WarpCubeMgr

- `mWarpCube`: const WarpCube *

## WarpPod

- `mArg2`: s32
- `mCameraStep`: s32
- `mCameraTime`: s32
- `mDelay`: s32
- `mDrawsPath`: _Bool
- `mEventCameraName`: char *
- `mGlowColorIndex`: s32
- `mGrandstarReq`: s32
- `mGroupId`: s32
- `mIsAppearDemoPending`: _Bool
- `mIsInactive`: _Bool
- `mIsPairCameraOn`: _Bool
- `mNumPathPoints`: u32
- `mPairCameraName`: char *
- `mPairPod`: WarpPod *
- `mPathFlagIndex`: s32
- `mPathOpenType`: s32
- `mVisibilityState`: s32

## WarpPodMgr

- `mCameraPod`: const LiveActor *
- `mNextPairIndex`: s32
- `mPods`: LiveActorGroup *

## WaterAreaHolder

- `mCamInWater`: _Bool
- `mCameraFilter`: WaterCameraFilter *
- `mNumOceanBowls`: s32
- `mNumOceanRings`: s32
- `mNumOceanSpheres`: s32
- `mNumWhirlPoolAccelerators`: s32
- `mNumWhirlPools`: s32
- `mUseBloom`: _Bool

## WaterBazooka

- `mAlreadyDoneFlag`: s32
- `mBreakModel`: ModelObj *
- `mCapsule`: WaterBazookaCapsule *
- `mDemoActor`: CameraTargetDemoActor *
- `mElectricTime`: s32
- `mHasPowerStar`: _Bool
- `mIsElectric`: _Bool
- `mIsPinchBGMStarted`: _Bool
- `mLife`: s32
- `mShooter`: MogucchiShooter *
- `mShotNum`: s32

## WaterCameraFilter

- `mWaterAlpha`: f32
- `mWavePhase`: f32

## WaterLeakPipe

- `mIceStep`: IceStepNoSlip *
- `mPipeHeight`: f32

## WaterPlant

- `mAreaRadius`: f32
- `mHalfWidth`: f32
- `mHeight`: f32
- `mIsSpin`: _Bool
- `mPlantNum`: s32
- `mType`: s32

## WaterPlantDrawInit

- `mAngleOffset`: f32

## WaterPressure

- `mIsInvalidSpinKill`: _Bool

## WaterPressureBullet

- `mHost`: LiveActor *
- `mHostActor`: LiveActor *
- `mIsGroundSlide`: _Bool
- `mIsInvalidSpinKill`: _Bool
- `mIsNoGravity`: _Bool
- `mSpeed`: f32

## WaterRoad

- `mAlpha`: u8
- `mRadius`: f32
- `mRider`: LiveActor *

## WhirlPool

- `mHeight`: f32
- `mPointCount`: s32
- `mRadius`: f32
- `mRotation`: f32
- `mTexOffsetU0`: f32
- `mTexOffsetU1`: f32
- `mTexOffsetU2`: f32
- `mTexOffsetV0`: f32
- `mTexOffsetV1`: f32
- `mTexOffsetV2`: f32

## WhirlPoolAccelerator

- `mHeight`: f32
- `mPointCount`: s32
- `mRadius`: f32
- `mRotation`: f32
- `mTexOffsetU0`: f32
- `mTexOffsetV0`: f32

## WipeFade

- `mFrame`: s32
- `mIsWipeIn`: _Bool
- `mStep`: s32

## WipeKoopa

- `mFrame`: s32

## WipeRing

- `mAnimFrame`: s32
- `mIsCenterOnPlayer`: _Bool
- `mWipeInFrame`: f32
- `mWipeOutFrame`: f32

## WoodBox

- `mBreakModel`: ModelObj *
- `mCoinCount`: s32
- `mFloorTouchTimer`: s32
- `mHasPowerStar`: _Bool
- `mHitPoint`: u16
- `mIsNoRespawn`: _Bool
- `mOneUp`: BenefitItemOneUp *
- `mPlaySolveSE`: _Bool
- `mStarBitCount`: s32
- `mStarDemoModel`: ModelObj *

## WormEatenPlanet

- `mCaterpillar`: GreenCaterpillarBig *

## YesNoLayout

- `mHasInformationMessage`: _Bool

