# Fields

4622 fields a script reads (`object.mVelocity`) or writes. Handles, text and pointers are read-only; a class has its bases' fields too.

## NameObj

- `mDrawBufferPlace`: int (s32)
- `mDrawSlot`: int (u32)
- `mExecutorIdx`: int (s32)
- `mHolderPlace`: int (s32)
- `mListPlace`: int (s32) (array)
- `mLumaClass`: int (u16)
- `mMotionEpoch`: int (u32)
- `mMovementFlag`: int (u16)
- `mName`: text (read-only)

## LiveActor

- `mFlag.mHasPose`: bool
- `mFlag.mIsCalcGravity`: bool
- `mFlag.mIsClipped`: bool
- `mFlag.mIsDead`: bool
- `mFlag.mIsDrawnClipped`: bool
- `mFlag.mIsHiddenModel`: bool
- `mFlag.mIsInvalidClipping`: bool
- `mFlag.mIsNoBind`: bool
- `mFlag.mIsNoCalcAnim`: bool
- `mFlag.mIsNoCalcView`: bool
- `mFlag.mIsStoppedAnim`: bool
- `mGravity`: vec3
- `mPosition`: vec3
- `mRotation`: vec3
- `mScale`: vec3
- `mVelocity`: vec3

## Air

- `mDistance`: float
- `mIsChanged`: int (u8)
- `mIsWaitStartCamera`: bool

## AirBubble

- `mBasePos`: vec3
- `mBobOffset`: float
- `mDriftDir`: vec3
- `mIsFollowRail`: bool
- `mIsItemMoving`: bool
- `mLife`: int (s32)
- `mRailMoveSpeed`: float

## AirBubbleGenerator

- `mBubbleLifeTime`: int (s32)
- `mWaitTime`: int (s32)

## NameObjGroup

- `mObjNum`: int (s32)
- `mObjNumMax`: int (s32)

## AllLiveActorGroup

- `mActorNum`: int (s32)

## MapObjActor

- `mBloomModel`: ModelObj (an object, read-only)
- `mIsMirrorFromModel`: int (u8)
- `mIsProjmapEveryFrame`: int (u8)
- `mModelObj`: ModelObj (an object, read-only)
- `mObjectName`: text (read-only)
- `mPlayRiddleSE`: int (u8)
- `mRailGuideDrawer`: MapPartsRailGuideDrawer (an object, read-only)
- `mRailMover`: MapPartsRailMover (an object, read-only)
- `mRailPosture`: MapPartsRailPosture (an object, read-only)
- `mRailRotator`: MapPartsRailRotator (an object, read-only)
- `mRotator`: MapPartsRotatorBase (an object, read-only)
- `mStageEffectMovingSe`: text (read-only)
- `mStageEffectName`: text (read-only)

## AnmModelObj

- `mDoneLevelSe`: text (read-only)
- `mJointPos`: vec3

## AreaObj

- `mFormType`: int (int)
- `mIsAwake`: bool
- `mIsFollowValid`: bool
- `mIsValid`: bool
- `mObjArg0`: int (s32)
- `mObjArg1`: int (s32)
- `mObjArg2`: int (s32)
- `mObjArg3`: int (s32)
- `mObjArg4`: int (s32)
- `mObjArg5`: int (s32)
- `mObjArg6`: int (s32)
- `mObjArg7`: int (s32)

## AreaObjContainer

- `mHashesShared`: bool
- `mIndex.mCount`: int (u32)
- `mIndex.mMask`: int (u32)
- `mManagers.mCount`: int (s32)

## AreaObjMgr

- `mArray.mCount`: int (s32)
- `mFirstCapacity`: int (s32)

## DynamicCollisionObj

- `mPositionNum`: int (s32)
- `mTriangleNum`: int (s32)

## AreaPolygon

- `mOffset`: float
- `mSize`: vec3

## ArrowSwitch

- `mIsPunch`: bool
- `mIsRotPlus`: bool
- `mLockAfterRotate`: bool
- `mRotAngle`: float
- `mRotSpeed`: float
- `mRotYTargetIndex`: int (s32)
- `mSwitchType`: int (s32)

## ArrowSwitchMulti

- `mActiveTargetIndex`: int (u32)
- `mIsRotPlus`: bool
- `mRotAngle`: float
- `mRotSpeed`: float
- `mRotYTargetIndex`: int (s32)
- `mTargetArray`: ArrowSwitchTarget (an object, read-only) (array)

## ArrowSwitchTarget

- `mTargetIndex`: int (s32)

## AssemblyBlock

- `mBloomModel`: PartsModel (an object, read-only)
- `mClipCenter`: vec3
- `mFloatRotateSpeed`: float
- `mIsTimerType`: bool
- `mObjArg7`: int (s32)
- `mPlayerSearchDistance`: float
- `mTimer`: int (s32)

## AstroChangeStageCube

- `mDestNo`: int (u32)
- `mWaitFrame`: int (s32)

## AstroCountDownPlate

- `mIsLightOn`: bool

## AstroDomeBlueStar

- `mCaptureRibbon`: GCaptureRibbon (an object, read-only)
- `mZoomPos`: vec3

## AstroDomeCameraController

- `mEaseRate`: float
- `mStartPos`: vec3
- `mTargetPos.mEnd`: vec3
- `mTargetPos.mStart`: vec3
- `mTargetPos.mTarget`: vec3
- `mUpPos.mEnd`: vec3
- `mUpPos.mStart`: vec3
- `mUpPos.mTarget`: vec3
- `mZeroPos`: vec3
- `mZoomPos.mEnd`: vec3
- `mZoomPos.mStart`: vec3
- `mZoomPos.mTarget`: vec3

## AstroDomeComet

- `mBloomModel`: PartsModel (an object, read-only)
- `mMiniGalaxy`: MiniatureGalaxy (an object, read-only)

## AstroDomeDemoStarter

- `mPathDrawer`: SpinDriverPathDrawer (an object, read-only)

## AstroDomeGalaxySelector

- `mBackButton`: GalaxySelectBackButton (an object, read-only)
- `mCameraController`: AstroDomeCameraController (an object, read-only)
- `mConfirmLayout`: GalaxyConfirmLayout (an object, read-only)
- `mInfo`: GalaxySelectInfo (an object, read-only)
- `mIsBackButtonPointed`: bool

## AstroDomeOrbit

- `mAngle`: float
- `mOrbitRadius`: float
- `mRingCos`: float (array)
- `mRingSin`: float (array)

## AstroDomeSky

- `mBrkFrame`: float

## SimpleEffectObj

- `mClippingCenter`: vec3
- `mObjName`: text (read-only)

## AstroEffectObj

- `mDomeId`: int (s32)

## AstroMapObj

- `mDomeId`: int (s32)
- `mEngineSound`: text (read-only)
- `mIsDomeEntrance`: bool
- `mIsDoor`: bool
- `mIsKitchenEntrance`: bool
- `mIsStarPlate`: bool
- `mNamePlate`: GalaxyNamePlate (an object, read-only)
- `mObjName`: text (read-only)
- `mRevivalSound`: text (read-only)

## AstroOverlookObj

- `mIsLayoutHidden`: int (u8)

## AstroSimpleObj

- `mNamePlate`: GalaxyNamePlate (an object, read-only)

## AudBgmConductor

- `mFadeInDelay`: int (s32)
- `mStartDelay`: int (s32)
- `mState`: int (s32)
- `mStateBits`: int (u32)
- `mStopImmediately`: bool

## AudEffectDirector

- `mAppliedType`: int (s32)
- `mEffectType`: int (s32)
- `mPriority`: int (s32)

## AudioEffectArea

- `mEffectParam`: int (s32)
- `mEffectType`: int (s32)

## LayoutActor

- `mFlag.mHudPart`: int (u8)
- `mFlag.mIsDead`: bool
- `mFlag.mIsHidden`: bool
- `mFlag.mIsOffCalcAnim`: bool
- `mFlag.mIsStopAnimFrame`: bool

## BackButton

- `mIsSelected`: bool

## BallBeamer

- `mHeadCollisionMtx.mMtx`: float (array)
- `mIsSwitchOn`: bool

## BallOpener

- `mBindVelocity`: vec3
- `mBoundSensor`: HitSensor (an object, read-only)
- `mHoleGravity`: vec3
- `mInitialVelocity`: vec3
- `mSettledFrames`: int (s32)

## BallRail

- `mAcceleration`: float
- `mBindSensor`: HitSensor (an object, read-only)
- `mBindSensorRadius`: float
- `mBindStartPos`: vec3
- `mDeceleration`: float

## Balloonfish

- `mNotBoundStep`: int (s32)
- `mRotateAngle`: float

## Banekiti

- `mRailMover`: MapPartsRailMover (an object, read-only)

## BasaBasa

- `mAttackDistance`: float
- `mAttackTarget`: vec3
- `mChaseDistance`: float
- `mChaseTurnAngle`: float
- `mClipStep`: int (s32)
- `mFront`: vec3
- `mHangModel`: ModelObj (an object, read-only)
- `mHomeDistance`: float
- `mHomeFront`: vec3
- `mHomePos`: vec3
- `mIsIceModel`: bool
- `mIsOnCeiling`: bool

## BaseMatrixFollowTargetHolder

- `mFollowers.mCount`: int (s32)
- `mTargets.mCount`: int (s32)

## BeamGoRoundBeam

- `mBloomModel`: ModelObj (an object, read-only)
- `mModelDrawer`: VolumeModelDrawer (an object, read-only)

## BeeFlowerHover

- `mClipCenter`: vec3
- `mRailMover`: MapPartsRailMover (an object, read-only)
- `mRailPosture`: MapPartsRailPosture (an object, read-only)
- `mRotateMtx.mMtx`: float (array)
- `mRotateSpeed`: float

## BegomanAttackPermitter

- `mBegoman`: BegomanBase (an object, read-only)
- `mDistToPlayer`: float
- `mHasRequest`: bool

## BegomanBase

- `mAnimScale`: vec3
- `mCanTrySetReturn`: bool
- `mElectricCounter`: int (s32)
- `mFaceVec`: vec3
- `mInitPos`: vec3
- `mInvertSideVec`: bool
- `mIsTouchElectricRail`: bool
- `mLastGravity`: vec3
- `mTargetVec`: vec3
- `mTiredCounter`: int (s32)

## BegomanBaby

- `mAppearThreeStarPiece`: bool
- `mHost`: NameObj (an object, read-only)

## PartsModel

- `mHost`: LiveActor (an object, read-only)
- `mIsCalcOwnMtx`: bool
- `mIsDead`: bool
- `mIsShownWhileHostHidden`: bool

## BegomanLauncher

- `mAppearDelay`: int (s32)
- `mBegomanCount`: int (s32)
- `mLaunchType`: int (s32)

## BegomanSpike

- `mBrokenModel`: ModelObj (an object, read-only)
- `mHead`: BegomanHead (an object, read-only)
- `mTurnAfterBlow`: bool

## BegomanSpring

- `mBaby`: BegomanBaby (an object, read-only)
- `mCameraStartPos`: vec3
- `mHead`: BegomanSpringHead (an object, read-only)
- `mHidePos`: vec3
- `mIsCameraActive`: bool
- `mIsHideType`: bool

## BenefitItemObj

- `mAppearType`: int (u8)
- `mBlendFront`: vec3
- `mCameraFrames`: int (u32)
- `mEscapeLife`: int (s32)
- `mEscapeType`: int (u8)
- `mEscapeVelocity`: vec3
- `mFallVelocity`: vec3
- `mFlashCounter`: int (s32)
- `mFlashCtrl`: FlashingCtrl (an object, read-only)
- `mFlashHideTime`: int (s32)
- `mFlashPeriod`: int (s32)
- `mHasAppearCamera`: bool
- `mHasAppearPicture`: bool
- `mHideCounter`: int (s32)
- `mHitSensorActor`: LiveActor (an object, read-only)
- `mIsActive`: bool
- `mIsDemoPending`: bool
- `mIsDisabled`: bool
- `mIsInAir`: bool
- `mIsItemMoving`: bool
- `mIsOnRail`: bool
- `mIsThrownHigh`: bool
- `mIsWaitingAppear`: bool
- `mModelMtx.mMtx`: float (array)
- `mModelName`: text (read-only)
- `mRailSpeed`: float
- `mRotateFlags`: int (u8)
- `mRotateY`: float
- `mSensorInvalidStep`: int (s32)
- `mShadowType`: int (u8)
- `mUp`: vec3

## BenefitItemInvincible

- `mFwdAxis`: vec3
- `mUpAxis`: vec3

## BgmProhibitArea

- `mIsMuteSet`: bool
- `mIsUnmuteSet`: bool
- `mPrevPlayerPos`: vec3

## BigBubble

- `mAppearRadius`: float
- `mBinderRadius`: float
- `mCoriolisAccel`: vec3
- `mDeformCoeff`: float (array)
- `mDeformSpeed`: float (array)
- `mDrawZ`: float
- `mHost`: LiveActor (an object, read-only)
- `mInterpolateRate`: float
- `mIsAttached`: bool
- `mIsBroken`: bool
- `mIsExitLimitter`: bool
- `mIsHidden`: bool
- `mIsObstruct`: bool
- `mIsShrinkable`: bool
- `mMergeBubble`: BigBubble (an object, read-only)
- `mMergeBubbles`: BigBubble (an object, read-only) (array)
- `mMergeIndex`: int (s32)
- `mMoment`: vec3
- `mMoveLimitter`: BigBubbleMoveLimitter (an object, read-only)
- `mPointAccel`: float
- `mPointerPos`: vec3
- `mPushDirection`: vec3
- `mPushInvalidTime`: int (s32)
- `mReduceVolumeTimer`: int (s32)
- `mRider`: LiveActor (an object, read-only)
- `mRiderBasePos`: vec3
- `mRiderPos`: vec3
- `mShapeType`: int (s32)
- `mSpawnPosition`: vec3
- `mVolume`: float

## CubeCameraArea

- `mCategoryMask`: int (s32)
- `mZoneID`: int (u32)

## BigBubbleGenerator

- `mAttachedBubble`: BigBubble (an object, read-only)
- `mIsObstruct`: bool
- `mLimitterID`: int (s32)
- `mMaxSize`: float
- `mMinSize`: float
- `mShape`: int (s32)
- `mSpawnTimer`: int (s32)

## BigBubbleGoalArea

- `mMeterAmount`: float
- `mMeterCapacity`: float
- `mMeterDisplayAmount`: float
- `mTrans`: vec3

## BigBubbleHolder

- `mBubbles.mCount`: int (s32)
- `mDrawBubbles.mCount`: int (s32)
- `mIsMarioBinded`: bool
- `mLimitters.mArr`: BigBubbleMoveLimitter (an object, read-only) (array)
- `mLimitters.mSize`: int (s32)
- `mNumBubbles`: int (s32)
- `mNumLimitters`: int (s32)

## BigBubbleMoveLimitter

- `mAxis`: vec3
- `mCylinderHeight`: float
- `mCylinderRadius`: float
- `mFront`: vec3
- `mID`: int (s32)
- `mLimitMarginType`: int (s32)
- `mPlaneHeight`: float
- `mPlaneWidth`: float
- `mPosition`: vec3
- `mSide`: vec3
- `mType`: int (s32)
- `mUp`: vec3

## BigFan

- `mClippingCenter`: vec3
- `mIsTeresaGalaxy`: bool
- `mWindLength`: float
- `mWindModel`: ModelObj (an object, read-only)
- `mWindSpeed`: float

## Birikyu

- `mCircleAngle`: float
- `mCircleAxis`: vec3
- `mCircleCenter`: vec3
- `mCircleFront`: vec3
- `mClipCenter`: vec3
- `mHasRail`: bool
- `mHasShadow`: bool
- `mObjName`: text (read-only)
- `mSpeed`: float

## BirikyuWithFace

- `mIsFaceReversed`: bool

## BlackHole

- `mBlackHoleModel`: ModelObj (an object, read-only)
- `mModelScale`: float
- `mRadius`: float

## ImageEffectArea

- `mEffectType`: int (s32)

## BloomArea

- `mIntensity`: int (u8)
- `mIntensity1`: int (s32)
- `mIntensity2`: int (s32)
- `mThreshold`: int (u8)

## ImageEffectBase

- `mFadeLevel`: float
- `mIsOn`: bool
- `mIsShown`: bool

## BloomEffect

- `mBloomIntensity`: int (u8)
- `mIntensity1`: float
- `mIntensity2`: float
- `mThreshold`: int (u8)

## BloomEffectSimple

- `mIntensity`: float
- `mMaskFilter`: int (s32)
- `mThreshold`: int (u8)

## ChipBase

- `mAirBubble`: PartsModel (an object, read-only)
- `mChipName`: text (read-only)
- `mChipType`: int (s32)
- `mClippingRange`: vec3
- `mFlashingCtrl`: FlashingCtrl (an object, read-only)
- `mGroupID`: int (s32)
- `mHost`: LiveActor (an object, read-only)
- `mIsCalcShadow`: bool
- `mRailMover`: MapPartsRailMover (an object, read-only)

## ChipGroup

- `mAlreadyDoneFlag`: int (s32)
- `mChipType`: int (s32)
- `mChips.mSize`: int (s32)
- `mCounterTimer`: int (s32)
- `mFlashFrame`: int (s32)
- `mGotCount`: int (s32)
- `mIsCounterVisible`: int (u8)
- `mIsDeactivated`: int (u32)
- `mIsListeningAppear`: int (u8)
- `mStageSwitchArg`: int (s32)
- `mTotalCount`: int (s32)
- `mUIPos`: vec3
- `mUIRange`: float
- `mUIRangeFixed`: float

## BlueStarCupsulePlanet

- `mIsStepUpdatable`: bool
- `mName`: text (read-only)
- `mRailMover`: MapPartsRailMover (an object, read-only)
- `mRotator`: MapPartsRotator (an object, read-only)

## BombBird

- `mBombs.mArr`: BombBirdBomb (an object, read-only) (array)
- `mBombs.mMovedSize`: int (s32)
- `mFront`: vec3
- `mHeldBomb`: BombBirdBomb (an object, read-only)
- `mHoldBomb`: bool
- `mSearchPlayer`: bool

## BombBirdBomb

- `mRotateMoment`: vec3

## BombHei

- `mBinderOffset`: vec3
- `mCarrySensor`: HitSensor (an object, read-only)
- `mCountdown`: int (s32)
- `mExplodeRadius`: float
- `mExplosionIgnoreCollision`: bool
- `mFront`: vec3
- `mFuseTime`: int (s32)
- `mTargetFront`: vec3

## BombHeiLauncher

- `mBomb`: BombHei (an object, read-only)
- `mCountdown`: int (s32)
- `mIsActive`: bool

## BombTeresa

- `mDisableRespawning`: bool
- `mDriftPullSpeed`: float
- `mDriftSpinSpeed`: float
- `mDriftStep`: int (s32)
- `mFront`: vec3
- `mInitPos`: vec3
- `mIsDriftRelease`: bool
- `mIsWanderAfterWait`: bool
- `mTonguePos`: vec3
- `mTongueRate`: float

## BombTimerLayout

- `mDangerTransFrame`: int (u32)
- `mFrame`: int (u32)
- `mIsSuspend`: bool
- `mTimeLimit`: int (u32)

## BossAccessor

- `mBoss`: LiveActor (an object, read-only)

## BossBegoman

- `mBabyFollowerNum`: int (s32)
- `mHead`: BossBegomanHead (an object, read-only)
- `mHealth`: int (s32)
- `mPursueSpeedRate`: float
- `mSpikeFollowerNum`: int (s32)

## BossBegomanHead

- `mAngle`: float

## BossKameck

- `mFront`: vec3
- `mKameckHolder`: KameckHolder (an object, read-only)
- `mLastVelocity`: vec3
- `mModelName`: text (read-only)
- `mMoveRailNum`: int (s32)
- `mObjArg7`: int (s32)

## BossKameckBarrier

- `mModelName`: text (read-only)

## BossKameckMoveRail

- `mRailNo`: int (s32)
- `mStoppablePointNum`: int (s32)

## BossStinkBug

- `mBombHolder`: BossStinkBugBombHolder (an object, read-only)
- `mBombLauncher`: PartsModel (an object, read-only)
- `mCameraRegisterId`: int (s32)
- `mCameraRegisterPos`: vec3
- `mFront`: vec3
- `mIsLegBound`: bool
- `mIsPoseUpdated`: bool
- `mLegFriction`: float
- `mLegIKRate`: float
- `mWingModel`: PartsModel (an object, read-only)

## BreakableCage

- `mBreakModel`: ModelObj (an object, read-only)
- `mDelayDeadActivate`: bool
- `mIgnoreGravity`: bool
- `mItemModel`: DummyDisplayModel (an object, read-only)
- `mRotationSpeed`: float

## BrightObj

- `mRadius`: float

## BrightSun

- `mSun`: Sun (an object, read-only)

## BrosButton

- `mIsSelectedMario`: bool

## NPCActor

- `mGoods0`: PartsModel (an object, read-only)
- `mGoods1`: PartsModel (an object, read-only)
- `mInitPos`: vec3
- `mIsJetTurtleHit`: int (u8)
- `mIsPointed`: int (u8)
- `mIsPointingStart`: int (u8)
- `mIsRailOnGround`: int (u8)
- `mIsReaction`: int (u8)
- `mIsReactionStart`: int (u8)
- `mIsSpinAttacked`: int (u8)
- `mIsSpinAttackedStart`: int (u8)
- `mIsTrampled`: int (u8)
- `mIsTrampledStart`: int (u8)
- `mMoveAction`: text (read-only)
- `mMoveTalkAction`: text (read-only)
- `mMoveTalkBckRate`: float
- `mMsgCtrl`: TalkMessageCtrl (an object, read-only)
- `mParam.mNearDistance`: float
- `mParam.mTalkAction`: text (read-only)
- `mParam.mTalkTurnAction`: text (read-only)
- `mParam.mTiltMaxAngle`: float
- `mParam.mTiltSpeed`: float
- `mParam.mTurnOnTalk`: int (u8)
- `mParam.mTurnSpeed`: float
- `mParam.mTurnWhenNear`: int (u8)
- `mParam.mWaitAction`: text (read-only)
- `mParam.mWaitTurnAction`: text (read-only)
- `mPointingAction`: text (read-only)
- `mPointingSeStep`: int (s32)
- `mPoseRotation`: vec3
- `mRailAccel`: float
- `mRailPoseRate`: float
- `mRailSpeed`: float
- `mReactionAction`: text (read-only)
- `mSpinAction`: text (read-only)
- `mSwingReactDist`: float
- `mTrampleAction`: text (read-only)
- `mTrampleCoolDown`: int (s32)
- `mUseReactionActions`: int (u8)
- `mWasPointed`: int (u8)
- `mWasReaction`: int (u8)
- `mWasSpinAttacked`: int (u8)
- `mWasTrampled`: int (u8)

## Butler

- `mIsAstroDome`: bool
- `mIsPointed`: bool
- `mIsStarPieceLecture`: bool
- `mReplaceStep`: int (s32)
- `mStarPieceHitCount`: int (s32)

## ButlerMap

- `mObjArg0`: bool

## Butterfly

- `mColorIndex`: int (s32)
- `mHomePos`: vec3
- `mPerchTarget`: HitSensor (an object, read-only)
- `mStarPieceAppeared`: bool

## Camera

- `mCameraMan`: CameraMan (an object, read-only)
- `mIsLOfsErpOff`: bool
- `mVPan`: CameraHeightArrange (an object, read-only)

## CameraAnim

- `mCurrentFrame`: float
- `mIsPaused`: bool
- `mNrFrames`: int (u32)
- `mShownFrame`: float
- `mSpeed`: float

## CameraBehind

- `mAngleX`: float
- `mAngleY`: float
- `mDist`: float
- `mRate`: float

## CameraBlackHole

- `mBasePos`: vec3
- `mFovy`: float
- `mRoll`: float
- `mWPoint`: vec3

## CameraCharmedFix

- `mBasePos`: vec3
- `mUp`: vec3
- `mWPoint`: vec3

## CameraCharmedTripodBoss

- `mAngleX`: float
- `mAngleY`: float
- `mJointId`: int (s32)
- `mUp`: vec3
- `mWPoint`: vec3

## CameraCharmedVecReg

- `mAngleX`: float
- `mAngleYMax`: float
- `mAngleYMin`: float
- `mCamAngleRate`: float
- `mCamDistRatio`: float
- `mDist`: float
- `mName`: text (read-only)

## CameraCharmedVecRegTower

- `mAngleX`: float
- `mAngleYMax`: float
- `mArrange`: bool
- `mAxis`: vec3
- `mDist`: float
- `mName`: text (read-only)
- `mWPoint`: vec3

## CameraContext

- `mFarZ`: float
- `mFovy`: float
- `mNearZ`: float
- `mShakeOffset.x`: float
- `mShakeOffset.y`: float

## CameraCover

- `mActor`: CaptureScreenActor (an object, read-only)
- `mCoverFrames`: int (u32)
- `mCoverFramesLeft`: int (s32)
- `mIsCoverRequested`: bool

## CameraCubePlanet

- `mAngleX`: float
- `mAngleY`: float
- `mDist`: float
- `mUp`: vec3

## CameraDead

- `mCameraType`: int (s32)
- `mDeadFrame`: int (s32)
- `mDeadTime`: int (s32)
- `mDist`: float
- `mFovy`: float

## CameraDirector

- `mCameraManEvent`: CameraManEvent (an object, read-only)
- `mCameraManGame`: CameraManGame (an object, read-only)
- `mCameraManSubjective`: CameraManSubjective (an object, read-only)
- `mCameraTargetMtx`: CameraTargetMtx (an object, read-only)
- `mChunkHolder`: CameraParamChunkHolder (an object, read-only)
- `mCover`: CameraCover (an object, read-only)
- `mEventNum`: int (u32)
- `mEvents.mSize`: int (s32)
- `mHolder`: CameraHolder (an object, read-only)
- `mIsCameraNG`: bool
- `mIsStartCameraActive`: bool
- `mIsStartSubjectiveCamera`: bool
- `mIsSubjectiveCalced`: bool
- `mIsSubjectiveCamera`: bool
- `mNearZ`: float
- `mOnlyCamera`: OnlyCamera (an object, read-only)
- `mRailHolder`: CameraRailHolder (an object, read-only)
- `mRegisterHolder`: CameraRegisterHolder (an object, read-only)
- `mRequestCameraLocalOffsetReset`: bool
- `mRequestCameraManReset`: bool
- `mShaker`: CameraShaker (an object, read-only)
- `mStartCameraCreated`: bool
- `mStartTime`: int (s32)
- `mSubjectiveFrame`: int (s32)
- `mTargetObj`: CameraTargetObj (an object, read-only)

## CameraFix

- `mAxis`: vec3
- `mTarget`: CameraTargetMtx (an object, read-only)
- `mUp`: vec3
- `mWPoint`: vec3

## CameraFixedPoint

- `mCameraType`: int (u32)
- `mPos`: vec3

## CameraFixedThere

- `mAxis`: vec3
- `mCameraType`: int (u32)
- `mIsFovyFixed`: bool
- `mRoll`: float
- `mUp`: vec3

## CameraFollow

- `mAngleXMin`: float
- `mAngleXRange`: float
- `mAngleXSpeed`: float
- `mAngleYRoundSpeed`: float
- `mAngleYSpeed`: float
- `mAngleYTarget`: float
- `mCheckWall`: bool
- `mDistMax`: float
- `mDistMin`: float
- `mIsRoundLeftValid`: bool
- `mIsRoundRightValid`: bool
- `mIsRounding`: bool
- `mPos`: vec3
- `mWatchPos`: vec3

## CameraFooFighter

- `mAngleXRate`: float
- `mAngleXRateMin`: float
- `mCollideCount`: int (s32)
- `mDistMax`: float
- `mDistMin`: float
- `mTargetLastMoveDir`: vec3

## CameraFooFighterPlanet

- `mDistMax`: float
- `mDistMin`: float
- `mPitchMax`: float

## CameraFrontAndBack

- `mAxis`: vec3
- `mDist`: float
- `mMaxDipAngle`: float
- `mRotateRate`: float
- `mWPoint`: vec3

## CameraGround

- `mAngleA`: float
- `mAngleB`: float
- `mDist`: float
- `mIsNotInitialized`: bool
- `mLastWatchPoint`: vec3
- `mUp`: vec3

## CameraHeightArrange

- `mCamera`: Camera (an object, read-only)
- `mCameraPosHeight`: float
- `mCameraPosOffset`: float
- `mCameraTargetPosHeight`: float
- `mCameraWatchPosBaseHeight`: float
- `mCameraWatchPosHeight`: float
- `mCameraWatchPosOffset`: float
- `mChaseTime`: int (s32)
- `mDropDelay`: int (s32)
- `mDropTime`: int (s32)
- `mFocalScaleLower`: float
- `mFocalScaleUpper`: float
- `mGlobalAxis`: vec3
- `mGndInt`: int (s32)
- `mIsCatchup`: bool
- `mIsJumping`: bool
- `mIsStartJump`: bool
- `mJumpState`: int (u32)
- `mMaxRiseEaseTime`: int (s32)
- `mOffsetScale`: float
- `mOffsetScaleMax`: float
- `mPosOffsetMinDropLag`: float
- `mPosOffsetMinRiseLag`: float
- `mRiseDelay`: int (s32)
- `mRiseEaseTime`: int (s32)
- `mRiseTime`: int (s32)
- `mStateTime`: int (s32)
- `mUpdateGlobalAxis`: bool
- `mVPanAxis`: vec3
- `mVPanUse`: bool

## CameraHolder

- `mDefaultCameraIndex`: int (s32)
- `mIndex.mCount`: int (u32)
- `mIndex.mMask`: int (u32)

## CameraInfo

- `mCanSkipAppearAnim`: bool
- `mIsActive`: bool
- `mStep`: int (s32)

## CameraInnerCylinder

- `mAngleA`: float
- `mAngleB`: float
- `mAxis`: vec3
- `mDist`: float
- `mIsNotInitialized`: bool
- `mLastWatchPoint`: vec3
- `mSideDist`: float
- `mWPoint`: vec3

## CameraInwardSphere

- `mDistBase`: float
- `mDistMax`: float
- `mDistMin`: float
- `mGravityCenter`: vec3
- `mPos`: vec3
- `mVelocity`: vec3

## CameraInwardTower

- `mAngleMax`: float
- `mAngleX`: float
- `mAngleY`: float
- `mAxis`: vec3
- `mDist`: float
- `mIsResetting`: bool
- `mPos`: vec3
- `mWPoint`: vec3

## CameraMan

- `mDirector`: CameraDirector (an object, read-only)
- `mIsActivated`: bool
- `mRequestLOfsReset`: bool

## CameraManEvent

- `mCamera`: Camera (an object, read-only)
- `mChunkHolder`: CameraParamChunkHolder (an object, read-only)
- `mHolder`: CameraHolder (an object, read-only)
- `mRequestReset`: bool

## CameraManGame

- `mCamera`: Camera (an object, read-only)
- `mChunkHolder`: CameraParamChunkHolder (an object, read-only)
- `mHolder`: CameraHolder (an object, read-only)
- `mInterpolateOffCounter`: int (s32)
- `mIsStartPosActive`: bool
- `mRequestReset`: bool
- `mTypeState`: int (u32)
- `mZoomedIn`: bool

## CameraManSubjective

- `mCamera`: CameraSubjective (an object, read-only)

## CameraMedianPlanet

- `mAngleConstrainRate`: float
- `mAngleMin`: float
- `mDist`: float
- `mDistMax`: float
- `mDistMin`: float
- `mPartialRate`: float
- `mPeepAngle`: float
- `mString`: text (read-only)

## CameraMedianTower

- `mAngleA`: float
- `mAxis`: vec3
- `mBaseFront`: vec3
- `mDist`: float
- `mInterpolateTime`: int (s32)
- `mIsResetting`: bool
- `mMaxFocusDist`: float
- `mPartialRate`: float
- `mString`: text (read-only)
- `mWPoint`: vec3

## CameraMtxRegParallel

- `mAngleX`: float
- `mAngleY`: float
- `mDist`: float
- `mString`: text (read-only)
- `mWPoint`: vec3

## CameraObjParallel

- `mAngleX`: float
- `mAngleY`: float
- `mDist`: float

## CameraParallel

- `mAngleX`: float
- `mAngleY`: float
- `mDist`: float
- `mIsEnableValid`: bool
- `mIsRounding`: bool
- `mRoundAngle`: float
- `mRoundTarget`: float

## CameraParamChunkHolder

- `mCameraHolder`: CameraHolder (an object, read-only)
- `mCameraVersion`: int (u32)
- `mChunkCapacity`: int (s32)
- `mIsSorted`: bool
- `mNrChunks`: int (s32)

## CameraPointer

- `mAngleX`: float
- `mAngleXRange`: float
- `mAngleYMin`: float
- `mAngleYRange`: float
- `mBlendFriction`: float
- `mBlendPointPos.x`: float
- `mBlendPointPos.y`: float
- `mCameraType`: int (s32)
- `mDist`: float
- `mFovy`: float
- `mPointerDistance`: float
- `mRealPointPos.x`: float
- `mRealPointPos.y`: float
- `mResetPointingPos`: bool
- `mSpeedBlendRate`: float

## CameraRaceFollow

- `mAngleX`: float
- `mDistMax`: float
- `mDistMin`: float
- `mIsForcePitch`: bool
- `mRoundAngleSpeed`: float

## CameraRailDemo

- `mCalcType`: int (s32)
- `mCoord`: float
- `mDampRatio`: float
- `mDemoTime`: int (s32)
- `mDemoTimer`: int (s32)

## CameraRailFollow

- `mMarginScale`: int (s32)
- `mRailAccel`: float
- `mRailCoord`: float
- `mRailSpeed`: float
- `mRailSpeedMax`: float
- `mTargetMargin`: float

## CameraRailWatch

- `mAngleX`: float
- `mDirection`: int (s32)
- `mDist`: float
- `mRailCoordOffset`: float
- `mSetDirection`: int (s32)

## CameraRegisterHolder

- `mDummyFloat`: float
- `mDummyMatrix.mMtx`: float (array)
- `mDummyVector`: vec3
- `mFloats.mCapacity`: int (u32)
- `mFloats.mCount`: int (u32)
- `mMatrices.mCapacity`: int (u32)
- `mMatrices.mCount`: int (u32)
- `mVectors.mCapacity`: int (u32)
- `mVectors.mCount`: int (u32)

## CameraShaker

- `mInfinity.mCount`: int (s32)

## CameraSlide

- `mAngleA`: float
- `mAxis`: vec3
- `mDist`: float
- `mUp`: vec3
- `mWPoint`: vec3

## CameraSpiral

- `mAngleEnd`: float
- `mAngleStart`: float
- `mDistEnd`: float
- `mDistStart`: float
- `mEaseType`: int (s32)
- `mEndTime`: int (s32)
- `mOffsetYEnd`: float
- `mOffsetYStart`: float
- `mStartTime`: int (s32)
- `mTimer`: int (s32)

## CameraSubjective

- `mAngle.x`: float
- `mAngle.y`: float
- `mAngleVel.x`: float
- `mAngleVel.y`: float

## CameraTalk

- `mDist`: float
- `mHeight`: float
- `mPos`: vec3
- `mUp`: vec3

## CameraTargetActor

- `mActor`: LiveActor (an object, read-only)
- `mCameraArea`: CubeCameraArea (an object, read-only)
- `mFront`: vec3
- `mSide`: vec3
- `mUp`: vec3

## CameraTargetMtx

- `mCameraArea`: CubeCameraArea (an object, read-only)
- `mFront`: vec3
- `mGravityVector`: vec3
- `mInvalidLastMove`: bool
- `mLastMove`: vec3
- `mPosition`: vec3
- `mSide`: vec3
- `mUp`: vec3

## CameraTargetPlayer

- `mActor`: MarioActor (an object, read-only)
- `mCameraArea`: CubeCameraArea (an object, read-only)
- `mFront`: vec3
- `mGravity`: vec3
- `mGroundPos`: vec3
- `mIsPlayerMoving`: bool
- `mPlayerMovementTimer`: int (s32)
- `mSide`: vec3
- `mUp`: vec3

## CameraTower

- `mAngleX`: float
- `mAngleYRoundSpeed`: float
- `mAngleYSpeed`: float
- `mAxis`: vec3
- `mDist`: float
- `mIsRoundLeftValid`: bool
- `mIsRoundRightValid`: bool
- `mIsRounding`: bool
- `mPos`: vec3
- `mRoundIntervalNum`: int (s32)
- `mWPoint`: vec3

## CameraTowerPos

- `mAngleX`: float
- `mAngleXRate`: float
- `mAngleYMaxSpeed`: float
- `mAxis`: vec3
- `mDist`: float
- `mPos`: vec3
- `mWPoint`: vec3

## CameraTripodBoss

- `mAngleY`: float

## CameraTripodBossJoint

- `mAngleX`: float
- `mAngleY`: float
- `mAxis`: vec3
- `mDist`: float
- `mIsRounding`: bool
- `mJointID`: int (s32)
- `mRoundAngle`: float
- `mRoundTarget`: float

## CameraTripodPlanet

- `mAngleX`: float
- `mAngleY`: float
- `mAxis`: vec3
- `mDist`: float
- `mWPoint`: vec3

## CameraTrundle

- `mAngleX`: float
- `mAngleY`: float
- `mAxis`: vec3
- `mDist`: float
- `mDotMax`: float
- `mWPoint`: vec3

## CameraTwistedPassage

- `mDistMax`: float
- `mDistMin`: float
- `mIsReversed`: bool

## CameraWaterFollow

- `mCollideCount`: int (s32)
- `mDistMax`: float
- `mDistMin`: float
- `mIsRounding`: bool
- `mLastMoveDir`: vec3
- `mRoundingFrame`: int (s32)
- `mSide`: vec3
- `mSideBlendRate`: float
- `mSideBlendRateMin`: float

## CameraWaterPlanet

- `mAngleX`: float
- `mDistMax`: float
- `mDistMin`: float

## CameraWaterPlanetBoss

- `mAngleXMax`: float
- `mAngleXMin`: float
- `mCollideCount`: int (s32)
- `mDistMax`: float
- `mDistMin`: float
- `mDotFrontMax`: float
- `mDotFrontMin`: float
- `mIsBlendUpEnabled`: bool
- `mIsRounding`: bool
- `mLastMoveDir`: vec3
- `mRoundingFrame`: int (s32)
- `mSide`: vec3
- `mSideBlendRate`: float
- `mSideBlendRateMin`: float
- `mWPoint`: vec3

## CameraWonderPlanet

- `mAngleX`: float
- `mDistMax`: float
- `mDistMin`: float
- `mIsRotating`: bool
- `mRotateFrame`: int (s32)

## Candlestand

- `mHasItemAppear`: bool
- `mItem`: int (s32)

## CaptureScreenActor

- `mCameraName`: text (read-only)

## CaptureScreenDirector

- `mIsCaptureRequested`: bool
- `mRequestedTiming`: text (read-only)
- `mTimingType`: text (read-only)

## Caretaker

- `mBombTimerLayout`: BombTimerLayout (an object, read-only)
- `mTidyTimeLimit`: int (s32)

## CenterScreenBlur

- `mAlpha`: int (u8)
- `mBlendRate`: float
- `mFadeIn`: int (s32)
- `mFadeOut`: int (s32)
- `mOffset`: float
- `mTime`: int (s32)

## ChangeBgmCube

- `mIsInside`: int (u8)

## ChipCounter

- `mCollectCounter`: CollectCounter (an object, read-only)
- `mCount`: int (s32)
- `mGroupId`: int (s32)
- `mShowRate`: float
- `mType`: int (s32)

## ChipHolder

- `mChipCounter`: ChipCounter (an object, read-only)
- `mChipGroups.mArr`: ChipGroup (an object, read-only) (array)
- `mChipGroups.mSize`: int (s32)
- `mChipType`: int (s32)
- `mNumChipGroups`: int (s32)

## ChooChooTrain

- `mClipCenter`: vec3
- `mModelArray.mCount`: int (s32)
- `mRailSpeed`: float
- `mWhistleTimer`: int (s32)

## CinemaFrame

- `mStartAt`: float

## CoinGroup

- `mCoinCount`: int (u32)
- `mIsPurpleCoinGroup`: bool
- `mTimeLimit`: int (s32)

## CircleCoinGroup

- `mCoinRadius`: float

## ClipAreaDrop

- `mBaseSize`: float

## ClipAreaDropLaser

- `mDrawCount`: int (int)
- `mNumPointsToDraw`: float
- `mPointIndexToSkipDraw`: int (int)
- `mPoints`: vec3 (array)
- `mSpeed`: float

## ClipAreaHolder

- `mIsActive`: bool

## ClipAreaMovable

- `mMapPartsRailMover`: MapPartsRailMover (an object, read-only)
- `mMapPartsRotator`: MapPartsRotator (an object, read-only)

## ClipFieldFillDraw

- `mFillBlendOp`: int (u8)
- `mFillDstFactor`: int (u8)
- `mFillInsideVolume`: bool
- `mFillSrcFactor`: int (u8)

## MapParts

- `mClipCenter`: vec3

## GeneralMapParts

- `mAppearController`: MapPartsAppearController (an object, read-only)
- `mFunctionArray.mCount`: int (s32)
- `mGuideDrawer`: MapPartsRailGuideDrawer (an object, read-only)
- `mIsCameraEnded`: int (u8)
- `mIsPaused`: int (u8)
- `mMoveConditionType`: int (s32)
- `mRailMover`: MapPartsRailMover (an object, read-only)
- `mRailPosture`: MapPartsRailPosture (an object, read-only)
- `mRailRotator`: MapPartsRailRotator (an object, read-only)
- `mRotator`: MapPartsRotator (an object, read-only)
- `mShadowType`: int (s32)
- `mSignMotionType`: int (s32)

## ClippingActorHolder

- `mActorNum`: int (s32)
- `mPlanetNum`: int (s32)
- `mTableMask`: int (u32)

## ClippingDirector

- `mActorHolder`: ClippingActorHolder (an object, read-only)
- `mGroupHolder`: ClippingGroupHolder (an object, read-only)
- `mJudge`: ClippingJudge (an object, read-only)

## ClippingGroupHolder

- `mMaxGroups`: int (s32)
- `mNumGroups`: int (s32)

## ClippingInfoGroup

- `mCount`: int (s32)
- `mInfos.mRoom`: int (u32)
- `mIsClipped`: bool

## CocoNut

- `mAreaVelocity`: vec3
- `mContinueRolling`: bool
- `mFallSpeed`: float
- `mFront`: vec3
- `mIsBindOff`: bool
- `mIsGroupClipping`: bool
- `mIsKicked`: bool
- `mIsOnMoveArea`: bool
- `mMoveSpeed`: float
- `mOffGroundStep`: int (s32)
- `mRadius`: float
- `mRespawnWhenOutOfView`: bool
- `mSpawnPosition`: vec3
- `mSphericalShadow`: bool

## CocoNutBall

- `mFreezePos`: vec3
- `mFreezeStep`: int (s32)
- `mFreezeVelocity`: vec3
- `mHeightMax`: float
- `mHeightMin`: float
- `mHitBackGravity`: vec3
- `mHost`: LiveActor (an object, read-only)
- `mIsHeightLimited`: bool
- `mIsRallyBgm`: bool
- `mIsThrowRight`: bool
- `mKillDistance`: float
- `mRallyNum`: int (s32)
- `mUp`: vec3

## CocoSambo

- `mFrontVec`: vec3
- `mHead`: CocoSamboHead (an object, read-only)
- `mHitEffectScale`: vec3
- `mPlayerSearchDistance`: float
- `mUpVec`: vec3

## CocoSamboHead

- `mFrontVec`: vec3

## CoconutTreeLeaf

- `mAxisX`: vec3
- `mAxisY`: vec3
- `mAxisZ`: vec3
- `mGroup`: CoconutTreeLeafGroup (an object, read-only)
- `mRestTipPosition`: vec3
- `mRestUp`: vec3
- `mTipPosition`: vec3
- `mWindAccel`: vec3
- `mWindStartStep`: int (s32)
- `mWindStep`: int (s32)

## CoconutTreeLeafGroup

- `mDistanceToPlayer`: float
- `mNumLeaves`: int (s32)
- `mWindDirection`: vec3

## Coin

- `mAirBubble`: PartsModel (an object, read-only)
- `mCannotTime`: int (s32)
- `mClippingRange`: vec3
- `mDropPosition`: vec3
- `mFlashCtrl`: FlashingCtrl (an object, read-only)
- `mIsCalcShadowAlways`: bool
- `mIsInWater`: bool
- `mIsNeedBubble`: bool
- `mIsPoseFromRotation`: bool
- `mIsPurpleCoin`: bool
- `mLife`: int (int)

## CoinCounter

- `mCoinDisplayNum`: int (s32)
- `mCoinNum`: int (s32)
- `mFollowPos.x`: float
- `mFollowPos.y`: float
- `mInvalidCountUpFrame`: int (s32)
- `mIsForceAppear`: bool

## CoinHolder

- `mHostInfos.mCount`: int (s32)

## CoinReplica

- `mCoin`: Coin (an object, read-only)

## CoinRotater

- `mHiSpeedRotateY`: float
- `mHiSpeedRotateYMtx.mMtx`: float (array)
- `mRotateY`: float
- `mRotateYMtx.mMtx`: float (array)
- `mWaterRotateMtx.mMtx`: float (array)
- `mWaterRotateY`: float

## CoinSpot

- `mCoinCount`: int (s32)
- `mIsInTornado`: bool
- `mSpawnTimer`: int (s32)
- `mSpawnedCount`: int (s32)

## CollapsePlane

- `mCollapseStep`: int (s32)
- `mCollapseTime`: int (s32)

## CollectCounter

- `mFollowPosition.x`: float
- `mFollowPosition.y`: float

## CollectTico

- `mIsDemoCast`: int (u8)
- `mStarPos`: vec3
- `mTicoNum`: int (s32)

## CollisionArea

- `mCenter`: vec3
- `mHitRadius`: int (s32)
- `mInsideDepth`: float
- `mIsTeresaPass`: bool
- `mIsValid`: bool
- `mOutsideAxisCount`: int (u32)
- `mPolygon`: AreaPolygon (an object, read-only)
- `mPushType`: int (s32)
- `mSurfaceMask`: int (s32)
- `mTeresaType`: int (s32)

## CollisionCategorizedKeeper

- `mCategory`: int (s32)
- `mHitInfoRoom`: int (s32)
- `mNumHits`: int (s32)
- `mPartsCount`: int (s32)
- `mUpdateZoneBounds`: bool
- `mZones.mCount`: int (s32)
- `mZonesInitialized`: bool

## CollisionDirector

- `mCategoryKeeper`: CollisionCategorizedKeeper (an object, read-only) (array)

## CollisionShadow

- `mCaptureOn`: int (u8)
- `mCenter`: vec3
- `mCheckOffset`: float
- `mDirection`: vec3
- `mDrawFlags`: int (u16)
- `mDrawOffset`: float
- `mEdgePolygonCount`: int (u32)
- `mLength`: float
- `mListCapacity`: int (u32)
- `mMode`: int (u8)
- `mPolygonCount`: int (u32)
- `mProjCenter`: vec3
- `mProjJumps`: int (u32)
- `mProjSide`: vec3
- `mProjUp`: vec3
- `mRadius`: float
- `mReferencePos`: vec3
- `mSnapDirection`: bool
- `mUp`: vec3
- `mUpdate`: int (u8)
- `mVertexCount`: int (u32)
- `mVertsPerPolygon`: int (u16)
- `mViewDone`: int (u8)
- `mVolumeHeight`: float
- `mVolumeUp`: vec3
- `mVolumeWidth`: float

## CometEventExecutorTimeLimit

- `mTimeLimit`: int (u32)
- `mTimeLimitLayout`: TimeLimitLayout (an object, read-only)

## CometRetryButton

- `mPlayerMissLeft`: PlayerMissLeft (an object, read-only)

## CounterLayoutController

- `mCoinCounter`: CoinCounter (an object, read-only)
- `mHPMeter`: MarioMeter (an object, read-only)
- `mIsTicoEatRequested`: bool
- `mPlayerLeft`: PlayerLeft (an object, read-only)
- `mPlayerNotMovingFrame`: int (s32)
- `mStarCounter`: StarCounter (an object, read-only)
- `mStarPieceCounter`: StarPieceCounter (an object, read-only)

## Creeper

- `mBendDirection`: vec3
- `mBendRate`: float
- `mBendRateBee`: float
- `mClippingCenter`: vec3
- `mCoord`: float
- `mFlowerModel`: PartsModel (an object, read-only)
- `mFront`: vec3
- `mHangSpeed`: float
- `mJumpSpeedFront`: float
- `mJumpSpeedUp`: float
- `mLeafModel`: PartsModel (an object, read-only)
- `mNumPoints`: int (s32)
- `mRider`: LiveActor (an object, read-only)
- `mUp`: vec3

## CrystalCage

- `mBasePos`: vec3
- `mBinderOffset`: vec3
- `mBreakObj`: ModelObj (an object, read-only)
- `mBreakOnlyIfShown`: bool
- `mDisplayModel`: DummyDisplayModel (an object, read-only)
- `mGroundPos`: vec3
- `mHasBinding`: bool
- `mHitInvalidTime`: int (s32)
- `mHitPoint`: int (s32)
- `mPlayRiddleSFX`: bool
- `mPowerStarId`: int (s32)
- `mRumbleDir`: vec3
- `mSensorPos`: vec3
- `mVanishOnBreak`: bool

## CrystalCageMoving

- `mIsCoreShown`: int (u8)
- `mStartPos`: vec3
- `mTicoModel`: ModelObj (an object, read-only)

## CrystalSwitch

- `mIsHit`: int (u8)
- `mRotateSpeed`: float

## DarknessRoomDoor

- `mIsNormalDraw`: bool

## DashRing

- `mActiveTime`: int (s32)
- `mAxis`: vec3
- `mBoostTime`: int (s32)
- `mDoTurn`: bool
- `mInitialPosition`: vec3
- `mMaxDuration`: int (s32)
- `mRevivalTime`: int (s32)
- `mSpeedScale`: float
- `mSubAxis`: vec3

## DeadLeaves

- `mItemType`: int (s32)

## DemoCastGroup

- `mGroup`: LiveActorGroup (an object, read-only)

## DemoCastSubGroup

- `mExecutor`: DemoExecutor (an object, read-only)

## DemoDirector

- `mCastGroupHolder`: DemoCastGroupHolder (an object, read-only)
- `mCastSubGroupHolder`: DemoCastGroupHolder (an object, read-only)
- `mDemoName`: text (read-only)
- `mDemoStarter`: NameObj (an object, read-only)
- `mExecutor`: DemoExecutor (an object, read-only)
- `mIsActive`: bool
- `mIsWaitingCamera`: bool
- `mUseCinemaFrame`: bool

## DemoExecutor

- `mDemoName`: text (read-only)
- `mDemoStarter`: NameObj (an object, read-only)
- `mSheetKeeper.mCount`: int (s32)
- `mSheetName`: text (read-only)
- `mTalkAnimCtrl.mCount`: int (s32)
- `mTalkMessageCtrl.mCount`: int (s32)
- `mUnclippedCasts.mCount`: int (s32)

## DemoKoopaJrShip

- `mAnimCameraIndex`: int (s32)
- `mKoopaJrObj`: KoopaJr (an object, read-only)

## DemoRabbit

- `mAirTimer`: int (s32)
- `mFrontVec`: vec3

## DepthOfFieldArea

- `mIsPlayerSync`: bool

## DepthOfFieldBlur

- `mBlurMaxDist`: int (s32)
- `mBlurMinDist`: int (s32)
- `mIntensity`: float

## DesertLandMoveSwitch

- `mIsHipDropped`: bool
- `mIsOnPlayer`: bool
- `mObjName`: text (read-only)
- `mTimer`: int (s32)
- `mTimerLead`: int (s32)
- `mWasOnPlayer`: bool

## DesertMovingLand

- `mDemoPlayerMtx.mMtx`: float (array)
- `mLowerPos`: vec3
- `mMoveDownTime`: int (s32)
- `mMoveUpTime`: int (s32)
- `mUpperPos`: vec3

## DharmaSambo

- `mFixedPositions.mCount`: int (s32)
- `mHideTimer`: int (s32)
- `mHitSensor`: HitSensor (an object, read-only)
- `mInitialPos`: vec3
- `mParts.mCount`: int (s32)
- `mPartsCount`: int (s32)

## DharmaSamboParts

- `mHitDistance`: float
- `mHost`: DharmaSambo (an object, read-only)
- `mIndex`: int (s32)
- `mIsHead`: bool
- `mOriginalIndex`: int (s32)

## DinoPackun

- `mAimTailRate`: float
- `mAimTailRateSpeed`: float
- `mBall`: DinoPackunBall (an object, read-only)
- `mCamTargetMtx`: CameraTargetMtx (an object, read-only)
- `mCameraRegisterId`: int (s32)
- `mCameraVec`: vec3
- `mDamageCameraState`: int (s32)
- `mDemoPos`: DinoPackunDemoPosition (an object, read-only)
- `mFootPrint`: FootPrint (an object, read-only)
- `mFront`: vec3
- `mIsPreCalcAnim`: bool
- `mShell`: DinoPackunEggShell (an object, read-only)
- `mShellBreakModel`: PartsModel (an object, read-only)
- `mStartPos`: vec3
- `mTailBall`: PartsModel (an object, read-only)

## DinoPackunTailNode

- `mIsLocked`: bool
- `mJointFront`: vec3
- `mJointPos`: vec3
- `mKeepBendPower`: float
- `mLinkLength`: float
- `mNextNode`: LiveActor (an object, read-only)
- `mNodeDirection`: vec3
- `mParent`: DinoPackun (an object, read-only)
- `mPrevNode`: LiveActor (an object, read-only)

## DinoPackunBall

- `mDragSoundStep`: int (s32)
- `mIsBlackDrag`: bool
- `mIsOnFire`: bool
- `mReboundVelocity`: vec3
- `mWeakSensor`: HitSensor (an object, read-only)

## DinoPackunEggShell

- `mSensor`: HitSensor (an object, read-only)

## DinoPackunTailPart

- `mRadius`: float

## Dodoryu

- `mBank`: DodoryuBank (an object, read-only)
- `mCameraRegisterPos`: vec3
- `mClosedAreaObj`: AreaObj (an object, read-only)
- `mDemoCameraTarget`: CameraTargetMtx (an object, read-only)
- `mGroundNormal`: vec3
- `mHill`: DodoryuHill (an object, read-only)
- `mIsRumbling`: bool
- `mIsSwitchOn`: bool
- `mLeadHill`: DodoryuLeadHill (an object, read-only)
- `mRabbit`: DodoryuRabbit (an object, read-only)
- `mSpinOutCameraTarget`: CameraTargetMtx (an object, read-only)
- `mState.mCount`: int (s32)
- `mStateIndex`: int (u32)
- `mWallNormal`: vec3

## DodoryuBank

- `mBankMtx.mMtx`: float (array)

## DodoryuStateBase

- `mHost`: Dodoryu (an object, read-only)

## DodoryuDemoAngry

- `mActorCameraInfo.mCameraSetID`: int (s32)
- `mActorCameraInfo.mZoneID`: int (s32)

## DodoryuDemoAppear

- `mActorCameraInfo.mCameraSetID`: int (s32)
- `mActorCameraInfo.mZoneID`: int (s32)

## DodoryuDemoDown

- `mActorCameraInfo.mCameraSetID`: int (s32)
- `mActorCameraInfo.mZoneID`: int (s32)

## DodoryuDemoOpening

- `mActorCameraInfo.mCameraSetID`: int (s32)
- `mActorCameraInfo.mZoneID`: int (s32)

## MogucchiHill

- `mAppearDistance`: float
- `mAppearDistanceMax`: float
- `mAppearNum`: int (s32)
- `mAutoEffectName`: text (read-only)
- `mHasLightCtrl`: bool
- `mHasReserveMtx`: bool
- `mHost`: LiveActor (an object, read-only)
- `mJointName`: text (read-only)
- `mModelNum`: int (u32)
- `mPieceIndex`: int (s32)
- `mPieceNum`: int (s32)
- `mPieces`: MogucchiHillPiece (an object, read-only)
- `mSizeAnimState`: int (s32)
- `mSizeStep`: int (s32)

## DodoryuHill

- `mHost`: Dodoryu (an object, read-only)

## DodoryuLeadHill

- `mSpinAngle`: float

## DodoryuRabbit

- `mHost`: Dodoryu (an object, read-only)
- `mIsDisplayMessage`: bool
- `mRailCoord`: float
- `mTalkCtrl`: TalkMessageCtrl (an object, read-only)
- `mTalkNode`: int (s32)
- `mTalkSwitchStep`: int (s32)

## DodoryuStateLv1

- `mAppearStep`: int (s32)
- `mHideStep`: int (s32)
- `mIsSpinCountered`: bool
- `mIsValidPop`: bool
- `mKnockDir`: vec3
- `mPopAccel`: float
- `mPopSpeed`: float
- `mRailSpeed`: float
- `mRailSpeedMax`: float

## DodoryuStateLv2

- `mAttackDir`: vec3
- `mChaseAppearEndCounter`: int (s32)
- `mChaseAppearStep`: int (s32)
- `mChaseMoreCounter`: int (s32)
- `mEscapeStepMaxLeft`: int (s32)
- `mEscapeStepMinLeft`: int (s32)
- `mFindPosStep`: int (s32)
- `mIsChaseMoreBrake`: bool
- `mIsPlayerStaggering`: bool
- `mReadyAppearStep`: int (s32)

## DodoryuStateWait

- `mNextStateCounter`: int (s32)

## Dossun

- `mFallingTime`: int (s32)
- `mHasShadow`: bool
- `mHoldTime`: int (s32)
- `mLowerPos`: vec3
- `mMovementDist`: float
- `mRisenWaitTime`: int (s32)
- `mShadowType`: int (s32)
- `mUpperPos`: vec3

## DragonHeadFlower

- `mCurrentGravity`: vec3
- `mEndGravity`: vec3
- `mParabolaAccel`: float
- `mParabolaDirection`: vec3
- `mParabolaDistance`: float
- `mParabolaProgress`: float
- `mParabolaStartPos`: vec3
- `mParabolaSteps`: int (u32)
- `mParabolaVel`: float
- `mPlayerSensor`: HitSensor (an object, read-only)
- `mTargetVelocity`: vec3

## DriftWood

- `mRailClipping`: vec3
- `mRailDirection`: vec3
- `mVibrateOffset`: vec3
- `mWaveSoundTimer`: int (s32)

## DummyDisplayModel

- `mBrkFrame`: int (s32)
- `mIsInCrystal`: bool
- `mItemType`: int (s32)

## EarthenPipe

- `mBasePos`: vec3
- `mHorizExitForce`: float
- `mHostActor`: LiveActor (an object, read-only)
- `mIsIgnoreGravity`: bool
- `mIsRabbitWait`: bool
- `mIsWaterFlowing`: bool
- `mMusicChangeIdx`: int (s32)
- `mMusicState`: int (s32)
- `mPipeLength`: float
- `mPipeMode`: int (s32)
- `mPipeStreamModel`: PartsModel (an object, read-only)
- `mPlayerInStartMtx.mMtx`: float (array)
- `mTargetPipe`: EarthenPipe (an object, read-only)
- `mUp`: vec3
- `mVertExitForce`: float

## EarthenPipeMediator

- `mNumEntries`: int (s32)
- `mPipeEntryRoom`: int (s32)

## ElectricBall

- `mAngularSpeed`: float
- `mBalls.mMaxSize`: int (s32)
- `mClosestBallPos`: vec3
- `mRadius`: float
- `mRailMover`: MapPartsRailMover (an object, read-only)

## ElectricPressureBullet

- `mFront`: vec3
- `mSpeed`: float

## ElectricRail

- `mEaseIn`: float
- `mIsCalcGravity`: bool
- `mPointCount`: int (s32)
- `mPoints`: ElectricRailPoint (an object, read-only)
- `mRailHeight`: int (s32)
- `mSeparatorCount`: int (s32)
- `mShadowDrawer`: ElectricRailShadowDrawer (an object, read-only)

## ElectricRailHolder

- `mIsActive`: bool
- `mRailItems.mCount`: int (s32)
- `mRailModels.mArr`: ModelObj (an object, read-only) (array)
- `mRailModels.mMovedSize`: int (s32)

## ElectricRailMoving

- `mAlpha`: int (u8)
- `mCoord`: float
- `mMovementSpeed`: float
- `mMovingPoints`: ElectricRailMovingPoint (an object, read-only)
- `mPointNum`: int (s32)
- `mRailPosNum`: int (s32)
- `mRailWidth`: float
- `mSegmentLength`: float
- `mSegmentNum`: int (s32)
- `mStackHeight`: int (s32)

## ElectricRailMovingPoint

- `mHasShadow`: bool
- `mIsElectric`: bool

## ElectricRailPoint

- `mHasShadow`: bool
- `mIsActive`: bool

## ShadowDrawer

- `mAnchorJumps`: int (u32)
- `mDrawAnchor`: vec3
- `mHasAnchor`: bool

## ShadowVolumeDrawer

- `mEndDrawShapeOffset`: float
- `mIsCutDropShadow`: bool
- `mStartDrawShapeOffset`: float

## ElectricRailShadowDrawer

- `mHost`: LiveActor (an object, read-only)

## ErrorMessageWindow

- `mMessageId`: text (read-only)

## EventDirector

- `mPowerStarHolder`: PowerStarHolder (an object, read-only)

## EventSequencer

- `mNames.mCount`: int (u32)
- `mNames.mMask`: int (u32)
- `mNumSequences`: int (u32)
- `mSequenceFrame`: int (u16)
- `mSequences.mSize`: int (s32)

## ExterminationChecker

- `mGroup`: LiveActorGroup (an object, read-only)
- `mIsKeySwitch`: int (u8)
- `mIsPowerStar`: int (u8)
- `mKeySwitch`: KeySwitch (an object, read-only)
- `mKeySwitchPos`: vec3

## EyeBeamer

- `mBasePos`: vec3
- `mBeamBloom`: ModelObj (an object, read-only)
- `mBeamLength`: float
- `mBeamMdl`: ModelObj (an object, read-only)
- `mBeamVolumeDrawer`: VolumeModelDrawer (an object, read-only)
- `mClipCenter`: vec3
- `mGotoStartPos`: vec3
- `mIsInMercatorCube`: bool
- `mRailMover`: MapPartsRailMover (an object, read-only)
- `mRailStartPos`: vec3
- `mWaterSurfaceHeight`: float

## FallDownBridge

- `mAngle`: float
- `mAngleSpeed`: float
- `mClipCenter`: vec3
- `mIsFallen`: bool
- `mMaxAngle`: float
- `mMinAngle`: float

## FallOutFieldDraw

- `mFillInsideVolume`: bool

## FileSelectButton

- `mGuidancePlain`: GalaxyMapGalaxyPlain (an object, read-only)

## FileSelectCameraController

- `mFovy`: float
- `mNearTarget`: vec3
- `mPrevFovy`: float
- `mPrevWPoint`: vec3
- `mUpVec`: vec3
- `mWPoint`: vec3

## FileSelectEffect

- `mEffectFrame`: float

## FileSelectInfo

- `mDateMessage.mRoom`: int (u32)
- `mIsSelectedMario`: bool
- `mIsSelectedMarioPrev`: bool
- `mIsViewCompleteEnding`: bool
- `mIsViewNormalEnding`: bool
- `mMissNum`: int (s32)
- `mNameBufferSize`: int (s32)
- `mNumber`: int (s32)
- `mStarNum`: int (s32)
- `mStarPieceNum`: int (s32)
- `mTimeMessage.mRoom`: int (u32)

## FileSelectItem

- `mFaceMtx.mMtx`: float (array)
- `mFaceParts`: MiiFaceParts (an object, read-only)
- `mFellowMtx.mMtx`: float (array)
- `mFileNo`: int (s32)
- `mIsComplete`: int (u8)
- `mIsDragging`: int (u8)
- `mIsInvalidRotate`: bool
- `mIsInvalidateSelect`: bool
- `mIsMiiBroken`: int (u8)
- `mIsNewFile`: bool
- `mIsPointed`: int (u8)
- `mIsPointerOut`: int (u8)
- `mNumber`: FileSelectNumber (an object, read-only)
- `mPlanetMapObj`: PartsModel (an object, read-only)
- `mPrevPointerPos.x`: float
- `mPrevPointerPos.y`: float
- `mRotateSpeed`: float
- `mTurnStep`: int (s32)
- `mTurnTime`: int (s32)

## FileSelectNumber

- `mNumber`: int (s32)

## FileSelectSky

- `mAngleX`: float
- `mAngleY`: float

## FileSelector

- `mBackButton`: BackButton (an object, read-only)
- `mBgmState`: int (s32)
- `mBrosButton`: BrosButton (an object, read-only)
- `mCameraController`: FileSelectCameraController (an object, read-only)
- `mDecidedItem`: FileSelectItem (an object, read-only)
- `mFileInfo`: FileSelectInfo (an object, read-only)
- `mFocusWanted`: NebulaFileSelectButton (an object, read-only)
- `mInfoMessage`: InformationMessage (an object, read-only)
- `mInitItem`: int (unsigned int)
- `mInitStep`: int (unsigned int)
- `mIsAskingQuit`: bool
- `mIsMiiSelectStartFirst`: bool
- `mManual`: Manual2P (an object, read-only)
- `mMiiConfirmIcon`: MiiConfirmIcon (an object, read-only)
- `mMiiSelect`: MiiSelect (an object, read-only)
- `mOperationButton`: FileSelectButton (an object, read-only)
- `mPointCandidate`: FileSelectItem (an object, read-only)
- `mPointedItem`: FileSelectItem (an object, read-only)
- `mQuitButton`: NebulaFileSelectButton (an object, read-only)
- `mQuitFrames`: int (s32)
- `mQuitWindow`: SysInfoWindow (an object, read-only)
- `mSelectEffect`: FileSelectEffect (an object, read-only)
- `mSelectedItem`: FileSelectItem (an object, read-only)
- `mSettingsButton`: NebulaFileSelectButton (an object, read-only)
- `mSettingsFrames`: int (s32)
- `mSky`: FileSelectSky (an object, read-only)
- `mSysInfoWindow`: SysInfoWindow (an object, read-only)
- `mSysInfoWindowMini`: SysInfoWindow (an object, read-only)

## FireBall

- `mHost`: LiveActor (an object, read-only)
- `mUp`: vec3

## FireBar

- `mFireBallCount`: int (s32)
- `mFireBarSpeed`: float
- `mFront`: vec3
- `mStickCount`: int (s32)
- `mStickDistance`: float

## FireBarBall

- `mFireBarParent`: LiveActor (an object, read-only)
- `mIsEffectNear`: bool

## FireBubble

- `mActFrame`: int (s32)
- `mAppearFront`: vec3
- `mAppearPos`: vec3
- `mAppearVelocity`: vec3
- `mChaseCounter`: int (s32)
- `mFront`: vec3
- `mIsValidInfo`: bool

## FireMarioBall

- `mBindTimer`: int (u16)

## PressureBase

- `mBallSpeed`: float
- `mBaseAngle`: float
- `mFront`: vec3
- `mGroup`: MsgSharedGroup (an object, read-only)
- `mIsShortShot`: bool
- `mMessenger`: PressureMessenger (an object, read-only)
- `mRelaxAngle`: float
- `mShotType`: int (s32)
- `mWaitTime`: int (s32)

## FirePressure

- `mIsDisableCollision`: bool

## FirePressureBullet

- `mCanCrash`: bool
- `mFront`: vec3
- `mIsHoming`: bool
- `mSpeed`: float

## FirePressureRadiate

- `mCannonRotation`: float
- `mGroup`: MsgSharedGroup (an object, read-only)
- `mIsGroupLeader`: bool
- `mRadiateLength`: float
- `mRadiateMaxLength`: float
- `mRadiateStep`: int (s32)
- `mShootTime`: int (s32)
- `mWaitTime`: int (s32)

## Fish

- `mCanEscape`: bool
- `mDistanceToApproach`: float
- `mFishType`: int (int)
- `mFollowPos`: vec3
- `mFollowPosOffset`: vec3
- `mFront`: vec3
- `mGroup`: FishGroup (an object, read-only)
- `mStepToMove`: int (s32)
- `mTimeToSearchPlayer`: int (s32)

## FishGroup

- `mClippingCenter`: vec3
- `mFishNum`: int (s32)
- `mSpeed`: float
- `mUp`: vec3

## Flag

- `mAlpha`: int (u8)
- `mAlphaDistanceFar`: float
- `mAlphaDistanceNear`: float
- `mClipCenter`: vec3
- `mDisableLighting`: bool
- `mDisableSound`: bool
- `mFrictionRateMax`: float
- `mFrictionRateMin`: float
- `mFront`: vec3
- `mGravityScale`: float
- `mIsVerticalFlag`: bool
- `mLightColorMin`: float
- `mNumPointsU`: int (s32)
- `mNumPointsV`: int (s32)
- `mObjName`: text (read-only)
- `mPointIntervalU`: float
- `mPointIntervalV`: float
- `mRandomWindAccelMax`: float
- `mRandomWindAccelMin`: float
- `mSeStep`: int (s32)
- `mSide`: vec3
- `mStickLength`: float
- `mUp`: vec3
- `mUseAlpha`: bool
- `mWaitTime`: int (s32)
- `mWavePhase`: float
- `mWindAccelMin`: float
- `mWindAccelRate`: float

## FlameGun

- `mState`: int (s32)

## FlashingCtrl

- `mActor`: LiveActor (an object, read-only)
- `mFlashStartTime`: int (s32)
- `mIsEnded`: bool
- `mOverrideInterval`: int (s32)
- `mTimer`: int (s32)
- `mToggleDraw`: bool

## FlexibleSphere

- `mEnabled`: bool
- `mIsGrowing`: bool
- `mMaxScale`: float
- `mMinScale`: float
- `mScaleRate`: float
- `mShakeTimer`: int (s32)
- `mTimer`: int (s32)
- `mVanishOnShrinkEnd`: bool

## FlipPanel

- `mIsFlipped`: int (u8)
- `mIsPlayerOn`: bool
- `mIsReverse`: bool
- `mJumpCheckCount`: int (s32)

## FlipPanelObserver

- `mDemoDelay`: int (s32)
- `mFlippedNum`: int (s32)
- `mGroup`: MsgSharedGroup (an object, read-only)
- `mIsHeavensDoor`: int (u8)
- `mPowerStarId`: int (s32)

## MapPartsFunction

- `mHost`: LiveActor (an object, read-only)
- `mIsActive`: bool

## FloaterFloatingForce

- `mBasePos`: vec3
- `mMaxSinkDepth`: float
- `mObjName`: text (read-only)
- `mUp`: vec3

## FloaterFloatingForceTypeNormal

- `mRiseAccel`: float
- `mSinkAccel`: float
- `mSinkDepth`: float
- `mSinkSpeed`: float
- `mVelocity`: vec3

## FloaterFloatingForceTypeSpring

- `mRideSinkSpeed`: float
- `mVelocity`: vec3

## Fluff

- `mCameraTargetMtx`: CameraTargetMtx (an object, read-only)
- `mFront`: vec3
- `mModel`: ModelObj (an object, read-only)
- `mRider`: LiveActor (an object, read-only)
- `mSide`: vec3
- `mSpinsRemaining`: int (s32)
- `mTargetUpVec`: vec3
- `mUp`: vec3
- `mWindDir`: vec3
- `mWindDistance`: float
- `mWindSpinTimer`: int (s32)

## FluffWind

- `mNumEffects`: int (s32)

## FluffWindEffect

- `mEffectName`: text (read-only)
- `mTimer`: int (s32)

## FollowCollisionArea

- `mCenter`: vec3
- `mHitRadius`: int (s32)
- `mInsideDepth`: float
- `mIsTeresaPass`: bool
- `mOffset`: float
- `mOutsideAxisCount`: int (u32)
- `mPolygon`: AreaPolygon (an object, read-only)
- `mPushType`: int (s32)
- `mSize`: vec3
- `mTeresaType`: int (s32)

## FootPrint

- `mHeight`: float
- `mIsFading`: bool
- `mLastPrintPos`: vec3
- `mMinPrintDistance`: float
- `mPrintMaxNum`: int (s32)
- `mPrintNum`: int (s32)
- `mRotatingPrintIndex`: int (s32)
- `mSideOffset`: float
- `mWidth`: float

## Fountain

- `mFountainName`: text (read-only)
- `mUp`: vec3

## FountainBig

- `mClippingRadius`: vec3
- `mSpoutTimer`: int (s32)

## FullnessMeter

- `mHost`: LiveActor (an object, read-only)
- `mNumber`: int (s32)
- `mNumberMax`: int (s32)
- `mTargetCounter`: SimpleLayout (an object, read-only)

## PlanetMap

- `mBloomModel`: ModelObj (an object, read-only)
- `mClippingCenter`: vec3
- `mModelName`: text (read-only)

## GCapture

- `mBindActor`: LiveActor (an object, read-only)
- `mCaptureRibbon`: GCaptureRibbon (an object, read-only)
- `mEffectBlend`: float
- `mEffectOffset`: vec3
- `mEffectPos`: vec3
- `mGuidanceTime`: int (s32)
- `mRibbonRate`: float
- `mRotateAxis`: vec3
- `mRotateVelocity`: vec3
- `mTargetDist`: float
- `mTargetKeepTime`: int (s32)
- `mTargetMtx`: CameraTargetMtx (an object, read-only)
- `mTractPower`: float

## GCaptureRibbon

- `mEnd`: vec3
- `mIsLineSet`: bool
- `mSide`: vec3
- `mStart`: vec3
- `mUp`: vec3

## GCaptureTarget

- `mFarAwayColor`: bool
- `mPointableRange`: float
- `mRailMover`: MapPartsRailMover (an object, read-only)
- `mReleaseDistance`: float
- `mStarAnimSpeed`: float

## GalaxyConfirmLayout

- `mBackButton`: BackButton (an object, read-only)

## GalaxyMap

- `mCometIcon.mCount`: int (s32)
- `mDomeIcon.mCount`: int (s32)
- `mGalaxyDetail`: GalaxyMapGalaxyDetail (an object, read-only)
- `mGalaxyName`: text (read-only)
- `mGalaxyPlain`: GalaxyMapGalaxyPlain (an object, read-only)
- `mIcon.mCount`: int (s32)
- `mIsFocusEnabled`: bool
- `mIsPointingNew`: bool
- `mMarioIcon1`: GalaxyMapMarioIcon (an object, read-only)
- `mMarioIcon2`: GalaxyMapMarioIcon (an object, read-only)
- `mPointingIcon`: GalaxyMapIcon (an object, read-only)
- `mTicoIcon.mCount`: int (s32)
- `mTitle`: GalaxyMapTitle (an object, read-only)

## GalaxyMapCometIcon

- `mGalaxyName`: text (read-only)
- `mHost`: LayoutActor (an object, read-only)
- `mPaneName`: text (read-only)

## GalaxyMapController

- `mBackButton`: BackButton (an object, read-only)
- `mMap`: GalaxyMap (an object, read-only)
- `mMapBackground`: GalaxyMapBackground (an object, read-only)
- `mMapSelectButton`: GalaxyMapSelectButton (an object, read-only)
- `mMode`: int (s32)
- `mPowerStarList`: PowerStarList (an object, read-only)
- `mWipe`: WipeFade (an object, read-only)
- `mWipeFrame`: int (s32)

## GalaxyMapDomeIcon

- `mDomeNo`: int (int)
- `mHost`: LayoutActor (an object, read-only)
- `mMode`: int (u8)
- `mNamePaneName`: text (read-only)
- `mNamePlate`: GalaxyNamePlate (an object, read-only)
- `mPaneName`: text (read-only)

## GalaxyMapGalaxyDetail

- `mAButtonIcon`: IconAButton (an object, read-only)

## GalaxyMapGalaxyPlain

- `mGalaxyName.mRoom`: int (u32)
- `mHost`: LayoutActor (an object, read-only)
- `mIsShow`: bool
- `mNamePlate`: GalaxyNamePlate (an object, read-only)
- `mPaneName`: text (read-only)

## GalaxyMapIcon

- `mGalaxyName`: text (read-only)
- `mHost`: LayoutActor (an object, read-only)
- `mMode`: int (u8)
- `mPaneName`: text (read-only)

## GalaxyMapMarioIcon

- `mTargetLayout`: LayoutActor (an object, read-only)
- `mTargetPaneName`: text (read-only)

## GalaxyMapTicoIcon

- `mExclamationGalaxyIndex`: int (int)
- `mGalaxyName`: text (read-only)
- `mHost`: LayoutActor (an object, read-only)
- `mMode`: int (u8)
- `mPaneName`: text (read-only)

## GalaxyMapTitle

- `mMessageID`: text (read-only)

## GalaxyNamePlate

- `mGalaxyName`: text (read-only)
- `mIsAppearNow`: bool
- `mIsShownThisFrame`: bool
- `mPlayNewSe`: bool
- `mShowBalloonNozzle`: bool
- `mStateFrame`: int (s32)

## GalaxyNamePlateDrawer

- `mDrawOrder.mCount`: int (s32)
- `mPlateArray.mCount`: int (s32)

## GalaxySelectInfo

- `mIsOnPause`: bool
- `mIsShownThisFrame`: bool
- `mShownGalaxyName`: text (read-only)

## GamePauseSequence

- `mPauseMenu`: PauseMenu (an object, read-only)

## GameSceneLayoutHolder

- `mAchievementBoard`: NebulaAchievementBoard (an object, read-only)
- `mAchievementToast`: NebulaAchievementToast (an object, read-only)
- `mCameraInfo`: CameraInfo (an object, read-only)
- `mCounterLayoutCtrl`: CounterLayoutController (an object, read-only)
- `mInformationMessage`: InformationMessage (an object, read-only)
- `mIsActiveDefaultGameLayout`: bool
- `mIsStatusInvalidatePending`: bool
- `mMarioMeter`: MarioMeter (an object, read-only)
- `mMarioSubMeter`: MarioSubMeter (an object, read-only)
- `mMissLayout`: MissLayout (an object, read-only)
- `mModText`: NebulaModText (an object, read-only)
- `mNoteCounter`: NoteCounter (an object, read-only)
- `mOneUpBoard`: OneUpBoard (an object, read-only)
- `mPurpleCoinCounter`: PurpleCoinCounter (an object, read-only)
- `mYesNoLayout`: YesNoLayout (an object, read-only)

## GameStageClearSequence

- `mEffectFollowPos.x`: float
- `mEffectFollowPos.y`: float
- `mEffectLayout`: SimpleEffectLayout (an object, read-only)

## GameSystemRestartProcess

- `mResetOperation`: int (s32)

## Gesso

- `mClipStep`: int (s32)
- `mFront`: vec3
- `mInitPos`: vec3
- `mIsHighSpeedMode`: bool
- `mIsMarioLeft`: bool
- `mPushVelocity`: vec3
- `mUp`: vec3
- `mUpTarget`: vec3

## GhostPlayer

- `mAnimTrackWeights`: float (array)
- `mAppearStarPieceCooldown`: int (u16)
- `mCameraTargetMtx`: CameraTargetMtx (an object, read-only)
- `mGhostNames`: text (read-only)
- `mHasJetTurtle`: bool
- `mInitialDirection`: vec3
- `mIsDemoCameraActive`: bool
- `mIsHidden`: bool
- `mJetTurtleShadow`: JetTurtleShadow (an object, read-only)
- `mKilledByStar`: bool
- `mPlayerTrampleCooldown`: int (u16)
- `mPowerStarTarget`: LiveActor (an object, read-only)
- `mRaceManagerLayout`: RaceManagerLayout (an object, read-only)
- `mSkippingLeadIn`: bool
- `mStartPos`: vec3
- `mTargetRotation`: vec3
- `mWaitingToStart`: bool

## GlaringLightArea

- `mPos`: vec3

## JetTurtle

- `mHasReviveLight`: bool
- `mHomingTarget`: HitSensor (an object, read-only)
- `mInitPos`: vec3
- `mInitRotation`: vec3
- `mIsAppeared`: bool
- `mIsResetRequested`: bool
- `mIsRevive`: bool
- `mLastGravity`: vec3
- `mShellType`: int (u16)
- `mSpeedType`: int (u16)
- `mTakenSensor`: HitSensor (an object, read-only)
- `mThrowDir`: vec3
- `mThrowSpeedBonus`: float
- `mThrowStartPos`: vec3

## GrandStarReturnDemoStarter

- `mDistanceToCore`: vec3
- `mPowerStar`: ModelObj (an object, read-only)
- `mPowerStarPosition`: vec3
- `mStageResultInformer`: StageResultInformer (an object, read-only)

## GravityLight

- `mBaseMtx.mMtx`: float (array)
- `mCenter`: vec3
- `mLightCylinder`: LightCylinder (an object, read-only)
- `mMapPartsRailMover`: MapPartsRailMover (an object, read-only)
- `mMapPartsRotator`: MapPartsRotator (an object, read-only)
- `mRadius`: float

## GravityLightRoad

- `mEndPos`: vec3
- `mHost`: LiveActor (an object, read-only)
- `mRadius`: float
- `mSpeed`: float

## GreenCaterpillarBig

- `mBodyArrayLength`: int (s32)
- `mCurBodyParts`: int (s32)
- `mIsEnteringHole`: int (u8)
- `mIsLeavingApple`: int (u8)
- `mRailPointNo`: int (s32)

## GreenCaterpillarBigBody

- `mCaterpillar`: LiveActor (an object, read-only)
- `mFrontVec`: vec3

## GroundChecker

- `mFriction`: float

## GroupCheckManager

- `mGroups`: GroupChecker (an object, read-only) (array)

## GroupChecker

- `mNames.mCount`: int (u32)
- `mNames.mMask`: int (u32)

## GroupSwitchWatcher

- `mActionType`: int (s32)
- `mLogicType`: int (s32)
- `mNumSwitches`: int (s32)

## Halo

- `mDistance`: float

## HammerHeadPackun

- `mAttackDir`: vec3
- `mFront`: vec3
- `mImmunityTime`: int (s32)
- `mLeafModel`: PartsModel (an object, read-only)
- `mNumbTime`: int (s32)
- `mUp`: vec3

## Hanachan

- `mAttackPos`: vec3
- `mBodyParts.mArr`: HanachanParts (an object, read-only) (array)
- `mBodyParts.mMovedSize`: int (s32)
- `mFrontDir`: vec3
- `mIsChasePlayer`: bool

## HanachanParts

- `mActionStartStep`: int (s32)
- `mFallVelocity`: vec3
- `mHost`: Hanachan (an object, read-only)
- `mIsLanded`: bool
- `mPartsIndex`: int (s32)
- `mPushVelocity`: vec3

## HazeCube

- `mDistance`: float

## HeatHazeDirector

- `mEffect`: HeatHazeEffect (an object, read-only)

## HeatHazeEffect

- `mHazeDistance`: float

## HeavensDoorDemoObj

- `mIsInsideCageDemo`: bool

## HipDropMoveObj

- `mModelName`: text (read-only)

## HipDropDemoMoveObj

- `mMtx.mMtx`: float (array)

## HipDropRock

- `mBreakModel`: ModelObj (an object, read-only)
- `mBreakModelMtx.mMtx`: float (array)
- `mItemType`: int (s32)
- `mObjArg7`: bool
- `mStarPieceNum`: int (s32)

## HipDropSwitch

- `mIsHipDropped`: bool
- `mIsOnPlayer`: bool
- `mWasOnPlayer`: bool

## HipDropTimerSwitch

- `mIsLightPressed`: bool
- `mTimerMax`: int (s32)
- `mTimerSFXType`: int (s32)
- `mWasLightPressed`: bool

## HitWallTimerSwitch

- `mIsHit`: int (u8)
- `mTimer`: int (s32)

## HomeButtonLayout

- `mForcedClosed`: bool
- `mIsInitialized`: bool

## HomingKiller

- `mBaseFront`: vec3
- `mBasePos`: vec3
- `mBaseUp`: vec3
- `mChaseEndDist`: float
- `mChaseInvalidTime`: int (s32)
- `mChaseRotateSpeed`: float
- `mChaseStartAngle`: float
- `mChaseStartDist`: float
- `mDisableChase`: bool
- `mFreezePos`: vec3
- `mFreezeTime`: int (s32)
- `mFront`: vec3
- `mIsLinearShot`: bool
- `mMoveTime`: int (s32)
- `mPropeller`: PartsModel (an object, read-only)
- `mTargetSensor`: HitSensor (an object, read-only)
- `mTorpedoLight`: ModelObj (an object, read-only)
- `mUp`: vec3
- `mUseFullSightAngle`: bool

## HomingKillerLauncher

- `mKiller`: HomingKiller (an object, read-only)

## HoneyBee

- `mClippingCenter`: vec3
- `mFloatOffset`: float
- `mInitAction`: int (s32)
- `mRailPointNo`: int (s32)
- `mTalkMtx.mMtx`: float (array)

## HoneyQueen

- `mWing`: ModelObj (an object, read-only)

## IceMerameraKing

- `mBinderOffset`: vec3
- `mBreakModel`: LiveActor (an object, read-only)
- `mCameraTarget`: CameraTargetDemoActor (an object, read-only)
- `mEscapeStep`: int (u32)
- `mExtinguishHeight`: float
- `mFront`: vec3
- `mHitPoint`: int (s32)
- `mIce.mCount`: int (s32)
- `mInitPos`: vec3
- `mInitRotation`: vec3
- `mIsPinchBgmAllowed`: bool
- `mIsPinchBgmOn`: bool
- `mMeramera`: Meramera (an object, read-only)
- `mShockWave`: LiveActor (an object, read-only)
- `mThrowNum`: int (s32)
- `mThrowingIce`: ThrowingIce (an object, read-only)
- `mUp`: vec3
- `mWeaponIndex`: int (s32)
- `mWeaponNum`: int (s32)

## IceStep

- `mTimer`: int (s32)

## RailMoveObj

- `mRailClippingCenter`: vec3
- `mWasRailMoving`: int (u8)

## IceVolcanoUpDownPlane

- `mNearestPosToPlayer`: vec3

## IconAButton

- `mFollowActor`: LayoutActor (an object, read-only)
- `mFollowPaneName.mInline`: int (char) (array)
- `mFollowPaneName.mText`: text (read-only)
- `mFollowPos.x`: float
- `mFollowPos.y`: float

## ImageEffectDirector

- `mCurrentEffect`: ImageEffectBase (an object, read-only)
- `mDepthOfFieldIntensity`: float
- `mIsAuto`: bool
- `mIsDOFOffInSubjective`: bool
- `mIsPlayerSync`: bool
- `mPlayerSyncIntensity`: int (s32)
- `mWasDOFSuppressed`: bool

## ImageEffectSystemHolder

- `mDirector`: ImageEffectDirector (an object, read-only)

## InformationMessage

- `mAButtonIcon`: IconAButton (an object, read-only)
- `mIsCenter`: bool

## InformationObserver

- `mDemoActor`: LiveActor (an object, read-only)
- `mDisplayFrame`: int (s32)
- `mIsTimeKeepDemo`: bool

## InvisiblePolygonObj

- `mBaseMtx.mMtx`: float (array)

## IronCannonLauncherPoint

- `mBulletSpeed`: float
- `mShotInterval`: int (s32)

## IronCannonLauncher

- `mMuzzlePos`: vec3

## KoopaJrShipCannonShell

- `mFreezePos`: vec3
- `mFreezeStep`: int (s32)
- `mFreezeVelocity`: vec3

## ItemBlock

- `mCalcShadow`: bool
- `mItemCount`: int (s32)
- `mKind`: int (s32)
- `mNoCalcAnim`: bool
- `mTimer`: int (s32)

## ItemBubble

- `mBasePos`: vec3
- `mClipCenter`: vec3
- `mItemCount`: int (u32)
- `mItemType`: int (u32)
- `mRailSpeed`: float
- `mSwing`: float
- `mUseRail`: bool

## Jellyfish

- `mClipCenter`: vec3
- `mFront`: vec3
- `mIsConnectedRail`: bool
- `mIsRailTurnBack`: bool
- `mRailGoalWaitStep`: int (s32)
- `mRailSpeed`: float
- `mWaveStep`: int (s32)

## JellyfishElectric

- `mClipCenter`: vec3
- `mFront`: vec3
- `mIsConnectedRail`: bool
- `mIsDanger`: bool
- `mIsRailTurnBack`: bool
- `mRailGoalWaitStep`: int (s32)
- `mRailSpeed`: float
- `mWaveStep`: int (s32)

## Jiraira

- `mExplodeRadius`: float

## JumpEmitter

- `mCameraCenter`: vec3
- `mHeadModel`: PartsModel (an object, read-only)
- `mHeadMtx.mMtx`: float (array)
- `mIsEventCamera`: bool

## JumpBeamer

- `mBeams`: RingBeam (an object, read-only) (array)

## JumpBranch

- `mCenter`: vec3
- `mFront`: vec3
- `mRider`: LiveActor (an object, read-only)
- `mSide`: vec3
- `mSwingReverse`: bool
- `mUp`: vec3

## JumpGuarder

- `mBabies`: JumpGuarderBaby (an object, read-only)
- `mBabyVelocity`: float
- `mBumpCooldown`: int (s32)
- `mNumActiveBabies`: int (s32)
- `mNumBabies`: int (s32)
- `mNumCoins`: int (s32)

## JumpHole

- `mBindVelocity`: vec3
- `mBoundSensor`: HitSensor (an object, read-only)
- `mHoleGravity`: vec3
- `mLaunchDirection`: vec3
- `mLaunchDistance`: float
- `mLaunchDuration`: int (u32)
- `mLaunchGravity`: vec3
- `mLaunchLinear`: float
- `mLaunchQuadratic`: float
- `mLaunchStart`: vec3
- `mLaunchTime`: float
- `mSettledFrames`: int (s32)

## JumpSpider

- `mFront`: vec3
- `mJumpPos`: vec3

## JumpStand

- `mBindedActor`: LiveActor (an object, read-only)
- `mIsMarioJumpingHigh`: bool

## Kabokuri

- `mBreakModel`: ModelObj (an object, read-only)
- `mFrontVec`: vec3
- `mIsFloating`: bool
- `mIsValidDropFire`: bool
- `mKuribo`: Kuribo (an object, read-only)

## Kameck

- `mActiveDistance`: float
- `mBeam`: KameckBeam (an object, read-only)
- `mBeamType`: int (s32)
- `mFrontVec`: vec3
- `mMoveStep`: int (s32)
- `mRailCoord`: float
- `mRailNextPointCoord`: float

## KameckBeam

- `mBeamKind`: int (s32)
- `mKameckFireBalls`: KameckFireBall (an object, read-only) (array)
- `mKameckTurtle`: KameckTurtle (an object, read-only)
- `mShootDir`: vec3
- `mWandLocalPosition`: vec3

## KameckTurtle

- `mFront`: vec3
- `mRunDir`: vec3
- `mTurtle`: JetTurtle (an object, read-only)

## Kanina

- `mFront`: vec3
- `mHitWallDir`: vec3
- `mInitPos`: vec3
- `mKinokoOneUp`: BenefitItemObj (an object, read-only)
- `mRunAwayBreakStep`: int (u32)
- `mRunAwaySide`: float
- `mRunAwayStep`: int (u32)
- `mWalkCount`: int (s32)

## Karikari

- `mCanAttack`: bool
- `mFront`: vec3
- `mFrozenTime`: int (s32)
- `mIsPushable`: bool
- `mIsReadyToLandTornado`: bool

## KarikariDirector

- `mClingNum`: int (s32)
- `mLowestReleaseDist`: float
- `mNoReleaseTime`: int (s32)
- `mReleaseKarikari`: Karikari (an object, read-only)
- `mReleaseVel.x`: float
- `mReleaseVel.y`: float
- `mRumbleTime`: int (s32)

## Karon

- `mStopSwAnim`: bool

## KeySwitch

- `mCurDemoFrame`: int (s32)

## Kinopio

- `mAppearBehavior`: int (s32)
- `mBehavior`: int (s32)
- `mGoodsIndex`: int (s32)
- `mIsBgmStarted`: bool
- `mIsSpawnPowerStarMode`: bool
- `mObjArg0`: int (s32)

## KinopioAstro

- `mHasLuigiLetter`: bool
- `mHasPeachLetter`: bool
- `mIsPlayerLuigi`: bool
- `mLetterIconTalkCtrl`: TalkMessageCtrl (an object, read-only)
- `mLuigiLetter`: LuigiLetter (an object, read-only)
- `mPeachLetter`: PeachLetter (an object, read-only)

## Kiraira

- `mChain`: KirairaChain (an object, read-only)
- `mDriftStep`: float
- `mEyesOpen`: bool
- `mFront`: vec3
- `mIsForceDetonated`: bool
- `mIsRail`: bool
- `mRailCoordSpeed`: float
- `mRailVec`: vec3
- `mSharedGroup`: MsgSharedGroup (an object, read-only)

## KirairaChain

- `mAccel`: vec3
- `mAccelPointIdx`: int (s32)
- `mAccelTimer`: int (s32)
- `mClippingCenter`: vec3
- `mCutPos`: vec3
- `mFixPointBottom`: ModelObj (an object, read-only)
- `mFixPointBottomDir`: vec3
- `mFixPointTop`: ModelObj (an object, read-only)
- `mFixPointTopDir`: vec3
- `mHost`: LiveActor (an object, read-only)
- `mIsCut`: bool
- `mModelDrawer`: SimpleModelDrawer (an object, read-only)
- `mPointCount`: int (s32)
- `mRailEnd`: vec3
- `mRailStart`: vec3

## Koopa

- `mFront`: vec3

## KoopaBattleMapPlanet

- `mCoinPlateGroup`: LiveActorGroup (an object, read-only)
- `mDamagePlate`: LiveActor (an object, read-only)
- `mDamagePlateGroup`: LiveActorGroup (an object, read-only)
- `mIsFinalPlanet`: bool
- `mIsNoPlate`: bool
- `mModelName`: text (read-only)
- `mPlateGroup`: LiveActorGroup (an object, read-only)
- `mRotateMoment`: vec3

## KoopaBattleMapStair

- `mFallStep`: int (s32)
- `mFireAttackStep`: int (s32)
- `mIsBig`: bool
- `mIsJumpAfterAttack`: bool
- `mIsKoopaAttack`: bool
- `mIsTurn`: bool
- `mLevel`: int (s32)
- `mTargetCorner`: int (s32)
- `mTargetPos`: vec3
- `mTimeToBreak`: int (s32)
- `mType`: int (s32)
- `mWaitFallStep`: int (s32)

## KoopaFigureBall

- `mAngle`: float
- `mFront`: vec3
- `mKoopa`: Koopa (an object, read-only)
- `mRadius`: float

## KoopaFireShort

- `mDuration`: int (s32)
- `mFront`: vec3
- `mKoopa`: Koopa (an object, read-only)
- `mOffset`: float
- `mSpeed`: float

## KoopaFireStairs

- `mBreakModel`: ModelObj (an object, read-only)
- `mFlyUpDir`: vec3
- `mFront`: vec3
- `mIsKoopaJr`: bool
- `mStair`: KoopaBattleMapStair (an object, read-only)
- `mStartPos`: vec3

## KoopaJr

- `mIsInvalidAppearStarPiece`: bool
- `mIsShipBattleTalk`: bool

## KoopaJrShip

- `mCoordSpeed`: float
- `mHitDir`: vec3
- `mHitPoint`: int (s32)
- `mHitPos`: vec3
- `mInitPos`: vec3
- `mJr`: KoopaJr (an object, read-only)
- `mJrLocalPos`: vec3
- `mKamecks.mCount`: int (s32)
- `mKamecksAtTurn.mCount`: int (s32)
- `mKillers.mArr`: HomingKiller (an object, read-only) (array)
- `mKillers.mMovedSize`: int (s32)
- `mPodModel`: ModelObj (an object, read-only)
- `mPodMtx.mMtx`: float (array)
- `mPropAngle`: float
- `mPropRotateSpeed`: float
- `mShipBreakModel`: ModelObj (an object, read-only)
- `mShipMtx.mMtx`: float (array)
- `mSpeedEaseStep`: int (s32)

## KoopaPlanetShadow

- `mKoopa`: Koopa (an object, read-only)

## KoopaShockWave

- `mAngle`: float
- `mFront`: vec3
- `mKoopa`: Koopa (an object, read-only)
- `mPartsModel`: PartsModel (an object, read-only)
- `mShadow`: ModelObj (an object, read-only)
- `mUp`: vec3

## KoteBug

- `mFront`: vec3

## Kuribo

- `mFront`: vec3
- `mHasItem`: bool
- `mIsNoGravitySupport`: bool

## KuriboChief

- `mFront`: vec3
- `mKeySwitch`: KeySwitch (an object, read-only)

## KuriboMini

- `mFront`: vec3

## LargeChain

- `mBottomFixParts`: LargeChainParts (an object, read-only)
- `mChainArray`: LargeChainParts (an object, read-only)
- `mChainCount`: int (u32)
- `mTopFixParts`: LargeChainParts (an object, read-only)

## LavaBreakColumn

- `mEffectPos`: vec3
- `mIsPillarFallen`: bool

## LavaFloater

- `mFloatingForce`: FloaterFloatingForceTypeNormal (an object, read-only)
- `mGroundOffset`: float
- `mGroundPos`: vec3
- `mObjectName`: text (read-only)

## LavaGeyser

- `mArg0`: int (s32)
- `mArg1`: int (s32)
- `mTopPos`: vec3
- `mUp`: vec3

## LavaHomeSeesaw

- `mGroundPos`: vec3
- `mSeesawRotator`: LavaHomeSeesawRotator (an object, read-only)
- `mYOffset`: float

## LavaHomeSeesawRotator

- `mAngularSpeedMax`: float
- `mAngularVelocity`: float
- `mInertia`: float
- `mInitialUp`: vec3
- `mIsHipDropped`: bool
- `mMoveSound`: text (read-only)
- `mMoveSoundSpeed`: float
- `mRestoreForce`: float
- `mRotateAngle`: float
- `mRotateAxis`: vec3

## LavaJamboSunPlanet

- `mBloomModel`: PartsModel (an object, read-only)
- `mClipCenter`: vec3
- `mMoveSpeed`: float

## LavaProminence

- `mBloomModel`: PartsModel (an object, read-only)
- `mClipCenter`: vec3
- `mEmitDropEffect`: bool
- `mMoveRailCoord`: float
- `mMoveRailEndOffset`: float
- `mMoveSpeed`: float
- `mRailDir`: vec3
- `mRailEndDir`: vec3
- `mRailEndPos`: vec3
- `mRailStartDir`: vec3
- `mRailStartPos`: vec3
- `mWaitTime`: int (s32)

## LavaProminenceTriple

- `mAppearTime`: int (s32)
- `mBloomModel`: PartsModel (an object, read-only)
- `mDefaultAppearTime`: int (s32)
- `mFront`: vec3
- `mIsAppearDrop1`: bool
- `mIsAppearDrop2`: bool
- `mIsAppearDrop3`: bool
- `mIsEnvironment`: bool
- `mRotateVel`: float
- `mSide`: vec3

## LavaSteam

- `mEffectScale`: vec3
- `mUp`: vec3

## LavaStrangeRock

- `mHitType`: int (s32)
- `mRockType`: int (s32)

## LavaSunPlanet

- `mObjName`: text (read-only)
- `mRotator`: MapPartsRotator (an object, read-only)

## LensFlareDirector

- `mAsker`: int (u32)
- `mBrightCenter.x`: float
- `mBrightCenter.y`: float
- `mBrightLevel`: float
- `mBrightObjArray.mMovedSize`: int (s32)
- `mBrightObjCount`: int (u32)
- `mGlow`: LensFlareGlow (an object, read-only)
- `mLine`: LensFlareLine (an object, read-only)
- `mNowCenter.x`: float
- `mNowCenter.y`: float
- `mObjCenter.x`: float
- `mObjCenter.y`: float
- `mProbeCapacity`: int (u32)
- `mRing`: LensFlareRing (an object, read-only)

## LensFlareModel

- `mFadeLevel`: float
- `mFadeStep`: float
- `mFlareStrength`: float

## LensFlareRing

- `mRingFrame`: float

## LightArea

- `mPlacedZoneID`: int (s32)

## LightCylinder

- `mLightVolume`: VolumeModelDrawer (an object, read-only)

## LightDirector

- `mAreaHolder`: LightAreaHolder (an object, read-only)

## LiveActorGroupArray

- `mGroups.mCount`: int (s32)

## LotusLeaf

- `mInitPos`: vec3
- `mShakePeriod`: float
- `mShakeSpeed`: float

## LuigiLetter

- `mAButtonIcon`: IconAButton (an object, read-only)
- `mGalaxyName`: text (read-only)
- `mIsForTalk`: bool

## LuigiNPC

- `mType`: int (s32)

## MagicBell

- `mHitMarkPosition`: vec3

## Manual2P

- `mBackButton`: BackButton (an object, read-only)
- `mCanScrollLeft`: bool
- `mCanScrollRight`: bool
- `mPageIndex`: int (s32)
- `mPageNum`: int (s32)

## MapPartsAppearController

- `mIsAppeared`: int (u8)
- `mSignMotionType`: int (s32)

## MapPartsFloatingForce

- `mBasePos`: vec3
- `mMaxDepth`: float
- `mObjectName`: text (read-only)
- `mRideSinkSpeed`: float
- `mRotateAccelType`: int (s32)
- `mRotateAngle`: float
- `mRotateSpeed`: float
- `mSinkDepth`: float
- `mSinkSpeed`: float
- `mUpDirection`: vec3
- `mVelocity`: vec3

## MapPartsRailGuideDrawer

- `mGuidePoints.mCount`: int (s32)
- `mGuideType`: int (s32)
- `mModelName`: text (read-only)
- `mRailId`: int (s32)

## MapPartsRailGuideHolder

- `mDrawers.mArr`: MapPartsRailGuideDrawer (an object, read-only) (array)
- `mDrawers.mSize`: int (s32)
- `mNumRailGuides`: int (s32)

## MapPartsRailGuidePoint

- `mCoord`: float
- `mIsPointModel`: bool

## MapPartsRailMover

- `mAccelTime`: int (s32)
- `mAcceleration`: float
- `mInitCoord`: float
- `mMoveConditionType`: int (s32)
- `mMoveStopType`: int (s32)
- `mRailPointPassChecker`: MapPartsRailPointPassChecker (an object, read-only)
- `mSignCoord`: float
- `mSignMotionType`: int (s32)
- `mSpeed`: float
- `mStartPos`: vec3
- `mStopTime`: int (s32)

## MapPartsRailPointPassChecker

- `mRailPointNo`: int (s32)

## MapPartsRailPosture

- `mMovePosture`: int (s32)

## MapPartsRailRotator

- `mAngle`: float
- `mRotateAxis`: int (s32)
- `mRotateSpeed`: float
- `mRotateType`: int (s32)
- `mTargetAngle`: float

## MapPartsRotator

- `mAngle`: float
- `mIsOnReverse`: bool
- `mRotateAccelType`: int (s32)
- `mRotateAngle`: float
- `mRotateAxis`: int (s32)
- `mRotateSpeed`: float
- `mRotateStopTime`: int (s32)
- `mRotateType`: int (s32)
- `mSignAngle`: float
- `mSignMotionType`: int (s32)
- `mTargetAngle`: float
- `mTargetRotateSpeed`: float

## MapPartsSeesaw1AxisRotator

- `mAngularAccel`: float
- `mAngularSpeedMax`: float
- `mAngularVelocity`: float
- `mHipDrop`: bool
- `mInertiaConst`: float
- `mInitialUp`: vec3
- `mMoveSound`: text (read-only)
- `mMoveSoundSpeed`: float
- `mRestoreForce`: float
- `mRotateAngle`: float
- `mRotateAxis`: vec3

## MapPartsSeesaw2AxisRotator

- `mHipDrop`: bool
- `mInertiaConst`: float
- `mMoveSound`: text (read-only)
- `mMoveSoundSpeed`: float
- `mRestoreForce`: float
- `mRotateAngle`: float
- `mRotateSpeed`: float
- `mUp`: vec3

## MarblePlanet

- `mCorePlanetModel`: ModelObj (an object, read-only)
- `mNumElectrons`: int (s32)
- `mRemainingElectrons`: int (s32)

## MarblePlanetElectron

- `mElectronShadow`: MarblePlanetElectronShadow (an object, read-only)
- `mFront`: vec3
- `mParentPlanet`: MarblePlanet (an object, read-only)

## MarblePlanetElectronShadow

- `mParentElectron`: MarblePlanetElectron (an object, read-only)

## MarioActor

- `mActionHeld`: bool
- `mActionTrigger`: bool
- `mAirPunchHitCount`: int (u32)
- `mAirPunchHits.mArr`: HitSensor (an object, read-only) (array)
- `mAirPunchHits.mSize`: int (s32)
- `mAlphaTint.alpha`: int (u8)
- `mAlphaTint.fogEnd`: float
- `mAlphaTint.fogFar`: float
- `mAlphaTint.fogNear`: float
- `mAlphaTint.fogStart`: float
- `mAlphaTint.hasFog`: bool
- `mAlphaTint.kind`: int (u8)
- `mAnimationFrozen`: bool
- `mAttacking`: bool
- `mAutoBindTable`: MatrixControl (an object, read-only)
- `mBaseCorrectionMtx.mMtx`: float (array)
- `mBaseEuler`: vec3
- `mBaseFrontVec`: vec3
- `mBaseMtx.mMtx`: float (array)
- `mBaseMtxForced`: bool
- `mBaseMtxSet`: bool
- `mBaseMtxStale`: bool
- `mBaseScaleMtx.mMtx`: float (array)
- `mBeeModel`: ModelHolder (an object, read-only)
- `mBeePoseDir`: vec3
- `mBeePoseGravity`: vec3
- `mBeePoseHeadPos`: vec3
- `mBeePoseTrans`: vec3
- `mBeePoseVelocity`: vec3
- `mBeeStickInhibit`: int (u16)
- `mBeeWallWalk`: int (u8)
- `mBeeWing`: MarioParts (an object, read-only)
- `mBeeWingAnim`: int (u8)
- `mBindOn`: bool
- `mBindRatio`: float
- `mBinderOffset`: vec3
- `mBlackHole`: BlackHole (an object, read-only)
- `mBlackHolePosition`: vec3
- `mBlackHoleRotateAxis`: vec3
- `mBlendMtxRate`: float
- `mBlendMtxTimer`: int (u16)
- `mBlinkFrame`: int (u8)
- `mBlinkTimer`: int (u16)
- `mBlueFireDamage`: int (u8)
- `mBlurPoseIndex`: int (u16)
- `mBlurPoseSet`: int (u16)
- `mBlurState`: int (u8)
- `mBodyDown`: vec3
- `mBodyHeight`: float
- `mBodyRadius`: float
- `mBodyUp`: vec3
- `mBombTeresaTimer`: int (u16)
- `mBoneModel`: ModelHolder (an object, read-only)
- `mCamDirX`: vec3
- `mCamDirY`: vec3
- `mCamDirZ`: vec3
- `mCamPos`: vec3
- `mCameraCut`: bool
- `mCameraFrontVec`: vec3
- `mCameraLiftTimer`: int (u16)
- `mCameraSideVec`: vec3
- `mCameraTrans`: vec3
- `mCameraUpTarget`: vec3
- `mCameraUpTimer`: int (u16)
- `mCapMtx.mMtx`: float (array)
- `mCareMovingObject`: bool
- `mCenterPos`: vec3
- `mClapCatchStart`: bool
- `mClapCoinCount`: int (u32)
- `mClapCoins.mArr`: HitSensor (an object, read-only) (array)
- `mClapCoins.mSize`: int (s32)
- `mCollisionShadow`: CollisionShadow (an object, read-only)
- `mCommonEffectCount`: int (u16)
- `mControlPos`: vec3
- `mControlVelocity`: vec3
- `mCurrModel`: int (u8)
- `mDamageFlashTimer`: int (u16)
- `mDarkMaskOn`: bool
- `mDarkMaskTimer`: int (u16)
- `mDemoBaseKept`: bool
- `mDisplayModel`: int (u8)
- `mEyeLidFrame`: int (u16)
- `mEyeLidReset`: int (u8)
- `mEyeLidTexBase`: int (u32)
- `mFaceHeightTable`: MatrixValueGetter (an object, read-only)
- `mFaceModel`: ModelHolder (an object, read-only)
- `mFaceMtx.mMtx`: float (array)
- `mFacePose`: int (u8)
- `mFacePoseCount`: int (u8)
- `mFireBalls`: FireMarioBall (an object, read-only) (array)
- `mFirstMorphCameraEnd`: int (u16)
- `mFirstMorphCameraOn`: int (u16)
- `mFlashFogOn`: bool
- `mFlashFogRate`: float
- `mFlashFogTimer`: int (u16)
- `mFloorMaterialHold`: int (u16)
- `mFogRate`: float
- `mFogWaveTimer`: int (u16)
- `mFootPrint`: FootPrint (an object, read-only)
- `mFootRMtx.mMtx`: float (array)
- `mForceKilled`: bool
- `mForcedBaseMtx.mMtx`: float (array)
- `mFpViewHidden`: int (u8)
- `mFrameCount`: int (u32)
- `mFreezeModel`: MarioParts (an object, read-only)
- `mFrontEuler`: vec3
- `mGameOverKind`: int (u8)
- `mGravityBlendTimer`: int (u16)
- `mGravityJumpTimer`: int (u16)
- `mGravityRatio`: float
- `mGravitySameAhead`: bool
- `mGravityVec`: vec3
- `mHandL`: ModelHolder (an object, read-only)
- `mHandLMtx.mMtx`: float (array)
- `mHandPoseL`: int (u8)
- `mHandPoseR`: int (u8)
- `mHandR`: ModelHolder (an object, read-only)
- `mHandRMtx.mMtx`: float (array)
- `mHandsHidden`: bool
- `mHeadPos`: vec3
- `mHealth`: int (u32)
- `mHidden`: bool
- `mHiddenByDemo`: int (u8)
- `mHipDropButton`: int (u16)
- `mHipMtx.mMtx`: float (array)
- `mHomingSensor`: HitSensor (an object, read-only)
- `mHopperModel`: ModelHolder (an object, read-only)
- `mIceHandL`: ModelHolder (an object, read-only)
- `mIceHandR`: ModelHolder (an object, read-only)
- `mIceModel`: ModelHolder (an object, read-only)
- `mIceStepIndex`: int (u16)
- `mInDemo`: bool
- `mInRush`: bool
- `mInitializing`: int (u8)
- `mInvincibleHandL`: ModelHolder (an object, read-only)
- `mInvincibleHandR`: ModelHolder (an object, read-only)
- `mInvincibleModel`: ModelHolder (an object, read-only)
- `mItemPickupBlock`: int (u16)
- `mJumpButton`: int (u16)
- `mLastMove`: vec3
- `mLastPushMove`: vec3
- `mLastRushEndFrame`: int (u32)
- `mLastRushSensor`: HitSensor (an object, read-only)
- `mLifeDamageQueue`: int (u32)
- `mLifeGainBlocked`: bool
- `mLightTint.alpha`: int (u8)
- `mLightTint.fogEnd`: float
- `mLightTint.fogFar`: float
- `mLightTint.fogNear`: float
- `mLightTint.fogStart`: float
- `mLightTint.hasFog`: bool
- `mLightTint.kind`: int (u8)
- `mLockOnAngle`: float
- `mMainSpinBlock`: int (u16)
- `mMainSpinHeld`: bool
- `mMainSpinTrigger`: bool
- `mMaskIndex`: int (u16)
- `mMaterialEffects.mCount`: int (u32)
- `mMaterialEffects.mMask`: int (u32)
- `mMaxHealth`: int (u32)
- `mMessageEffectTable`: MatrixControl (an object, read-only)
- `mModForm`: int (u16)
- `mModHidden`: bool
- `mModelUnbaseMtx.mMtx`: float (array)
- `mMorphActionTable`: MatrixControl (an object, read-only)
- `mMorphAnimDone`: int (u8)
- `mMorphEndTimer`: int (u16)
- `mMorphFlashDone`: int (u8)
- `mMorphHidden`: bool
- `mMorphItemSensor`: HitSensor (an object, read-only)
- `mMorphTimer`: int (u16)
- `mMoveFrontVec`: vec3
- `mMovementMove`: vec3
- `mMovementTimer`: int (u16)
- `mMudWalkTimer`: int (u8)
- `mNullAnimation`: MarioNullBck (an object, read-only)
- `mNullAnimationKind`: int (s8)
- `mNumModPowerUps`: int (s32)
- `mPlayerMode`: int (u16)
- `mPlayerModeTimer`: int (u16)
- `mPolygonAttackDone`: bool
- `mPosRelativeToBlackHole`: vec3
- `mPowerupCollected`: bool
- `mPreWipeOn`: bool
- `mPreWipeTimer`: int (u16)
- `mPressAxis`: vec3
- `mPressCeilingSensor`: HitSensor (an object, read-only)
- `mPressEndTimer`: int (u32)
- `mPressKind`: int (u8)
- `mPressScale`: float
- `mPressSwellTimer`: int (u32)
- `mPressTimer`: int (u32)
- `mPrevCamDirZ`: vec3
- `mPrevControlPos`: vec3
- `mPrevHeadVec`: vec3
- `mPrevModForm`: int (u16)
- `mPrevPlayerMode`: int (u16)
- `mPrevViewMtx.mMtx`: float (array)
- `mPullSensors.mArr`: HitSensor (an object, read-only) (array)
- `mPullSensors.mSize`: int (s32)
- `mPunchFrame`: int (u8)
- `mPunchHitTimer`: int (u8)
- `mPunchSensorOn`: bool
- `mPunchTimerSensor`: bool
- `mPushSensorAngle`: float
- `mRasterScrollAmp`: float
- `mRasterScrollPhase`: int (u16)
- `mRasterScrollSet`: int (u8)
- `mRasterScrollWave`: float
- `mRawGravityVec`: vec3
- `mReflectAlpha`: int (u8)
- `mReflectPos`: vec3
- `mReflectSimpleAlpha`: int (u8)
- `mRemoteDemo`: bool
- `mRushButton`: int (u16)
- `mRushEndFront`: vec3
- `mRushEndFrontMode`: int (u16)
- `mRushJustEnded`: bool
- `mRushMove`: vec3
- `mRushSensor`: HitSensor (an object, read-only)
- `mRushSensorCount`: int (u32)
- `mRushSensorNear.mArr`: bool (array)
- `mRushSensorNear.mSize`: int (s32)
- `mRushSensors.mArr`: HitSensor (an object, read-only) (array)
- `mRushSensors.mSize`: int (s32)
- `mScoutPrevRange`: float
- `mScoutRange`: float
- `mScoutSensor`: HitSensor (an object, read-only)
- `mScreenBlurAlpha`: int (u8)
- `mScreenBlurGrow`: float
- `mScreenBlurOn`: int (u16)
- `mScreenBlurTrailAlpha`: int (u8)
- `mScreenBlurTrailGrow`: float
- `mScreenBoxMax.x`: float
- `mScreenBoxMax.y`: float
- `mScreenBoxMin.x`: float
- `mScreenBoxMin.y`: float
- `mScreenBoxPos.x`: float
- `mScreenBoxPos.y`: float
- `mScreenBoxSize.x`: float
- `mScreenBoxSize.y`: float
- `mSearchLight`: MarioSearchLight (an object, read-only)
- `mSearchLightTimer`: int (u32)
- `mShadowDrawer`: DrawAdaptor (an object, read-only)
- `mShadowMode`: int (u8)
- `mShadowRotate`: vec3
- `mSimpleModelHolder`: ModelHolder (an object, read-only)
- `mSmokeEntryCount`: int (u16)
- `mSpinButton`: int (u16)
- `mSpinEffectHeight`: float
- `mSpinEffectRadius`: float
- `mSpinIntervalTimer`: int (u16)
- `mSpinStarEndTimer`: int (u16)
- `mSpinStarTimer`: int (u16)
- `mSpinTico`: LiveActor (an object, read-only)
- `mSpinTicoTimer`: int (u16)
- `mSpine2Mtx.mMtx`: float (array)
- `mStunTimer`: int (u16)
- `mSubSpinBlock`: int (u16)
- `mSubSpinHeld`: bool
- `mSubSpinTrigger`: bool
- `mSuperKinokoCollected`: bool
- `mSwingInhibitTimer`: int (u16)
- `mSwingPermission`: bool
- `mSwingTrigger`: bool
- `mTakeCandidate`: HitSensor (an object, read-only)
- `mTakeCount`: int (u32)
- `mTakeRotations.mArr`: vec3 (array)
- `mTakeRotations.mSize`: int (s32)
- `mTakeSensors.mArr`: HitSensor (an object, read-only) (array)
- `mTakeSensors.mSize`: int (s32)
- `mTakenSensor`: HitSensor (an object, read-only)
- `mTaking`: bool
- `mTakingWobble`: float
- `mTeresaAlpha`: float
- `mTeresaBlinkTimer`: int (u16)
- `mTeresaFlyWeight`: float
- `mTeresaModel`: MarioParts (an object, read-only)
- `mTeresaTilt`: float
- `mThrowAimHold`: int (u16)
- `mThrowAimSensor`: HitSensor (an object, read-only)
- `mThrowMemoSensor`: HitSensor (an object, read-only)
- `mThrowProbePos`: vec3
- `mThrowTarget`: HitSensor (an object, read-only)
- `mThrowVecTimer`: int (u32)
- `mTornadoEnd`: int (u8)
- `mTornadoMario`: TornadoMario (an object, read-only)
- `mTrampleCombo`: int (u8)
- `mTrampleCounter`: CollectCounter (an object, read-only)
- `mTrampleJumpStep`: int (u8)
- `mTrampling`: bool
- `mTransforming`: bool
- `mTurtleShadow`: JetTurtleShadow (an object, read-only)
- `mTurtleShadowMtx.mMtx`: float (array)
- `mUpVec`: vec3
- `mViewMtx.mMtx`: float (array)
- `mWaitTimer`: int (u16)
- `mWallShadeArea`: AreaObj (an object, read-only)
- `mWallShadeAreaKind`: int (u8)
- `mWallShadeDist`: float
- `mWallShadeNormal`: vec3
- `mWallShadeOn`: int (u8)
- `mWallShadePos`: vec3
- `mWaterColumn`: ModelObj (an object, read-only)
- `mWaterLife`: int (u32)
- `mWaterReflectAlpha`: int (u8)
- `mWaterSurfacePos`: vec3
- `mWaterSurfaceRot`: vec3

## MarioHolder

- `mActor`: MarioActor (an object, read-only)

## MarioLauncher

- `mCameraTargetMtx.mCameraArea`: CubeCameraArea (an object, read-only)
- `mCameraTargetMtx.mFront`: vec3
- `mCameraTargetMtx.mGravityVector`: vec3
- `mCameraTargetMtx.mInvalidLastMove`: bool
- `mCameraTargetMtx.mLastMove`: vec3
- `mCameraTargetMtx.mPosition`: vec3
- `mCameraTargetMtx.mSide`: vec3
- `mCameraTargetMtx.mUp`: vec3
- `mHost`: LiveActor (an object, read-only)
- `mIsInAttractor`: bool
- `mLauncherType`: int (s32)
- `mLayout`: MarioLauncherLayout (an object, read-only)
- `mShell`: MarioLauncherShell (an object, read-only)

## MarioLauncherLayout

- `mLayout`: SimpleLayout (an object, read-only)

## MarioLauncherShell

- `mReboundDir`: vec3

## MarioMeter

- `mHitPointMeter`: MeterLayout (an object, read-only)
- `mSuddenDeathMeter`: SuddenDeathMeter (an object, read-only)
- `mUseSuddenDeath`: bool

## MarioNullBck

- `mLastFrame`: int (u16)

## MarioParts

- `mEffectName`: text (read-only)

## MarioSearchLight

- `mBeamDistance`: float
- `mHitTimer`: int (u16)
- `mTarget`: HitSensor (an object, read-only)

## MarioSubMeter

- `mActiveMeter`: SubMeterLayout (an object, read-only)
- `mAirMeter`: SubMeterLayout (an object, read-only)
- `mFlyMeter`: SubMeterLayout (an object, read-only)

## MatrixControl

- `mIndex.mCount`: int (u32)
- `mIndex.mMask`: int (u32)

## MatrixValueGetter

- `mIndex.mCount`: int (u32)
- `mIndex.mMask`: int (u32)

## MechaKoopaPartsArm

- `mIsRightArm`: bool

## MechanicKoopaMini

- `mIsAppearJumping`: bool
- `mIsForbidPause`: bool

## Meramera

- `mAppearStatus`: int (s32)
- `mBodyEffectType`: int (s32)
- `mCanDive`: bool
- `mChaseDistance`: float
- `mDashDir`: vec3
- `mDiveDir`: vec3
- `mDivePos`: vec3
- `mDiveUp`: vec3
- `mEffectType`: int (s32)
- `mElementType`: int (s32)
- `mFireUp`: vec3
- `mFront`: vec3
- `mHomePosition`: vec3
- `mIsValidRestart`: bool
- `mPlayerPosition`: vec3
- `mRunawayTimer`: int (s32)
- `mTargetDistance`: float
- `mWalkTarget`: vec3

## MercatorFixParts

- `mAppearController`: MapPartsAppearController (an object, read-only)

## MercatorRailMoveParts

- `mAppearController`: MapPartsAppearController (an object, read-only)
- `mIsNotMoving`: bool
- `mLocalRotation`: vec3
- `mLocalTrans`: vec3
- `mRailMover`: MapPartsRailMover (an object, read-only)
- `mRailRotator`: MapPartsRailRotator (an object, read-only)
- `mRotateMtx.mMtx`: float (array)

## MercatorRotateParts

- `mAppearController`: MapPartsAppearController (an object, read-only)
- `mLocalRotate`: vec3
- `mLocalTrans`: vec3
- `mRotator`: MapPartsRotator (an object, read-only)

## MercatorTransformCube

- `mPlacementMtx.mMtx`: float (array)

## MessageArea

- `mZoneID`: int (s32)

## Metbo

- `mFront`: vec3
- `mIsClipped`: bool
- `mWalkCenter`: vec3
- `mWalkDir`: vec3

## MeteorStrike

- `mBrokenModel`: ModelObj (an object, read-only)
- `mLavaColumnPos.mMtx`: float (array)
- `mRailDir`: vec3
- `mStepSize`: float
- `mStepsLeft`: int (s32)
- `mTotalSteps`: int (s32)
- `mType`: int (s32)

## MeteorStrikeLauncher

- `mCreateStep`: int (s32)
- `mIntervalStep`: int (s32)
- `mIsMeteorStrike`: bool
- `mIsValidMultiMeteor`: bool
- `mMeteorStrikeCount`: int (s32)

## MeterLayout

- `mAlpha`: float
- `mCount`: int (s32)
- `mFollowPos.x`: float
- `mFollowPos.y`: float
- `mFollowPosW.x`: float
- `mFollowPosW.y`: float
- `mMeterMoveStartPos.x`: float
- `mMeterMoveStartPos.y`: float
- `mVisibleRate`: float

## MiiFaceIcon

- `mHeight`: int (u16)
- `mIsCreated`: bool
- `mIsFavoriteColorBG`: bool
- `mIsRequestMakeIcon`: bool
- `mRecipe.mExpressions`: int (u32)
- `mRecipe.mFaceSize`: int (u32)
- `mWidth`: int (u16)

## MiiFaceIconHolder

- `mIconNum`: int (u32)
- `mIconNumMax`: int (u32)

## MiiFaceParts

- `mHead.mBuilt`: bool
- `mHead.mExpressions`: int (u32)
- `mHead.mFaceKeys`: int (u8) (array)
- `mRecipe.mExpressions`: int (u32)
- `mRecipe.mFaceSize`: int (u32)
- `mWaiting`: bool

## MiiFacePartsHolder

- `mReady`: bool

## MiiSelect

- `mFavoriteMiiNum`: int (u32)
- `mFellowIconNum`: int (u32)
- `mIsIconProhibited`: int (u8)
- `mMiiIndexRoom`: int (u32)
- `mMiiNum`: int (u32)
- `mPageBaseIndex`: int (s32)
- `mSelectedIndex`: int (s32)

## MiiSelectIcon

- `mIcon`: MiiFaceIcon (an object, read-only)
- `mIsEnabled`: bool
- `mIsMiiDummy`: bool

## MiniatureGalaxy

- `mGalaxyName`: text (read-only)
- `mIsZoomOut`: bool
- `mMysteryModel`: ModelObj (an object, read-only)
- `mNamePlate`: GalaxyNamePlate (an object, read-only)
- `mObjectName`: text (read-only)
- `mOrbit`: AstroDomeOrbit (an object, read-only)
- `mSelectModel`: ModelObj (an object, read-only)
- `mShadowModel`: ModelObj (an object, read-only)
- `mStarPlateModel`: ModelObj (an object, read-only)
- `mState`: int (s32)
- `mType`: int (s32)
- `mZoomStep`: int (s32)

## MiniatureGalaxyHolder

- `mCometGalaxy`: MiniatureGalaxy (an object, read-only)
- `mCometID`: int (s32)
- `mIsRegistered`: bool
- `mMiniatureGalaxyGroup`: LiveActorGroup (an object, read-only)

## MirrorActor

- `mCenter`: vec3
- `mHost`: LiveActor (an object, read-only)
- `mIsDisconnected`: bool
- `mRadius`: float

## MirrorCamera

- `mMirrorNormal`: vec3
- `mMirrorPos`: vec3
- `mPlaneD`: float

## MirrorReflectionModel

- `mHost`: LiveActor (an object, read-only)
- `mIsHostGone`: bool

## Mogu

- `mHole`: ModelObj (an object, read-only)
- `mIsCannonFleet`: bool
- `mSight`: vec3
- `mStone`: MoguStone (an object, read-only)
- `mThrowOnGravity`: bool
- `mUp`: vec3

## MoguStone

- `mIsThrowHorizontal`: bool
- `mThrowDir`: vec3
- `mThrowSpeed`: float

## Mogucchi

- `mHill`: MogucchiHill (an object, read-only)
- `mHole`: ModelObj (an object, read-only)
- `mIsStoppedByP2`: bool
- `mMaxStrollSpeed`: float
- `mRailGravity`: vec3
- `mScatterDir`: vec3
- `mScatterUpSpeed`: float
- `mStrollSpeed`: float

## MogucchiHillPiece

- `mAutoEffectName`: text (read-only)
- `mHasLightCtrl`: bool
- `mHost`: MogucchiHill (an object, read-only)
- `mIsHostLeft`: bool
- `mIsHostTouching`: bool
- `mJointName`: text (read-only)
- `mModelName`: text (read-only)

## MogucchiShooter

- `mFront`: vec3

## MorphItemObjNeo

- `mAngle`: float
- `mAppearFrames`: int (s32)
- `mBoundCount`: int (u8)
- `mCameraMode`: int (u8)
- `mCameraTargetMtx`: CameraTargetMtx (an object, read-only)
- `mContainerBreakModel`: ModelObj (an object, read-only)
- `mContainerModel`: ModelObj (an object, read-only)
- `mContainerType`: int (u8)
- `mIsBind`: int (s8)
- `mIsDemoPending`: bool
- `mIsFloating`: bool
- `mIsSwitchAppearOnce`: bool
- `mIsUseSwitch`: bool
- `mMorphType`: int (s32)
- `mRotateMode`: int (s8)
- `mSpawnPosition`: vec3
- `mSpiralTime`: int (s32)
- `mZRotation`: vec3

## MovementOnOffGroupHolder

- `mGroups.mCount`: int (s32)

## MoviePlayerSimple

- `mInputCheckedBefore`: bool
- `mLoop`: bool
- `mMovieName`: text (read-only)

## MoviePlayingSequence

- `mSubtitles.mCount`: int (s32)

## MoviePlayingSequenceHolder

- `mSequence.mMaxSize`: int (s32)

## MovieStarter

- `mMovieType`: int (s32)

## MovieSubtitles

- `mAppearTime`: int (s32)

## MsgSharedGroup

- `mMsg`: int (u32)
- `mSensor`: HitSensor (an object, read-only)
- `mSensorName`: text (read-only)

## MultiSceneActor

- `mEffectSystem`: EffectSystem (an object, read-only)
- `mIsDead`: bool
- `mIsScenarioSelectSky`: bool
- `mRotation`: vec3
- `mScale`: vec3
- `mTranslation`: vec3

## NameObjExecuteHolder

- `mConnectDraw.mCount`: int (s32)
- `mConnectDraw.mWords`: int (s32)
- `mConnectMovement.mCount`: int (s32)
- `mConnectMovement.mWords`: int (s32)
- `mDisconnectDraw.mCount`: int (s32)
- `mDisconnectDraw.mWords`: int (s32)
- `mDisconnectMovement.mCount`: int (s32)
- `mDisconnectMovement.mWords`: int (s32)
- `mExecuteArrayMaxSize`: int (s32)
- `mExecuteArraySize`: int (s32)

## NamePosHolder

- `mIndex.mCount`: int (u32)
- `mIndex.mMask`: int (u32)
- `mPosNum`: int (s32)

## NebulaAchievementBoard

- `mBackground`: GalaxyMapBackground (an object, read-only)
- `mIsOpen`: bool

## Tico

- `mBehavior`: int (u32)
- `mDemoStarter.mActor`: NPCActor (an object, read-only)
- `mFloatOffset`: float
- `mPrevPos`: vec3
- `mTicoTalkCtrl`: TalkMessageCtrl (an object, read-only)

## NebulaAchievementToast

- `mFrame`: int (s32)
- `mHeight`: float
- `mHoldFrames`: int (s32)
- `mStep`: int (s32)
- `mWidth`: float

## NebulaFileSelectButton

- `mIsDecided`: bool
- `mShownFrames`: int (s32)

## NebulaModText

- `mAny`: bool
- `mHeight`: float
- `mRevision`: int (u32)
- `mScaleX`: float
- `mWidth`: float

## NeedlePlant

- `mObjArg0`: int (s32)

## NokonokoLand

- `mAppearPos`: vec3
- `mJetTurtle`: JetTurtle (an object, read-only)
- `mRailPointPassChecker`: MapPartsRailPointPassChecker (an object, read-only)
- `mTurnAxis`: vec3
- `mTurtleType`: int (s32)

## NormalMapBase

- `mAmbAlpha`: int (u8)
- `mAmbColor`: int (u8)
- `mBackLightMode`: int (u8)
- `mDrawFlags`: int (u16)
- `mGradTexMode`: int (s32)
- `mHardLightAlphaMask`: int (u16)
- `mHardLightColorMask`: int (u16)
- `mLightAMode`: int (u8)
- `mLightBMode`: int (u8)
- `mLightingHighLevel`: int (u8)
- `mLightingLowLevel`: int (u8)
- `mMatAlpha`: int (u8)
- `mMatColor`: int (u8)
- `mTexFlags`: int (u16)
- `mUseModelLightReg`: int (u16)
- `mUseModelStageColor`: int (u16)

## Note

- `mBrkFrame`: float
- `mCounter`: int (int)
- `mFlashCtrl`: FlashingCtrl (an object, read-only)
- `mIsCountdown`: bool
- `mParentFairy`: NoteFairy (an object, read-only)
- `mRailDirection`: vec3

## NoteCounter

- `mHost`: LiveActor (an object, read-only)
- `mMelodyNo`: int (s32)
- `mNoteAddNum`: int (s32)
- `mNoteMissNum`: int (s32)
- `mNoteNum`: int (s32)
- `mRailLength`: float

## NoteFairy

- `mAppearanceSpeed`: float
- `mAppearanceType`: int (s32)
- `mAppearedNoteNum`: int (s32)
- `mCompletionJingle`: int (s32)
- `mDemoJumpPos`: vec3
- `mHasDemo`: bool
- `mInitPos`: vec3
- `mIsDemoUntilAllNotes`: bool
- `mIsLowerBgm`: bool
- `mIsNoDemo`: bool
- `mIsNoRiddleSE`: bool
- `mIsNoteAtRailPoints`: bool
- `mLastGotNote`: Note (an object, read-only)
- `mMelodyNoteNum`: int (s32)
- `mNoteCoord`: float
- `mPowerStarSpawnType`: int (s32)
- `mSong`: int (s32)
- `mTimeLimit`: int (s32)

## NoteGroup

- `mRotation`: float

## OceanBowl

- `mBloomDrawer`: OceanBowlBloomDrawer (an object, read-only)
- `mFront`: vec3
- `mIsClipped`: bool
- `mRadius`: float
- `mSide`: vec3
- `mTexStep`: float
- `mTexU0`: float
- `mTexU1`: float
- `mTexU2`: float
- `mTexV0`: float
- `mTexV1`: float
- `mTexV2`: float
- `mUp`: vec3
- `mWaveX`: float
- `mWaveZ`: float

## OceanBowlBloomDrawer

- `mHost`: OceanBowl (an object, read-only)

## OceanFloaterLandParts

- `mAlreadyDoneFlag`: int (s32)
- `mIsMoving`: bool
- `mIsRegisteredDemo`: bool
- `mObjectName`: text (read-only)
- `mRailEndPos`: vec3
- `mRailMover`: MapPartsRailMover (an object, read-only)

## OceanHomeMapCtrl

- `mOceanHomePlanet`: PlanetMap (an object, read-only)
- `mOceanRingPlanet`: PlanetMap (an object, read-only)
- `mOceanRingPlanetLowInWater`: ModelObj (an object, read-only)

## OceanRing

- `mBloomDrawer`: OceanRingBloomDrawer (an object, read-only)
- `mClippingBox.f`: vec3
- `mClippingBox.i`: vec3
- `mClippingCenter`: vec3
- `mIsClipped`: bool
- `mNearestDir`: vec3
- `mNearestPos`: vec3
- `mNearestToCam`: vec3
- `mNearestToWatchCam`: vec3
- `mOceanRingPipe`: OceanRingPipe (an object, read-only)
- `mPlayerCoord`: float
- `mSegCount`: int (s32)
- `mStride`: int (s32)
- `mUpdateBox.f`: vec3
- `mUpdateBox.i`: vec3
- `mUpdateSegsAhead`: int (s32)
- `mWaterPointNum`: int (s32)
- `mWaveHeight1`: float
- `mWaveHeight2`: float
- `mWaveTheta0`: float
- `mWaveTheta1`: float
- `mWidthMax`: float

## OceanRingBloomDrawer

- `mRing`: OceanRing (an object, read-only)

## OceanRingPipe

- `mIsCameraInside`: bool
- `mNumPoints`: int (u32)
- `mNumSegments`: int (s32)
- `mNumSides`: int (s32)
- `mOceanRing`: OceanRing (an object, read-only)
- `mPipeInside`: OceanRingPipeInside (an object, read-only)
- `mPipeOutside`: OceanRingPipeOutside (an object, read-only)
- `mWidth`: float
- `mWidthArg`: float

## OceanRingPipeInside

- `mRingPipe`: OceanRingPipe (an object, read-only)
- `mTexU0`: float
- `mTexU1`: float
- `mTexV0`: float
- `mTexV1`: float

## OceanRingPipeOutside

- `mRingPipe`: OceanRingPipe (an object, read-only)
- `mTexU`: float

## OceanSmallTurtle

- `isBound`: int (u8)
- `isStartFloat`: int (u8)
- `mAccelRate`: float
- `mArg1`: float
- `mBoundDisplacement`: float
- `mBoundTimer`: int (s32)
- `mFloatDelayTimer`: float
- `mFloatUpSpeed`: float
- `mInfoRotate`: vec3
- `mIsOnPlayer`: int (u8)
- `mMercatorPos`: vec3
- `mRailClippingPos`: vec3
- `mRailClippingRadius`: float
- `mRailMover`: MapPartsRailMover (an object, read-only)
- `mStartNearestRailPos`: vec3
- `mStartRotationX`: float

## OceanSphere

- `mAlwaysUseRealDrawing`: bool
- `mEnableStartPosCameraSwitch`: bool
- `mIsCameraInside`: bool
- `mIsStartPosCamera`: bool
- `mPointCount`: int (s32)
- `mRadius`: float
- `mRadiusTarget`: float
- `mTexOffs0X`: float
- `mTexOffs0Y`: float
- `mTexOffs1X`: float
- `mTexOffs1Y`: float
- `mUseMesh`: bool
- `mWaveTime1`: float
- `mWaveTime2`: float

## OceanWaveFloater

- `mCanRipple`: bool
- `mMapGroundUpper`: vec3
- `mOffset`: float
- `mRipplePos`: vec3
- `mSoundDelay`: int (s32)
- `mSpring`: FloaterFloatingForceTypeSpring (an object, read-only)

## Onimasu

- `mClipCenter`: vec3
- `mRotateAxis`: vec3

## OnimasuJump

- `mCurNormal`: int (s32)
- `mNormals.mMaxSize`: int (s32)

## OnimasuPivot

- `mCurNormal`: int (s32)
- `mNormals.mMaxSize`: int (s32)

## OnlyCamera

- `mCalcIdeal`: bool
- `mIsResetting`: bool
- `mPos`: vec3
- `mSpeed`: float
- `mStartPose`: bool

## OtaKing

- `mBckLBuffer.mInline`: int (char) (array)
- `mBckLBuffer.mText`: text (read-only)
- `mBckRBuffer.mInline`: int (char) (array)
- `mBckRBuffer.mText`: text (read-only)
- `mCocoNutBallArray`: CocoNutBall (an object, read-only)
- `mDownCocoNut`: CocoNutBall (an object, read-only)
- `mFeet`: PartsModel (an object, read-only) (array)
- `mFireBallArray`: FireBall (an object, read-only)
- `mFireBubbleArray`: FireBubble (an object, read-only)
- `mHits`: int (s32)
- `mIsLv2`: bool
- `mLongFeet`: OtaKingLongFoot (an object, read-only) (array)
- `mMagma`: OtaKingMagma (an object, read-only)
- `mSoundObjPos`: vec3
- `mThrowNum`: int (s32)
- `mTurnSpeed`: float

## OtaKingLongFoot

- `mDemoBckStep`: int (s32)

## OtaKingMagma

- `mBloomModel`: PartsModel (an object, read-only)

## OtaRock

- `mCocoNutArray`: CocoNutBall (an object, read-only)
- `mFireBallArray`: FireBall (an object, read-only)
- `mFront`: vec3
- `mNoThrowCocoNut`: bool
- `mThrowCocoNutCounter`: int (s32)
- `mWaitFrame`: int (u32)

## PTimerSwitch

- `mIsPressed`: bool
- `mPressStep`: int (s32)

## PackunPetit

- `mBlownModel`: ModelObj (an object, read-only)
- `mDontTurn`: bool
- `mFront`: vec3

## PalmIsland

- `mFloatStartStep`: int (s32)
- `mIsRising`: int (s32)
- `mRipplePos`: vec3

## PauseMenu

- `mCoinNumPos.x`: float
- `mCoinNumPos.y`: float
- `mConfirmWindow`: SysInfoWindow (an object, read-only)
- `mLuigiLetter`: LuigiLetter (an object, read-only)
- `mSettingsButton`: NebulaPauseSettingsButton (an object, read-only)
- `mStageTitleOffset.x`: float
- `mStageTitleOffset.y`: float
- `mStarPieceNumPos.x`: float
- `mStarPieceNumPos.y`: float
- `mWasDefaultLayoutActive`: bool

## PeachLetter

- `mAButtonIcon`: IconAButton (an object, read-only)

## Penguin

- `mBehavior`: int (s32)
- `mStepToDive`: int (s32)

## PenguinCoach

- `mActor`: LiveActor (an object, read-only)
- `mBehavior`: int (s32)
- `mIsRaceComplete`: bool

## PenguinRacer

- `mMoveSpeed`: float
- `mRaceDisabled`: int (s32)
- `mRailPointNo`: int (s32)
- `mSpeed`: float

## PenguinSkater

- `mCrossPoint`: int (s32)
- `mCurrentRail`: NPCSupportRail (an object, read-only)
- `mMarioPos`: vec3
- `mNumRails`: int (s32)
- `mRail`: NPCSupportRail (an object, read-only)
- `mSwitchRail`: NPCSupportRail (an object, read-only)
- `mSwitchRails.mArr`: NPCSupportRail (an object, read-only) (array)
- `mSwitchRails.mSize`: int (s32)

## PenguinStudent

- `mActor`: LiveActor (an object, read-only)
- `mObjArg0`: int (s32)
- `mRailOffset`: float

## Petari

- `mBodyCenter`: vec3
- `mFootPrint`: FootPrint (an object, read-only)
- `mFootprintYOffs`: float
- `mFront`: vec3
- `mHeadMtx.mMtx`: float (array)
- `mMeanderAngle`: float
- `mMeanderStep`: int (s32)
- `mRequestSmoke`: bool
- `mStarPieceRemaining`: int (s32)
- `mStarPieceTotal`: int (s32)
- `mTargetDir`: vec3

## PhantomShipBoxFloater

- `mFloaterOffset`: vec3
- `mFloatingForce`: FloaterFloatingForceTypeSpring (an object, read-only)
- `mObjectName`: text (read-only)
- `mRippleSeCounter`: int (s32)
- `mSinkDepthOver`: float
- `mSinkDepthUnder`: float

## PhantomShipBridge

- `mIsNutShipBridge`: int (s32)

## PhantomShipHandle

- `mRotator`: MapPartsSeesaw1AxisRotator (an object, read-only)

## PhantomTorch

- `mBurnSe`: text (read-only)
- `mIsDecorative`: bool
- `mItem`: int (s32)
- `mParticle`: text (read-only)

## PictureBookCloseButton

- `mCanCloseWithPad`: bool

## PictureBookLayout

- `mChapterMax`: int (s32)
- `mChapterMin`: int (s32)
- `mChapterNo`: int (s32)
- `mChapterRosettaMax`: int (s32)
- `mCloseButton`: PictureBookCloseButton (an object, read-only)
- `mIconAButton`: IconAButton (an object, read-only)
- `mIsNextItemFast`: bool
- `mNextItemDir`: int (s32)
- `mNotReadedChapterNo`: int (s32)
- `mNotReadedPageNo`: int (s32)
- `mNotReadedTextIndex`: int (s32)
- `mPageNo`: int (s32)
- `mTextIndex`: int (s32)

## PlacementStateChecker

- `mCurrentZoneId`: int (int)

## PlanetGravityManager

- `mMaxGravities`: int (s32)
- `mNumGravities`: int (s32)

## PlanetMapCreator

- `mIndex.mCount`: int (u32)
- `mIndex.mMask`: int (u32)
- `mTableCount`: int (s32)

## Plant

- `mAccelTimer`: int (s32)
- `mClippingCenter`: vec3
- `mFront`: vec3
- `mGrabbedTop`: bool
- `mJumpSpeed`: float
- `mJumpSpeedOffsetY`: float
- `mNumLeaves`: int (s32)
- `mPlayAppearDemo`: bool
- `mRailCoord`: float
- `mRideVelocity`: float
- `mRider`: LiveActor (an object, read-only)
- `mSeedPartsModel`: PartsModel (an object, read-only)
- `mStalk`: PlantStalk (an object, read-only)
- `mTopPartsModel`: PartsModel (an object, read-only)
- `mUp`: vec3

## PlantGroup

- `mHintIndex`: int (s32)
- `mHintPosition`: vec3
- `mHintRotation`: vec3
- `mHintTimer`: int (s32)
- `mIsStarPiece`: bool
- `mMemberCount`: int (s32)
- `mPlantType`: int (s32)

## PlantLeaf

- `mFront`: vec3
- `mLeafCoord`: float
- `mLeafSize`: float
- `mSide`: vec3
- `mSpringCoord`: float
- `mSpringSpeed`: float
- `mUp`: vec3

## PlantMember

- `mHasItem`: bool
- `mTouchType`: int (s32)

## PlantStalk

- `mGrowAccelTime`: int (s32)
- `mGrowSpeed`: float
- `mGrownPlantPoints`: int (s32)
- `mGrowthPercent`: float
- `mNumPlantPoints`: int (s32)
- `mStalkLength`: float

## PlantStalkDrawInit

- `mMaterial.mRowCount`: int (u32)
- `mMaterial.mTextureCount`: int (u32)

## PlayerActionGuidance

- `mCurrentLayout`: LayoutActor (an object, read-only)
- `mGuidancePrevState`: int (s32)
- `mGuidanceState`: int (s32)
- `mShow`: bool
- `mSpinLayout`: SimpleLayout (an object, read-only)
- `mTamakoroForced`: bool
- `mTamakoroLayout`: SimpleLayout (an object, read-only)

## PlayerLeft

- `mIsForceAppear`: bool
- `mPlayerLeft`: int (s32)

## PlayerSeArea

- `mSeType`: int (s32)

## Poihana

- `mBehavior`: int (s32)
- `mBindedActor`: LiveActor (an object, read-only)
- `mBoundTimer`: int (s32)
- `mCanDrown`: bool
- `mFrontVec`: vec3
- `mHomePos`: vec3
- `mIsActive`: bool
- `mLaunchIntensity`: float
- `mRandDir`: int (s32)
- `mRespawnPos`: vec3
- `mWaterColumn`: ModelObj (an object, read-only)

## Pole

- `mBaseMtx.mMtx`: float (array)
- `mBasePos`: vec3
- `mCenterPos`: vec3
- `mClimbCoord`: float
- `mClimbSpeed`: float
- `mDisableHandstand`: bool
- `mFront`: vec3
- `mHeight`: float
- `mInvertStick`: bool
- `mIsSquare`: bool
- `mNoModel`: bool
- `mRider`: LiveActor (an object, read-only)
- `mSide`: vec3
- `mTopPos`: vec3
- `mUp`: vec3
- `mUsePole9mCollision`: bool

## Polta

- `mBombTeresaHolder`: BombTeresaHolder (an object, read-only)
- `mBreakModel`: ModelObj (an object, read-only)
- `mFormationModel`: ModelObj (an object, read-only)
- `mFront`: vec3
- `mGroundRockHolder`: PoltaGroundRockHolder (an object, read-only)
- `mLeftArm`: PoltaArm (an object, read-only)
- `mRightArm`: PoltaArm (an object, read-only)
- `mRockHolder`: PoltaRockHolder (an object, read-only)
- `mRotateSpeed`: float
- `mStartPos`: vec3

## PoltaArm

- `mBreakModel`: ModelObj (an object, read-only)
- `mBrokenCounter`: int (s32)
- `mFormationModel`: ModelObj (an object, read-only)
- `mHitPoint`: int (s32)
- `mHitPointMax`: int (s32)
- `mIsLeftArm`: bool

## PoltaGroundRock

- `mBreakModel`: ModelObj (an object, read-only)
- `mGroundPos`: vec3
- `mOwner`: LiveActor (an object, read-only)

## PoltaRock

- `mChaseDir`: vec3
- `mOwner`: Polta (an object, read-only)
- `mOwnerLocalPos`: vec3

## PomponPlant

- `mCanGenItem`: bool
- `mItemCount`: int (s32)
- `mItemType`: int (s32)

## PowerStar

- `mAppearPosition`: vec3
- `mCameraActor`: PowerStarAppearPoint (an object, read-only)
- `mColor`: int (s32)
- `mInitPosition`: vec3
- `mInitRotation`: vec3
- `mIsCameraActorPose`: bool
- `mIsForceAppear`: bool
- `mIsGrandStar`: bool
- `mIsInDemo`: bool
- `mIsNoPostureFront`: bool
- `mIsRiseOnGravity`: bool
- `mIsWeak`: bool
- `mLuigiNPC`: ModelObj (an object, read-only)
- `mPlayerMtx.mMtx`: float (array)
- `mPowerStarId`: int (s32)
- `mPowerStarModelObj`: ModelObj (an object, read-only)

## PowerStarHalo

- `mAppearDuringOpeningCam`: int (s32)

## PowerStarHolder

- `mInfos.mSize`: int (s32)
- `mNumInfos`: int (s32)

## PowerStarList

- `mPageNo`: int (s32)
- `mPageNum`: int (s32)
- `mSysInfoWindow`: SysInfoWindow (an object, read-only)

## PressureMessenger

- `mSharedGroup`: MsgSharedGroup (an object, read-only)
- `mWaitSyncTime`: int (s32)

## PriorDrawAirHolder

- `mAirCount`: int (s32)
- `mAirs.mArr`: PriorDrawAir (an object, read-only) (array)
- `mAirs.mSize`: int (s32)

## PrizeRing

- `mLifeTime`: int (s32)

## Sky

- `mReflectionModel`: MirrorReflectionModel (an object, read-only)
- `mSpaceInner`: SpaceInner (an object, read-only)

## PrologueDirector

- `mCameraTarget`: CameraTargetMtx (an object, read-only)
- `mLetter`: PrologueLetter (an object, read-only)
- `mMarioPosDummyModel`: ModelObj (an object, read-only)
- `mPictureBook`: ProloguePictureBook (an object, read-only)
- `mPlayerBaseMtx.mMtx`: float (array)
- `mScenery`: ModelObj (an object, read-only)
- `mUseSwitchA`: bool

## PrologueHolder

- `mDirector`: PrologueDirector (an object, read-only)

## PrologueLetter

- `mAButtonIcon`: IconAButton (an object, read-only)

## ProloguePictureBook

- `mIconAButton`: IconAButton (an object, read-only)
- `mPage`: int (u32)

## Pukupuku

- `mBckBeforeBind`: text (read-only)
- `mBckFrameBeforeBind`: float
- `mBinderOffset`: vec3
- `mBtpFrameBeforeBind`: float
- `mClipCenter`: vec3
- `mIsTrampled`: bool

## PunchBox

- `mCoinCount`: int (s32)
- `mFloorTouchTimer`: int (s32)
- `mHasShadow`: bool
- `mHitCoolDown`: int (s32)
- `mHitPoint`: int (u16)
- `mIsPlayerNear`: bool
- `mShadowStep`: int (u32)

## PunchingKinoko

- `mCameraTarget`: vec3
- `mCameraVecId`: int (s32)
- `mGroundChecker`: GroundChecker (an object, read-only)
- `mInvincibleHitCoolDown`: int (s32)
- `mIsPointerHeld`: bool
- `mShadowNormal`: vec3
- `mStarPointerHitCoolDown`: int (s32)

## PurpleCoinCounter

- `mInvalidCountUpFrame`: int (s32)
- `mIsValid`: bool
- `mPurpleCoinDisplayNum`: int (s32)
- `mPurpleCoinNum`: int (s32)

## PurpleCoinHolder

- `mStarter`: PurpleCoinStarter (an object, read-only)

## PurpleCoinStarter

- `mHost`: PurpleCoinHolder (an object, read-only)

## QuestionCoin

- `mBoundActor`: LiveActor (an object, read-only)
- `mCalcShadow`: bool
- `mIsWeakGravity`: bool
- `mLaunchSpeed`: float
- `mPoseType`: int (s32)
- `mShadowDropLength`: float
- `mShadowDropLengthType`: int (s32)
- `mShadowPos`: vec3

## Rabbit

- `mBehavior`: int (s32)
- `mIsFalling`: bool
- `mIsLanded`: int (u16)
- `mJumpHeight`: float
- `mJumpScale`: float
- `mJumpSpeed`: float
- `mMoveDir`: vec3
- `mMoveSpeed`: float
- `mWaitStep`: int (s32)

## RaceManager

- `mAudience.mMovedSize`: int (s32)
- `mAudienceNum`: int (u32)
- `mBestTime`: int (u32)
- `mCurrentRace`: int (s32)
- `mLayout`: RaceManagerLayout (an object, read-only)
- `mPlayerRacer`: PlayerRacer (an object, read-only)
- `mRacer.mMovedSize`: int (s32)
- `mRacerNum`: int (u32)
- `mRank`: int (u32)
- `mTime`: int (u32)

## RaceRail

- `mPlayerRacer`: PlayerRacer (an object, read-only)

## RailBlock

- `mIsConnectedToRail`: bool
- `mMoveTime`: int (s32)
- `mRailSpeed`: int (s32)

## RainCloud

- `mClippingCenter`: vec3
- `mCloudCylinder`: ModelObj (an object, read-only)
- `mIsOnPlayer`: bool
- `mNoRain`: bool
- `mRailMover`: MapPartsRailMover (an object, read-only)
- `mRainLength`: float
- `mShadowDropLength`: float

## RandomEffectObj

- `mIsFireworks`: bool
- `mLifeTime`: int (s32)
- `mRandWidth`: int (s32)
- `mSeStep`: int (s32)
- `mStep`: int (s32)

## RestartCube

- `mBgmIndex`: int (s32)
- `mBgmStateIndex`: int (s32)
- `mIsBgmChanged`: bool

## RevolvingWay

- `mRadius`: float
- `mRotateMoment`: vec3

## RingBeam

- `mBeamGravity`: vec3
- `mBloomModel`: ModelObj (an object, read-only)
- `mEdgePos`: vec3
- `mFront`: vec3
- `mHasShadow`: bool
- `mHost`: LiveActor (an object, read-only)
- `mIsFollowGravity`: bool
- `mLife`: int (s32)
- `mShadowDrawer`: RingBeamShadowDrawer (an object, read-only)
- `mShadowModel`: ModelObj (an object, read-only)
- `mSoundPos`: vec3
- `mSpeed`: float

## RingBeamShadowDrawer

- `mHost`: LiveActor (an object, read-only)
- `mRadius`: float

## Rock

- `mAirTime`: int (u32)
- `mAppearAngle`: float
- `mAppearPos`: vec3
- `mAppearTime`: int (s32)
- `mBreakModel`: ModelObj (an object, read-only)
- `mBreakModelOnRailGoal`: bool
- `mCreator`: RockCreator (an object, read-only)
- `mCurrentRailPoint`: int (s32)
- `mFallVelocity`: vec3
- `mFreezePos`: vec3
- `mFreezeTime`: int (s32)
- `mFront`: vec3
- `mGravityRate`: float
- `mInvalidBindTime`: int (s32)
- `mIsNormalGravity`: bool
- `mMoveSpeed`: float
- `mPrevPos`: vec3
- `mRadius`: float
- `mRollSoundTimer`: int (s32)
- `mRotateSpeed`: float
- `mSlowDownOnAttack`: bool
- `mSlowDownTimer`: int (s32)
- `mWanwanVoiceTimer`: int (s32)

## RockCreator

- `mBreakModelOnRailGoal`: bool
- `mIsValid`: bool
- `mNumRocks`: int (s32)
- `mRockSpeed`: float
- `mRocks`: Rock (an object, read-only)
- `mSpawnDelay`: int (s32)
- `mSpawnTimer`: int (u32)

## Rosetta

- `mFadeStarter.mActor`: NPCActor (an object, read-only)
- `mFadeStarter.mFadeTime`: int (s32)
- `mObjArg0`: int (s32)

## RosettaChair

- `mDefaultPosition`: vec3
- `mDefaultRotation`: vec3
- `mScaleMtx.mMtx`: float (array)

## RosettaMonologue

- `mIconAButton`: IconAButton (an object, read-only)
- `mMessageNo`: int (s32)
- `mTextFormer.mFormMode`: int (s32)
- `mTextFormer.mHostActor`: LayoutActor (an object, read-only)
- `mTextFormer.mPaneName`: text (read-only)

## RosettaPictureBook

- `mIconAButton`: IconAButton (an object, read-only)
- `mIsValidOpenIconAButton`: bool
- `mLayout`: PictureBookLayout (an object, read-only)

## RosettaReading

- `mPictureBookLayout`: PictureBookLayout (an object, read-only)

## RotateMoveObj

- `mIsGear`: bool

## RunawayRabbit

- `mCollect`: RunawayRabbitCollect (an object, read-only)
- `mFootPrint`: FootPrint (an object, read-only)
- `mFrontVec`: vec3
- `mGroupId`: int (s32)
- `mHideLocation`: int (s32)
- `mIsActive`: bool
- `mIsLinked`: bool
- `mMsgCtrl`: TalkMessageCtrl (an object, read-only)
- `mNearSoundRadius`: float
- `mNotCaughtableTimer`: int (s32)
- `mPlayerBindPos`: vec3
- `mPressureTimer`: int (s32)
- `mRunawayLevel`: int (s32)
- `mSpotMarkLight`: SpotMarkLight (an object, read-only)

## RunawayRabbitCollect

- `mBgmState`: int (s32)
- `mCaughtRabbitCount`: int (s32)
- `mCompleteRabbitCount`: int (s32)
- `mIsAppearTico`: bool (array)
- `mRabbitNum`: int (s32)
- `mTicoNum`: int (s32)

## RunawayTico

- `mDemoCastID`: int (s32)
- `mIsAllCaught`: bool
- `mIsStartRunaway`: bool
- `mRole`: int (s32)

## SamboHead

- `mFront`: vec3
- `mInitPos`: vec3
- `mIsPursueAlways`: bool

## SandCapsulePressGround

- `mLowerPos`: vec3
- `mUpperPos`: vec3

## SandUpDownEffectObj

- `mClippingCenter`: vec3
- `mLineLength`: float
- `mLineOffset`: float

## SandUpDownTriRock

- `mRestStep`: int (s32)

## Sandstorm

- `mAdjustStartRadius`: float
- `mBindActor`: LiveActor (an object, read-only)
- `mBindHeight`: float
- `mBindOffsetY`: float
- `mBindRadius`: float
- `mBindStartStep`: float
- `mBindStep`: int (s32)
- `mClippingCenter`: vec3
- `mFront`: vec3
- `mSensorHeight`: float
- `mSensorMin`: float
- `mSizeUpRate`: float
- `mSpiralAngle`: float
- `mSpiralRadius`: float
- `mSpiralRot`: float
- `mStepAdjust`: int (s32)
- `mStepEnd`: int (s32)
- `mStepFix`: int (s32)
- `mStepStorm`: int (s32)
- `mType`: int (s32)

## SaveIcon

- `mActor`: LayoutActor (an object, read-only)

## ScenarioDataParser

- `mNames.mCount`: int (u32)
- `mNames.mMask`: int (u32)
- `mScenarioDataNum`: int (s32)

## ScenarioSelectLayout

- `mBackButton`: BackButton (an object, read-only)
- `mCameraContext`: CameraContext (an object, read-only)
- `mEffectHostMtx.mMtx`: float (array)
- `mEffectSystem`: EffectSystem (an object, read-only)
- `mIsDecided`: bool
- `mListNum`: int (s32)
- `mListPage`: int (s32)
- `mMarioPaneFollowPos.x`: float
- `mMarioPaneFollowPos.y`: float
- `mMarioPaneName`: text (read-only)
- `mNewGreenTextFollowPos.x`: float
- `mNewGreenTextFollowPos.y`: float
- `mNewTextFollowPos.x`: float
- `mNewTextFollowPos.y`: float
- `mPageKeyHeld`: bool
- `mScenarioSky`: MultiSceneActor (an object, read-only)
- `mSelectedScenarioNo`: int (s32)
- `mStarTopFollowPos.x`: float
- `mStarTopFollowPos.y`: float
- `mTextFadeState`: int (s32)
- `mTextScenarioNo`: int (s32)

## ScenarioSelectStar

- `mAppearFrame`: int (int)
- `mBasePos`: vec3
- `mIsPointing`: bool
- `mRotateSpeed`: float
- `mRotateTime`: int (s32)
- `mScaleOnSelect`: float
- `mScenarioNo`: int (s32)
- `mStarCollectedStatus`: int (int)
- `mStarId`: int (s32)
- `mTranslationOnSelect`: vec3

## ScenarioStarter

- `mBindActor`: LiveActor (an object, read-only)
- `mFallAngle`: float
- `mFallMotionStartStep`: int (s32)
- `mFlightTime`: int (s32)
- `mFlyEndStep`: int (s32)
- `mFlyMotionStartStep`: int (s32)
- `mHeadDir`: vec3
- `mInitFront`: vec3
- `mInitUp`: vec3
- `mInitalPlayerRotation`: float
- `mIsSkipped`: bool
- `mJumpVec`: vec3
- `mSpinAngle`: float
- `mSpinAngleMax`: float
- `mStartPos`: vec3
- `mStartRotate`: vec3
- `mTitle`: ScenarioTitle (an object, read-only)
- `mWelcomeLayout`: SimpleLayout (an object, read-only)

## SceneDataInitializer

- `mDataHolder`: StageDataHolder (an object, read-only)

## SceneNameObjMovementController

- `mIsSyncRequested`: bool

## ScenePlayingResult

- `mCoinNum`: int (s32)
- `mPurpleCoinNum`: int (s32)

## WipeHolderBase

- `mCurrentWipeLayout`: WipeLayoutBase (an object, read-only)
- `mWipeLayoutCount`: int (u32)

## ScreenBlurArea

- `mIsPlayerSync`: bool

## ScreenBlurEffect

- `mIntensity`: float
- `mScale`: float
- `mSkipDraw`: bool

## ScreenPreserver

- `mIsActive`: bool

## ScrewSwitch

- `mBindActor`: LiveActor (an object, read-only)

## ScrewSwitchReverse

- `mHost`: LiveActor (an object, read-only)
- `mIsForceJump`: bool

## SeaGull

- `mBankAngle`: float
- `mBankedUp`: vec3
- `mChirpTimer`: int (s32)
- `mFront`: vec3
- `mGlideTimer`: int (s32)
- `mHoverTimer`: int (s32)
- `mIsReverse`: bool
- `mLiftTimer`: int (s32)
- `mPointIndex`: int (s32)
- `mSeaGullGroup`: SeaGullGroup (an object, read-only)
- `mSide`: vec3
- `mTargetUpdateTimer`: int (s32)
- `mUp`: vec3

## SeaGullGroup

- `mPointCount`: int (s32)

## SearchBeamer

- `mBeamAimSpeed`: float
- `mBeamEnd`: vec3
- `mBeamLength`: float
- `mBeamStart`: vec3
- `mCollisionMtx.mMtx`: float (array)
- `mFront`: vec3
- `mPropellerAngle`: float
- `mPropellerSpeed`: float
- `mTerritoryRadius`: float
- `mUp`: vec3

## SeesawMoveNut

- `mBackHomeStep`: int (s32)
- `mPrevAngularSpeed`: float
- `mSpeed`: float

## ShadowClipActor

- `mHost`: LiveActor (an object, read-only)

## ShadowControllerHolder

- `mControllers.mCount`: int (s32)
- `mRequested.mCount`: int (s32)

## ShadowSurfaceBox

- `mSize`: vec3

## ShadowSurfaceCircle

- `mRadius`: float

## ShadowSurfaceOval

- `mSize`: vec3

## ShadowVolumeBox

- `mPoints`: vec3 (array)
- `mSize`: vec3
- `mVariant`: int (u32)

## ShadowVolumeCylinder

- `mRadius`: float

## ShadowVolumeFlatModel

- `mDropJoint`: int (u32)
- `mRootJoint`: int (u32)

## ShadowVolumeLine

- `mFromWidth`: float
- `mHasSides`: bool
- `mLastFromSide`: vec3
- `mLastToSide`: vec3
- `mSideFlips`: int (u32)
- `mToWidth`: float

## ShadowVolumeOval

- `mSize`: vec3

## ShadowVolumeOvalPole

- `mSize`: vec3

## ShadowVolumeSphere

- `mRadius`: float

## Shellfish

- `mIsItemBound`: bool
- `mItem`: LiveActor (an object, read-only)
- `mItemType`: int (s32)

## ShootingStar

- `mBasePos`: vec3
- `mFallAxis`: vec3
- `mFallHeight`: float
- `mGravityPreShoot`: vec3
- `mNumBurstStarPiece`: int (s32)
- `mWaitForNextShootTime`: int (s32)

## SideSpikeMoveStep

- `mRailClipping`: vec3

## SimpleBloomArea

- `mIntensity`: int (u8)
- `mMaskFilterColor`: int (u32)
- `mThreshold`: int (u8)

## SimpleBreakableObj

- `mBreakEnded`: bool
- `mBreakFlags`: int (u16)
- `mBreakOffsetFrame`: int (s32)
- `mItemNum`: int (s32)
- `mItemType`: int (s32)
- `mKinokoOneUp`: BenefitItemObj (an object, read-only)
- `mPlayRiddleSound`: bool
- `mSwitchDeadDelay`: int (s32)

## SimpleFloaterObj

- `mEffectPos`: vec3
- `mFloatingForce`: MapPartsFloatingForce (an object, read-only)
- `mObjName`: text (read-only)

## SimpleTimerObj

- `mFlashingCtrl`: FlashingCtrl (an object, read-only)
- `mTimeLeft`: int (s32)
- `mTimeLimit`: int (s32)

## SkeletalFishBaby

- `mSpeed`: float
- `mSpeedMax`: float
- `mStarPieceTargets`: LiveActor (an object, read-only) (array)

## SkeletalFishBoss

- `mBgmHoldStep`: int (s32)
- `mBgmType`: int (s32)
- `mBossDirector`: SkeletalFishBossBattleDirector (an object, read-only)
- `mBossHead`: SkeletalFishBossHead (an object, read-only)
- `mBossInfo`: SkeletalFishBossInfo (an object, read-only)
- `mBreakModel`: ModelObj (an object, read-only)
- `mCameraTargetMtx`: CameraTargetMtx (an object, read-only)
- `mCurScene`: text (read-only)
- `mDamageBrkIndex`: int (u32)
- `mGuardHolder`: SkeletalFishGuardHolder (an object, read-only)
- `mGuardNum`: int (s32)
- `mHitsLeft`: int (s32)
- `mLevel`: int (s32)
- `mLevelNum`: int (s32)
- `mPlanetCenter`: vec3
- `mScarFlash`: SkeletalFishBossScarFlash (an object, read-only)

## SkeletalFishBossBattleDirector

- `mClipActorNum`: int (s32)
- `mClipActors.mArr`: LiveActor (an object, read-only) (array)
- `mClipActors.mSize`: int (s32)
- `mColumnNum`: int (s32)
- `mColumns.mArr`: SubmarineVolcanoBigColumn (an object, read-only) (array)
- `mColumns.mSize`: int (s32)
- `mFishBoss`: SkeletalFishBoss (an object, read-only)
- `mGuardModels`: ModelObj (an object, read-only) (array)

## SkeletalFishBossHead

- `mBloomModels`: ModelObj (an object, read-only) (array)
- `mHeadCollisionMtx.mMtx`: float (array)
- `mIsEyeLightOn`: bool
- `mJawCollisionMtx.mMtx`: float (array)
- `mLightModels`: ModelObj (an object, read-only) (array)

## SkeletalFishBossInfo

- `mFishBoss`: SkeletalFishBoss (an object, read-only)
- `mGuardNum`: int (s32)
- `mLevelNum`: int (s32)

## SkeletalFishBossRail

- `mRailId`: int (s32)

## SkeletalFishBossRailHolder

- `mRails.mCount`: int (s32)

## SkeletalFishGuard

- `mApartFallSpeed`: float
- `mApartVelocity`: vec3
- `mAppearRandomDir`: vec3
- `mAppearStartPos`: vec3
- `mAppearWaitStep`: int (u32)
- `mAttackDelay`: int (s32)
- `mFishBoss`: SkeletalFishBoss (an object, read-only)
- `mFront`: vec3
- `mGuardOffset`: vec3
- `mSpeed`: float
- `mTargetPos`: vec3
- `mTargetRailDir`: vec3
- `mTargetRailPos`: vec3

## SkeletalFishGuardHolder

- `mAppearRailCoord`: float
- `mBossRail`: SkeletalFishBossRail (an object, read-only)
- `mFishBoss`: SkeletalFishBoss (an object, read-only)
- `mIsValid`: int (u8)
- `mNumGuards`: int (s32)

## SleepControllerHolder

- `mSleepControl.mCount`: int (s32)

## SlingShooter

- `mAttackVelocity`: vec3
- `mCameraTargetMtx`: CameraTargetMtx (an object, read-only)
- `mIsKinopioAttached`: bool
- `mIsKinopioCameraFocused`: bool
- `mIsKinopioTalkEnabled`: bool
- `mIsLastDamage`: bool
- `mPadChannel`: int (s32)
- `mPassThroughEnable`: bool
- `mPlayerZ`: float
- `mPointerPos`: vec3
- `mRider`: LiveActor (an object, read-only)

## SmallStone

- `mIsStarPiece`: bool
- `mMemberCount`: int (s32)
- `mPlacementOffset`: float
- `mStoneType`: int (s32)
- `mUseGravity`: bool

## SmallStoneMember

- `mAnimRate`: float
- `mAnimStopped`: bool
- `mBreakEffectName`: text (read-only)
- `mBreakTimer`: int (s32)
- `mShakeRequested`: int (u8)
- `mUseGravity`: bool

## Snakehead

- `mActivationRadius`: float
- `mBackwardSpeed`: float
- `mForwardSpeed`: float
- `mItem`: BenefitItemOneUp (an object, read-only)
- `mRailCenter`: vec3
- `mSmokePos`: vec3

## SnowFloor

- `mAxis`: vec3
- `mCheckCount`: int (u16)
- `mCheckList.mArr`: HitSensor (an object, read-only) (array)
- `mCheckList.mSize`: int (s32)
- `mDirtyFrames`: int (s32)
- `mHalfAngle`: float
- `mHalfLength`: float
- `mLastPlayerPoint.x`: float
- `mLastPlayerPoint.y`: float
- `mLastPointerPoint.x`: float
- `mLastPointerPoint.y`: float
- `mRadius`: float
- `mReduceMapSize`: int (u16)
- `mResistFrames`: int (s32)
- `mTextureIndex`: int (u8)
- `mUp`: vec3
- `mWalkingResist`: float

## SnowFloorTile

- `mCellSize`: float
- `mDirtyFrames`: int (s32)
- `mForward`: vec3
- `mLastPlayerPoint.x`: float
- `mLastPlayerPoint.y`: float
- `mLastPointerPoint.x`: float
- `mLastPointerPoint.y`: float
- `mReduceMapSize`: int (u16)
- `mRight`: vec3
- `mTextureIndex`: int (u8)
- `mTextureShift`: int (s32)
- `mTextureSize`: int (s32)
- `mUp`: vec3

## SnowplowSwitch

- `mIsDiscovered`: bool

## SoundEmitter

- `mSoundType`: int (s32)

## SoundEmitterCube

- `mSoundName`: text (read-only)
- `mSoundPos`: vec3

## SoundEmitterSphere

- `mSoundIndex`: int (s32)
- `mSoundName`: text (read-only)
- `mSoundPos`: vec3

## SpaceCocoon

- `mAttackTime`: int (s32)
- `mBasePos`: vec3
- `mCameraTargetMtx`: CameraTargetMtx (an object, read-only)
- `mCocoonModel`: PartsModel (an object, read-only)
- `mCocoonPos`: vec3
- `mFront`: vec3
- `mHeight`: float
- `mIsKinopioCameraFocused`: bool
- `mNeutralPos`: vec3
- `mNumPoints`: int (s32)
- `mPadChannel`: int (s32)
- `mPointerPos`: vec3
- `mRider`: LiveActor (an object, read-only)
- `mSide`: vec3
- `mSpringVel`: vec3
- `mTouchTime`: int (s32)
- `mUp`: vec3

## SpaceMine

- `mClippingRange`: vec3
- `mIsPlayerNear`: int (u8)
- `mRotateSpeed`: float
- `mShadowType`: int (s32)

## SpaceShipStep

- `mHasBeenSeen`: bool
- `mHiddenStep`: int (s32)
- `mRailClippingCenter`: vec3

## SphereRailDash

- `mBinderSensor`: HitSensor (an object, read-only)
- `mCoordCubic`: float
- `mCoordLinear`: float
- `mCoordQuadratic`: float
- `mEndSpeed`: float
- `mMoveStep`: int (s32)
- `mOffset`: vec3
- `mOffsetVelocity`: vec3

## SphereSelector

- `mClickPos.x`: float
- `mClickPos.y`: float
- `mClickStep`: int (s32)
- `mHandle`: SphereSelectorHandle (an object, read-only)
- `mIsPointingInvalid`: bool
- `mIsSePlayed`: bool
- `mNearestDist`: int (u32)
- `mNearestTarget`: LiveActor (an object, read-only)
- `mPointingTarget`: LiveActor (an object, read-only)
- `mSelectedTarget`: LiveActor (an object, read-only)
- `mSphereGroup`: LiveActorGroup (an object, read-only)

## SphereSelectorHandle

- `mFront`: vec3
- `mIsFileSelectMode`: bool
- `mIsRotateBgm`: bool
- `mNotPointingStep`: int (s32)
- `mPrevRotateSpeedX`: float
- `mRotateSpeed`: float
- `mRotateSpeedX`: float
- `mSide`: vec3
- `mTargetRotateSpeed`: float
- `mUp`: vec3

## SpiderCoin

- `mCocoonModel`: PartsModel (an object, read-only)
- `mPad0Velocity.x`: float
- `mPad0Velocity.y`: float
- `mPad1Velocity.x`: float
- `mPad1Velocity.y`: float
- `mThreadZ`: float

## SpiderMapBlock

- `mPosZ`: float

## SpiderThread

- `mCirclePartsBufferSize`: int (s32)
- `mCutPointsBufferSize`: int (s32)
- `mFront`: vec3
- `mIsBloomOn`: bool
- `mNumCircleParts`: int (s32)
- `mNumCutPoints`: int (s32)
- `mNumHangInfos`: int (s32)
- `mNumRadialLines`: int (s32)
- `mNumRadialParts`: int (s32)
- `mRadialPartsBufferSize`: int (s32)

## SpinDriver

- `mBindActor`: LiveActor (an object, read-only)
- `mBindPos`: vec3
- `mBindVelocity`: vec3
- `mCanBind`: bool
- `mCaptureRate`: float
- `mCaptureStartPos`: vec3
- `mClippingCenter`: vec3
- `mClippingRadius`: float
- `mCoolDown`: int (s32)
- `mFallStep`: int (s32)
- `mFlyEndStep`: int (s32)
- `mFlyShortStep`: int (s32)
- `mFront`: vec3
- `mInitPoseAngle`: float
- `mIsAppearSilent`: bool
- `mIsCoolingDown`: bool
- `mIsPlayerAway`: bool
- `mJumpSpeed`: float
- `mShadowDropLength`: float
- `mShootTime`: int (s32)
- `mSide`: vec3
- `mSpaceFlyEndFrame`: int (s32)
- `mSpinAngle`: float
- `mSpinSpeed`: float
- `mUp`: vec3

## SpinDriverOperateRing

- `mAccelerate`: vec3
- `mCenter`: vec3
- `mDirection`: vec3
- `mOffset`: vec3
- `mOffsetLength`: float
- `mOffsetRate`: float
- `mOperateState`: int (s32)
- `mPointerPos`: vec3
- `mRadiusRate`: float

## SpinDriverPathDrawInit

- `mIsPathAtOpa`: bool

## SpinDriverPathDrawer

- `mColor`: int (s32)
- `mCoord`: float
- `mEndCut`: float
- `mFadeScale`: float
- `mMaskLength`: float
- `mMaxCoord`: float
- `mPositionCount`: int (s32)

## SpinningBox

- `mIsIceBox`: bool
- `mIsOnGround`: bool
- `mRotateSpeed`: float
- `mSlideDir`: vec3
- `mSlideSpeed`: float
- `mSpinAngle`: float

## SpotMarkLight

- `mIsDisappeared`: bool

## SpringJetWater

- `mObjName`: text (read-only)
- `mUp`: vec3

## SpringWaterFloaterSpot

- `mBoundSize`: float
- `mBoundStep`: int (s32)
- `mFloatPos`: vec3
- `mFloatTimer`: int (s32)
- `mHasSurfaced`: bool
- `mHeight`: float
- `mInitialRotation`: vec3
- `mIsOnPlayer`: bool
- `mNeedsBound`: bool
- `mRiseAccel`: float
- `mRiseSpeed`: float
- `mWaterCheckOffset`: float

## StaffRoll

- `mBgmFrame`: int (s32)
- `mEndLogoPos.x`: float
- `mEndLogoPos.y`: float
- `mInfo`: InformationMessage (an object, read-only)
- `mLineStep`: int (s32)
- `mMsgLine`: int (u32)
- `mMsgLineNum`: int (u32)
- `mPicture`: StaffRollPicture (an object, read-only)
- `mTitleLogoPos.x`: float
- `mTitleLogoPos.y`: float

## StaffRollLine

- `mPosition.x`: float
- `mPosition.y`: float

## StageDataHolder

- `mChildObjs.mMaxSize`: int (s32)
- `mCommonPlacements.mMaxSize`: int (s32)
- `mGeneralPosObjs.mMaxSize`: int (s32)
- `mListObjs.mMaxSize`: int (s32)
- `mLoadCommon`: int (u8)
- `mObjNameIndex.mCount`: int (u32)
- `mObjNameIndex.mMask`: int (u32)
- `mPathObjs.mMaxSize`: int (s32)
- `mPlacementObjs.mMaxSize`: int (s32)
- `mPlacementStage`: int (unsigned int)
- `mScenarioPlacements.mMaxSize`: int (s32)
- `mStageDataArray.mArr`: StageDataHolder (an object, read-only) (array)
- `mStageDataArray.mSize`: int (s32)
- `mStageDataHolderCount`: int (s32)
- `mStageName`: text (read-only)
- `mStartObjs.mMaxSize`: int (s32)
- `mZoneID`: int (s32)

## StageResultInformer

- `mClearedStarPieceNum`: int (s32)
- `mHasBufferedDecide`: bool
- `mIconAButton`: IconAButton (an object, read-only)
- `mIsNewBestCoin`: bool

## StageSwitchContainer

- `mSwitches.mCount`: int (s32)

## StarCounter

- `mPowerStarNum`: int (s32)

## StarPiece

- `mChaseRate`: float
- `mFallKillTimer`: int (s32)
- `mFront`: vec3
- `mGettableDelayCounter`: int (s32)
- `mGettableEffectStep`: int (s32)
- `mNumGift`: int (s32)
- `mTargetSensor`: HitSensor (an object, read-only)
- `mThrowEnd`: vec3
- `mThrowStart`: vec3
- `mTurnRate`: float

## StarPieceCounter

- `mFollowPos.x`: float
- `mFollowPos.y`: float
- `mForcedAppear`: int (s32)
- `mInvalidCountUpFrame`: int (s32)
- `mMode`: int (int)
- `mStarPieceDisplayNum`: int (s32)
- `mStarPieceNum`: int (s32)

## StarPieceDirector

- `mColorIndex`: int (s32)
- `mGetSounds.mCount`: int (u32)
- `mGetSounds.mHead`: int (u32)
- `mHostInfos.mCount`: int (s32)
- `mNumStarPiecesLaunched`: int (s32)
- `mQueueNewGetSound`: bool
- `mReceiverInfos.mCount`: int (s32)
- `mResetChasingStarPiece`: bool
- `mSoundStep`: int (u32)
- `mStarPieceShooters`: StarPieceShooter (an object, read-only) (array)

## StarPieceFollowGroup

- `mAngle`: float
- `mLocalXAxis`: vec3
- `mLocalYAxis`: vec3
- `mLocalZAxis`: vec3
- `mNumPiecesTotal`: int (s32)
- `mNumPiecesUse`: int (s32)
- `mRadius`: float
- `mTimeLimit`: int (s32)

## StarPieceGroup

- `mCircleRadius`: float
- `mHasAppeared`: bool
- `mIsAppearEffect`: bool
- `mIsRail`: bool
- `mLifeStep`: int (s32)
- `mNumPieces`: int (s32)
- `mPlaceAtPathPoints`: bool
- `mRailClippingCenter`: vec3
- `mRailSpeed`: float

## StarPieceMother

- `mNextPiece`: int (u32)
- `mNumPieces`: int (s32)
- `mRailSpeed`: float

## StarPieceShooter

- `mChargeStep`: int (s32)
- `mEffectPos`: vec3
- `mEffectRot`: vec3
- `mLockOnSensor`: HitSensor (an object, read-only)
- `mPort`: int (s32)
- `mSearchDist`: float
- `mSearchSensor`: HitSensor (an object, read-only)
- `mShootSpeed`: float

## StarPointerBlur

- `mNumHiddenPoints`: int (s32)
- `mPadChannel`: int (s32)

## StarPointerCommandStream

- `mFollowPos.x`: float
- `mFollowPos.y`: float
- `mOffScreenTime`: int (s32)
- `mPadChannel`: int (s32)

## StarPointerGuidance

- `mIsActive`: bool
- `mIsActiveRequest1P`: bool
- `mIsActiveRequest2P`: bool
- `mIsGuidanceDisabled`: bool
- `mIsTimeOutEnabled`: bool
- `mOutScreenTime`: int (s32)
- `mRequestTime1P`: int (s32)
- `mRequestTime2P`: int (s32)
- `mTextLineNum1P`: int (u32)
- `mTextLineNum2P`: int (u32)
- `mTextLineNumGuidance`: int (u32)

## StarPointerLayout

- `mActor`: LiveActor (an object, read-only)
- `mAppearTime`: int (s32)
- `mBlur`: StarPointerBlur (an object, read-only)
- `mCommandStream`: StarPointerCommandStream (an object, read-only)
- `mIsAppear`: bool
- `mIsNewTouch`: bool
- `mIsPointerValid`: bool
- `mNumber`: StarPointerNumber (an object, read-only)
- `mPadChannel`: int (s32)
- `mPosition.x`: float
- `mPosition.y`: float
- `mRadius`: float
- `mShootDisabled`: bool
- `mSingleTouch`: bool
- `mStartDisableShoot`: bool
- `mStartSingleTouch`: bool
- `mStartTouch`: bool
- `mTouchTimer`: int (s32)

## StarPointerNumber

- `mAppearTime`: int (s32)
- `mIsAppear`: bool
- `mPadChannel`: int (s32)
- `mParent`: StarPointerLayout (an object, read-only)

## StarReturnDemoStarter

- `mFrame`: int (s32)
- `mHair`: LiveActor (an object, read-only)
- `mLuma`: LiveActor (an object, read-only)
- `mPosition`: vec3
- `mPowerStar`: LiveActor (an object, read-only)
- `mStageResultInformer`: StageResultInformer (an object, read-only)

## StinkBugBase

- `mFront`: vec3
- `mInitFront`: vec3
- `mInitPos`: vec3
- `mIsTurnLeft`: bool
- `mRadius`: float
- `mTerritoryAngle`: float

## StinkBugParent

- `mIsChildAlive`: bool

## StinkBugSmall

- `mIsBodyAttack`: bool

## StopSceneController

- `mDelayRequestMax`: int (s32)
- `mDelayRequestNum`: int (s32)
- `mFrame`: int (s32)

## StrayTico

- `mDemoAngle`: float
- `mDemoCenter`: vec3
- `mDemoFront`: vec3
- `mDemoStartPos`: vec3
- `mDemoUp`: vec3
- `mFrontVec`: vec3
- `mHost`: CollectTico (an object, read-only)
- `mItemBubbleModel`: PartsModel (an object, read-only)
- `mWarpPos`: vec3

## StringSpider

- `mIsPointerPulled`: bool
- `mPointerStep`: int (s32)
- `mString`: StringSpiderString (an object, read-only)
- `mStringLength`: float
- `mStringLengthMax`: float

## StringSpiderString

- `mRootPos`: vec3

## SubMeterLayout

- `mFullLifeFrame`: int (u16)
- `mLifeFrame`: int (u16)
- `mPhase`: int (u8)

## SubmarineSteam

- `mUp`: vec3

## SubmarineVolcanoBigColumn

- `mBreakModel`: ModelObj (an object, read-only)
- `mIsSmallColumn`: bool

## SuddenDeathMeter

- `mAlpha`: float
- `mCount`: int (s32)
- `mFollowPosW.x`: float
- `mFollowPosW.y`: float
- `mMeterMoveStartPos.x`: float
- `mMeterMoveStartPos.y`: float
- `mShowHideRate`: float

## SunakazeKunTrap

- `mHost`: LiveActor (an object, read-only)
- `mLocalPos`: vec3

## SuperSpinDriver

- `mAlreadyDoneFlag`: int (u32)
- `mAlreadyDoneFlagIdx`: int (s32)
- `mBindActor`: LiveActor (an object, read-only)
- `mBindPos`: vec3
- `mBindVelocity`: vec3
- `mCaptureStartPos`: vec3
- `mClippingCenter`: vec3
- `mClippingRadius`: float
- `mColor`: int (s32)
- `mCoolDown`: int (s32)
- `mDrawPathRangeIdx`: int (s32)
- `mEmptyModel`: ModelObj (an object, read-only)
- `mFallStep`: int (s32)
- `mFlightTime`: int (s32)
- `mFlyEndStep`: int (s32)
- `mFlyLoopStep`: int (s32)
- `mFront`: vec3
- `mFrontAngle`: float
- `mIsCoolingDown`: bool
- `mIsDisableJingle`: bool
- `mIsPlayerAway`: bool
- `mIsPullPlayer`: bool
- `mOperateRing`: SpinDriverOperateRing (an object, read-only)
- `mPathDrawer`: SpinDriverPathDrawer (an object, read-only)
- `mPlayerLandRotation`: float
- `mRollAngle`: float
- `mShadowLength`: float
- `mShootPathDirection`: vec3
- `mShootPathPosition`: vec3
- `mSide`: vec3
- `mSpaceFlyEndFrame`: int (s32)
- `mSpaceFlyStartFrame`: int (s32)
- `mSpinSpeed`: float
- `mSwingAngle`: float
- `mSwingAngleMax`: float
- `mUp`: vec3

## SurfRay

- `mAirTime`: int (s32)
- `mBaseSide`: vec3
- `mBaseUp`: vec3
- `mFront`: vec3
- `mGroundNormal`: vec3
- `mInTutorial`: bool
- `mInTutorialArea`: bool
- `mInWater`: bool
- `mLectureIdx`: int (s32)
- `mOrthoVelocity`: vec3
- `mPadChannel`: int (s32)
- `mRayTilt`: float
- `mRider`: LiveActor (an object, read-only)
- `mShadowAlpha`: float
- `mSide`: vec3
- `mSteerAccel`: float
- `mSteerRate`: float
- `mSurfSpeed`: float
- `mTwistBufferSize`: int (s32)
- `mUp`: vec3
- `mWarpPos`: vec3
- `mWaterInfo.mCamWaterDepth`: float
- `mWaterInfo.mDepth`: float
- `mWaterInfo.mEdgeDistance`: float
- `mWaterInfo.mEdgePos`: vec3
- `mWaterInfo.mOceanBowl`: OceanBowl (an object, read-only)
- `mWaterInfo.mOceanRing`: OceanRing (an object, read-only)
- `mWaterInfo.mOceanSphere`: OceanSphere (an object, read-only)
- `mWaterInfo.mStreamVec`: vec3
- `mWaterInfo.mSurfaceNormal`: vec3
- `mWaterInfo.mSurfacePos`: vec3
- `mWaterInfo.mWaterArea`: WaterArea (an object, read-only)
- `mWaterInfo.mWaveHeight`: float
- `mWaterNormal`: vec3
- `mWaterShadowPos`: vec3

## SurprisedGalaxy

- `mGalaxyName`: text (read-only)

## SwingLight

- `mLightCylinder`: LightCylinder (an object, read-only)

## SwingRope

- `mAccelTime`: int (s32)
- `mBasePos`: vec3
- `mFootPos`: vec3
- `mGrabCoord`: float
- `mGrabPointNum`: float
- `mIsStretched`: bool
- `mNumPoints`: int (s32)
- `mPrevSoundLvl`: int (s32)
- `mRider`: LiveActor (an object, read-only)
- `mRopeLength`: float
- `mShadowDropPos`: vec3
- `mSlideSpeed`: float
- `mStretchTime`: int (s32)

## SwingRopeGroup

- `mMaterial.mRowCount`: int (u32)
- `mMaterial.mTextureCount`: int (u32)

## SwitchBox

- `mFloorTouchTimer`: int (s32)
- `mHitCoolDown`: int (s32)
- `mHitPoint`: int (u16)

## SwitchSynchronizer

- `mIsCheckSameSwitchState`: bool

## SwitchWatcher

- `mFlags`: int (u32)

## SwitchWatcherHolder

- `mSwitchWatcher.mCount`: int (s32)

## Syati

- `mBlinkTimer`: int (s32)
- `mClippingRange`: float
- `mClippingVec`: vec3
- `mCurrentRailPointNo`: int (s32)
- `mHideOnShoreMode`: int (s32)
- `mMarioMoveLocalOffsetVec`: vec3
- `mNumRings`: int (s32)
- `mPrizeRingCount`: int (s32)
- `mPrizeRingGroup`: LiveActorGroup (an object, read-only)
- `mPrizeRingLife`: int (s32)
- `mRaceManagerLayout`: RaceManagerLayout (an object, read-only)
- `mRailDirBlend`: vec3
- `mStarSpawnType`: int (s32)
- `mSwimMode`: int (s32)
- `mTalkMessageCtrl`: TalkMessageCtrl (an object, read-only)

## SysInfoWindow

- `mDim`: float
- `mDimEpoch`: int (u32)
- `mDimFloor`: float
- `mDimHandOver`: int (u8)
- `mDimHanding`: bool
- `mDimLinger`: int (s32)
- `mExecutesIconAButton`: bool
- `mIconAButton`: IconAButton (an object, read-only)
- `mIsBackAnswersNo`: bool
- `mTextParentPaneName`: text (read-only)
- `mWindowParentPaneName`: text (read-only)

## SystemWipeHolder

- `mIsCapturing`: bool

## TakoHei

- `mDeathItem`: int (u32)
- `mFront`: vec3
- `mHasMoveLimit`: int (u8)
- `mInitPos`: vec3
- `mNoPushStep`: int (s32)
- `mWalkTarget`: vec3

## Takobo

- `mBox`: SpinningBox (an object, read-only)
- `mFront`: vec3
- `mInitPos`: vec3
- `mIsCheckFireGround`: bool
- `mIsMoveBack`: bool
- `mMoveDir`: vec3
- `mMoveEnd`: float
- `mMoveRange`: float
- `mMoveSpeed`: float
- `mMoveStart`: float
- `mMoveStep`: int (s32)

## TalkBalloon

- `mHasBeak`: bool
- `mIsBeakMoving`: bool
- `mMessageCtrl`: TalkMessageCtrl (an object, read-only)

## TalkBalloonEvent

- `mAButton`: IconAButton (an object, read-only)
- `mFormMode`: int (u32)
- `mHome.x`: float
- `mHome.y`: float
- `mPlaced`: bool

## TalkBalloonShort

- `mInterval`: int (u32)

## TalkDirector

- `mBalloonCtrl`: TalkMessageCtrl (an object, read-only)
- `mBranchResult`: bool (array)
- `mDemoType`: int (s32)
- `mHostActor`: LiveActor (an object, read-only)
- `mIsInvalidClipping`: bool
- `mIsInvalidTalk`: bool
- `mIsNext`: bool
- `mIsPlayerTalk`: bool
- `mIsSkipTerm`: bool
- `mIsTalkEnd`: bool
- `mIsTalkStart`: bool
- `mLastTalkCtrl`: TalkMessageCtrl (an object, read-only)
- `mMessageInfo.mAlreadyReadId`: int (s8)
- `mMessageInfo.mAreaId`: int (s8)
- `mMessageInfo.mBalloonType`: int (u8)
- `mMessageInfo.mCameraSetID`: int (u16)
- `mMessageInfo.mCameraType`: int (u8)
- `mMessageInfo.mSoundId`: int (s8)
- `mMessageInfo.mTalkType`: int (u8)
- `mMsgControls.mCount`: int (s32)
- `mMsgCtrl`: TalkMessageCtrl (an object, read-only)
- `mRequestCtrl`: TalkMessageCtrl (an object, read-only)

## TalkMessageCtrl

- `mAlreadyDoneFlag`: int (u32)
- `mAlreadyDoneFlags`: int (u32)
- `mBalloonPos`: vec3
- `mHostActor`: LiveActor (an object, read-only)
- `mIsOnReadNodeAuto`: bool
- `mIsOnRootNodeAuto`: bool
- `mIsStartOnlyFront`: bool
- `mMsgBalloonFollowOffs`: vec3
- `mTalkDistance`: float
- `mTalkStateKind`: int (u32)
- `mZoneID`: int (s32)

## Tamakoro

- `mAccelDir`: vec3
- `mAccelRate`: float
- `mAirTime`: int (s32)
- `mControlDisabled`: bool
- `mDirectionToMario`: vec3
- `mHasTutorial`: bool
- `mKickVel`: vec3
- `mMarioBindRequestPos`: vec3
- `mMarioFront`: vec3
- `mMarioOffset`: float
- `mMarioOffsetVelocity`: float
- `mMarioPos`: vec3
- `mMarioRotateYAngle`: float
- `mMarioUp`: vec3
- `mMoment`: vec3
- `mRingUp`: vec3
- `mTutorial`: TamakoroTutorial (an object, read-only)

## TamakoroTutorial

- `mCanTalk`: bool
- `mHost`: Tamakoro (an object, read-only)
- `mIsTrampled`: bool
- `mPadAccel`: vec3
- `mTalkCtrl`: TalkMessageCtrl (an object, read-only)
- `mTalkCtrlAutomatic`: TalkMessageCtrl (an object, read-only)

## Teresa

- `mAnimScale`: vec3
- `mAppearanceType`: int (s32)
- `mDisplayModel`: PartsModel (an object, read-only)
- `mHasCoin`: bool
- `mInitPos`: vec3
- `mIsChief`: bool
- `mKeySwitch`: KeySwitch (an object, read-only)
- `mTransparency`: float
- `mWalkRange`: float
- `mWalkTarget`: vec3
- `mWallHitPos`: vec3
- `mWallNormal`: vec3

## TeresaRacer

- `mBgmState`: int (s32)
- `mIsGoal`: bool
- `mRacerId`: int (s32)

## TeresaWater

- `mFarPos`: vec3
- `mFront`: vec3
- `mInitPos`: vec3

## TicoEat

- `mStarPieceLeft`: int (s32)
- `mStarPieceNeed`: int (s32)

## TicoDomeLecture

- `mInitPos`: vec3
- `mInitRotation`: vec3

## TicoFat

- `mAbsorbSoundStep`: int (s32)
- `mCurrentFed`: int (s32)
- `mEatInterval`: int (u32)
- `mEatSoundStep`: int (s32)
- `mFlyCoord`: float
- `mFlySpeed`: float
- `mGuidanceId`: int (s32)
- `mHungryTalkCtrl`: TalkMessageCtrl (an object, read-only)
- `mIsCameraOn`: bool
- `mItem`: BenefitItemObj (an object, read-only)
- `mItemType`: int (s32)
- `mMeter`: FullnessMeter (an object, read-only)
- `mNearStep`: int (s32)
- `mPointingStep`: int (s32)
- `mStarPieceLeft`: int (s32)
- `mStarPieceNeed`: int (s32)
- `mStartEat`: bool
- `mTicoSeedId`: int (s32)

## TicoGalaxy

- `mGalaxyName`: text (read-only)
- `mStarRing`: TicoStarRing (an object, read-only)

## TicoRail

- `mFront`: vec3
- `mTalkPartner`: LiveActor (an object, read-only)

## TicoShop

- `mChoseLifeUp`: bool
- `mChoseOneUp`: bool
- `mLifeUp`: BenefitItemLifeUp (an object, read-only)
- `mOneUp`: BenefitItemOneUp (an object, read-only)

## TicoStarRing

- `mAppearActor`: LiveActor (an object, read-only)
- `mGalaxyName`: text (read-only)
- `mTicoGalaxy`: LiveActor (an object, read-only)

## TimeAppearObj

- `mTimer`: int (s32)

## TimeLimitLayout

- `mIsNormalDisplay`: bool
- `mIsSuspend`: bool
- `mTime`: int (u32)
- `mTimeLimit`: int (u32)

## TimerMoveWall

- `mMoveLength`: float
- `mTimer`: int (s32)

## TimerSwitch

- `mTimeLeft`: int (s32)
- `mTimeLimit`: int (s32)

## TombSpider

- `mNoctilucaActive`: bool

## TombSpiderThreadAttacher

- `mRadius`: float
- `mZOffset`: float

## TornadoMario

- `mFront`: vec3
- `mIsVisible`: bool
- `mUp`: vec3

## TrampleStar

- `mCaptureFrames`: int (s32)
- `mCaptureVtx`: int (u16)
- `mDeformRate`: float
- `mIsLod`: bool
- `mJumpVel`: float
- `mNumDrawVtxs`: int (u16)
- `mNumFlags`: int (u16)
- `mNumSurfaces`: int (u16)
- `mNumVtxs`: int (u16)
- `mSpringRate`: float

## TrampleStarShare

- `mTrampleStar`: TrampleStar (an object, read-only)

## TransparentWall

- `mIsSolid`: bool
- `mSolidForms`: int (s32)

## Trapeze

- `mFront`: vec3
- `mGrabCoord`: float
- `mIsSwingBack`: bool
- `mIsSwingFront`: bool
- `mPrevSoundLvl`: int (s32)
- `mRider`: LiveActor (an object, read-only)
- `mRopeLength`: float
- `mSide`: vec3
- `mSwingReverse`: bool
- `mSwingVel`: float
- `mTrapezeModel`: PartsModel (an object, read-only)
- `mUp`: vec3
- `mWasSwingBack`: bool
- `mWasSwingFront`: bool

## TrapezeRopeDrawInit

- `mMaterial.mRowCount`: int (u32)
- `mMaterial.mTextureCount`: int (u32)

## TreasureBoxCracked

- `mBoxType`: int (s32)
- `mIsItemMoving`: bool
- `mItem`: LiveActor (an object, read-only)
- `mItemTargetPos`: vec3
- `mItemType`: int (s32)
- `mItemVelocity`: vec3
- `mOpenCondition`: int (s32)
- `mPowerStarId`: int (s32)
- `mWaitForSwitchOn`: int (s32)

## TreasureSpot

- `mIsCoinFlower`: bool
- `mIsGlowNear`: bool

## TrickRabbit

- `mFootPrint`: FootPrint (an object, read-only)
- `mFront`: vec3
- `mIsGhost`: bool
- `mIsRouteLevelControl`: bool
- `mNextNodeDistance`: float
- `mPhaseSteps`: int (s32)
- `mRouteLevel`: int (s32)
- `mRouteLevelStep`: int (s32)
- `mRouteLevelTarget`: int (s32)
- `mSpotMarkLight`: SpotMarkLight (an object, read-only)
- `mTalkMessageCtrl`: TalkMessageCtrl (an object, read-only)

## TrickRabbitFreeRun

- `mFootPrint`: FootPrint (an object, read-only)
- `mFrontVec`: vec3
- `mMsgCtrl`: TalkMessageCtrl (an object, read-only)
- `mSpotMarkLight`: SpotMarkLight (an object, read-only)

## TrickRabbitSnow

- `mFinishPos`: vec3
- `mFootPrint`: FootPrint (an object, read-only)
- `mFrontVec`: vec3
- `mIsValidAppearStarPiece`: bool
- `mMsgCtrl`: TalkMessageCtrl (an object, read-only)
- `mNotCaughtableTimer`: int (s32)
- `mSpotMarkLight`: SpotMarkLight (an object, read-only)

## TrickRabbitSnowCollect

- `mIsDemo`: bool
- `mMsgCtrl`: TalkMessageCtrl (an object, read-only)
- `mPowerStarDemoModel`: ModelObj (an object, read-only)
- `mRabbitNum`: int (s32)
- `mTimeLimit`: int (s32)
- `mTimerLayout`: BombTimerLayout (an object, read-only)

## TripodBoss

- `mBgmState`: int (s32)
- `mBodyHeight`: float
- `mBodyPos`: vec3
- `mBodyVelocity`: vec3
- `mClipCenter`: vec3
- `mClipRadius`: float
- `mCurrentStepSeq`: int (s32)
- `mDemoBlendRate`: float
- `mDummyModel`: DummyDisplayModel (an object, read-only)
- `mDummyModelOffset`: vec3
- `mDummyModelSpinSpeed`: float
- `mIsBgmChangePending`: bool
- `mIsPartsHidden`: bool
- `mLegHeightRate`: float
- `mLegLift`: float
- `mLegMiddleLength`: float
- `mLegReach`: float
- `mLegRootLength`: float
- `mLegs`: TripodBossLeg (an object, read-only) (array)
- `mMovableAreaRadius`: float
- `mNextStepSeq`: int (s32)
- `mRideMarioState`: int (s32)
- `mRideMarioTimer`: int (s32)
- `mStepPoints`: TripodBossStepPoint (an object, read-only) (array)
- `mStepSequenceNum`: int (s32)
- `mStepSequences.mMaxSize`: int (s32)
- `mWeightHeightOffset`: float
- `mWeightPos`: vec3

## TripodBossAccesser

- `mBoss`: TripodBoss (an object, read-only)
- `mParts.mArr`: LiveActor (an object, read-only) (array)
- `mParts.mSize`: int (s32)
- `mPartsNum`: int (s32)

## TripodBossBaseJointPosition

- `mID`: int (s32)

## TripodBossBreakMovement

- `mBreakVelocity`: vec3
- `mDamping`: float
- `mFlyDir`: vec3
- `mFlyDirRandomRate`: float
- `mFlySpeed`: float
- `mJointIndex`: int (s32)
- `mMoment`: vec3
- `mPos`: vec3

## TripodBossCoin

- `mCoin`: Coin (an object, read-only)
- `mJointID`: int (s32)

## TripodBossFixPartsBase

- `mBreakMovement`: TripodBossBreakMovement (an object, read-only)
- `mClipCenter`: vec3
- `mClippingRadius`: float
- `mIsActive`: bool
- `mJointID`: int (s32)
- `mLocalPos`: vec3

## TripodBossCore

- `mBloomModel`: ModelObj (an object, read-only)
- `mBreakModel`: ModelObj (an object, read-only)

## TripodBossFixParts

- `mBreakKillStep`: int (s32)
- `mBreakType`: int (s32)
- `mHasBreakMovement`: bool
- `mHasCollision`: bool
- `mIsMarioOn`: bool
- `mIsPlayingWarning`: bool

## TripodBossGuardWall

- `mCameraTargetMtx`: CameraTargetMtx (an object, read-only)
- `mJointID`: int (s32)
- `mStartTrans`: vec3

## TripodBossGuardWallPart

- `mPlacementAngle`: float
- `mStartTiming`: int (s32)

## TripodBossKillerGenerater

- `mActiveLabel`: int (s32)
- `mCanShootSettingPlayerDistance`: float
- `mHasCollision`: bool
- `mIsWaitPlayerAway`: bool
- `mKiller`: HomingKiller (an object, read-only)

## TripodBossKillerGeneraterCircle

- `mAngleNum`: int (s32)
- `mCannonPitch`: float
- `mCirclePitch`: float
- `mGenerater`: TripodBossKillerGenerater (an object, read-only)
- `mHeight`: float
- `mPosition`: vec3
- `mRadius`: float
- `mRotation`: vec3

## TripodBossKinokoOneUp

- `mJointID`: int (s32)
- `mOneUp`: BenefitItemOneUp (an object, read-only)

## TripodBossLeg

- `mBoss`: TripodBoss (an object, read-only)
- `mDemoEffectTiming`: int (s32)
- `mEndTarget`: vec3
- `mEndVelocity`: vec3
- `mForceEndPoint`: vec3
- `mIsAttackHit`: bool
- `mIsPressPlayer`: bool
- `mLandingHeight`: float
- `mPoseMode`: int (s32)
- `mPrevStepPoint`: TripodBossStepPoint (an object, read-only)
- `mRootLocalPos`: vec3
- `mStepPoint`: TripodBossStepPoint (an object, read-only)

## TripodBossRailMoveParts

- `mRailMover`: MapPartsRailMover (an object, read-only)

## TripodBossRotateParts

- `mRotator`: MapPartsRotator (an object, read-only)

## TripodBossShell

- `mBreakModel`: ModelObj (an object, read-only)

## TripodBossStepPoint

- `mLeg`: int (s32)
- `mSequenceIndex`: int (s32)
- `mSequencePriority`: int (s32)
- `mStepFront`: vec3
- `mStepNormal`: vec3
- `mStepPosition`: vec3
- `mWaitTime`: int (s32)

## TripodBossStepStartArea

- `mSequenceNo`: int (s32)

## Tsukidashikun

- `mMoveSpeed`: float
- `mWaitTimer`: int (s32)

## UFOBase

- `mFront`: vec3
- `mIsRailDone`: bool
- `mModel`: ModelObj (an object, read-only)
- `mMoveConditionType`: int (s32)
- `mRailMover`: MapPartsRailMover (an object, read-only)
- `mRotateSpeed`: float

## Unizo

- `mBlinkCount`: int (s32)
- `mBlinkFrame`: int (s32)
- `mBreakModel`: ModelObj (an object, read-only)
- `mFrame`: int (s32)
- `mGravityRate`: float
- `mJumpCount`: int (s32)
- `mRadiusScale`: float
- `mRollHeight`: float
- `mRollSoundTimer`: int (s32)
- `mSpinAnimTimer`: int (s32)
- `mType`: int (s32)

## UnizoLauncher

- `mUnizoNum`: int (s32)

## ValveSwitch

- `mBindActor`: LiveActor (an object, read-only)
- `mIsReverse`: bool

## WarpCube

- `mEventName`: text (read-only)
- `mInvalidateTimer`: int (s32)

## WarpCubeMgr

- `mWarpCube`: WarpCube (an object, read-only)

## WarpPod

- `mArg2`: int (s32)
- `mCameraStep`: int (s32)
- `mCameraTime`: int (s32)
- `mDelay`: int (s32)
- `mDrawsPath`: bool
- `mEventCameraName`: text (read-only)
- `mGlowColorIndex`: int (s32)
- `mGrandstarReq`: int (s32)
- `mGroupId`: int (s32)
- `mIsAppearDemoPending`: bool
- `mIsInactive`: bool
- `mIsPairCameraOn`: bool
- `mNumPathPoints`: int (u32)
- `mPairCameraName`: text (read-only)
- `mPairPod`: WarpPod (an object, read-only)
- `mPathFlagIndex`: int (s32)
- `mPathOpenType`: int (s32)
- `mVisibilityState`: int (s32)

## WarpPodMgr

- `mCameraPod`: LiveActor (an object, read-only)
- `mNextPairIndex`: int (s32)
- `mPods`: LiveActorGroup (an object, read-only)

## WatchTowerRotateStep

- `mRotateVec`: vec3

## WaterAreaHolder

- `mCamInWater`: bool
- `mCameraFilter`: WaterCameraFilter (an object, read-only)
- `mNumOceanBowls`: int (s32)
- `mNumOceanRings`: int (s32)
- `mNumOceanSpheres`: int (s32)
- `mNumWhirlPoolAccelerators`: int (s32)
- `mNumWhirlPools`: int (s32)
- `mOceanBowls.mArr`: OceanBowl (an object, read-only) (array)
- `mOceanBowls.mSize`: int (s32)
- `mOceanRings.mArr`: OceanRing (an object, read-only) (array)
- `mOceanRings.mSize`: int (s32)
- `mOceanSpheres.mArr`: OceanSphere (an object, read-only) (array)
- `mOceanSpheres.mSize`: int (s32)
- `mUseBloom`: bool
- `mWaterInfo.mCamWaterDepth`: float
- `mWaterInfo.mDepth`: float
- `mWaterInfo.mEdgeDistance`: float
- `mWaterInfo.mEdgePos`: vec3
- `mWaterInfo.mOceanBowl`: OceanBowl (an object, read-only)
- `mWaterInfo.mOceanRing`: OceanRing (an object, read-only)
- `mWaterInfo.mOceanSphere`: OceanSphere (an object, read-only)
- `mWaterInfo.mStreamVec`: vec3
- `mWaterInfo.mSurfaceNormal`: vec3
- `mWaterInfo.mSurfacePos`: vec3
- `mWaterInfo.mWaterArea`: WaterArea (an object, read-only)
- `mWaterInfo.mWaveHeight`: float
- `mWhirlPoolAccelerators.mArr`: WhirlPoolAccelerator (an object, read-only) (array)
- `mWhirlPoolAccelerators.mSize`: int (s32)
- `mWhirlPools.mArr`: WhirlPool (an object, read-only) (array)
- `mWhirlPools.mSize`: int (s32)

## WaterBazooka

- `mAlreadyDoneFlag`: int (s32)
- `mBreakModel`: ModelObj (an object, read-only)
- `mCapsule`: WaterBazookaCapsule (an object, read-only)
- `mDemoActor`: CameraTargetDemoActor (an object, read-only)
- `mElectricTime`: int (s32)
- `mHasPowerStar`: bool
- `mIsElectric`: bool
- `mIsPinchBGMStarted`: bool
- `mLife`: int (s32)
- `mShooter`: MogucchiShooter (an object, read-only)
- `mShotNum`: int (s32)

## WaterCameraFilter

- `mWaterAlpha`: float
- `mWavePhase`: float

## WaterLeakPipe

- `mClippingCenter`: vec3
- `mIceStep`: IceStepNoSlip (an object, read-only)
- `mPipeHeight`: float

## WaterPlant

- `mAreaRadius`: float
- `mHalfWidth`: float
- `mHeight`: float
- `mIsSpin`: bool
- `mPlantNum`: int (s32)
- `mType`: int (s32)

## WaterPlantDrawInit

- `mAngleOffset`: float
- `mCameraSide`: vec3

## WaterPressure

- `mIsInvalidSpinKill`: bool

## WaterPressureBullet

- `mFront`: vec3
- `mHost`: LiveActor (an object, read-only)
- `mHostActor`: LiveActor (an object, read-only)
- `mIsGroundSlide`: bool
- `mIsInvalidSpinKill`: bool
- `mIsNoGravity`: bool
- `mSide`: vec3
- `mSpeed`: float

## WaterRoad

- `mAlpha`: int (u8)
- `mBaseUp`: vec3
- `mBoundingBox.f`: vec3
- `mBoundingBox.i`: vec3
- `mCamRailNearestPos`: vec3
- `mClippingCenter`: vec3
- `mRadius`: float
- `mRider`: LiveActor (an object, read-only)
- `mTexUV0.x`: float
- `mTexUV0.y`: float
- `mTexUV1.x`: float
- `mTexUV1.y`: float
- `mTexUV2.x`: float
- `mTexUV2.y`: float

## WhirlPool

- `mAxis`: vec3
- `mClippingCenter`: vec3
- `mHeight`: float
- `mPointCount`: int (s32)
- `mRadius`: float
- `mRotation`: float
- `mTexOffsetU0`: float
- `mTexOffsetU1`: float
- `mTexOffsetU2`: float
- `mTexOffsetV0`: float
- `mTexOffsetV1`: float
- `mTexOffsetV2`: float

## WhirlPoolAccelerator

- `mAxis`: vec3
- `mClippingCenter`: vec3
- `mHeight`: float
- `mPointCount`: int (s32)
- `mRadius`: float
- `mRotation`: float
- `mTexOffsetU0`: float
- `mTexOffsetV0`: float

## WipeFade

- `mFrame`: int (s32)
- `mIsWipeIn`: bool
- `mStep`: int (s32)

## WipeKoopa

- `mFrame`: int (s32)

## WipeRing

- `mAnimFrame`: int (s32)
- `mCenterPos.x`: float
- `mCenterPos.y`: float
- `mIsCenterOnPlayer`: bool
- `mWipeInFrame`: float
- `mWipeOutFrame`: float

## WoodBox

- `mBreakModel`: ModelObj (an object, read-only)
- `mCoinCount`: int (s32)
- `mFloorTouchTimer`: int (s32)
- `mHasPowerStar`: bool
- `mHitPoint`: int (u16)
- `mIsNoRespawn`: bool
- `mOneUp`: BenefitItemOneUp (an object, read-only)
- `mPlaySolveSE`: bool
- `mStarBitCount`: int (s32)
- `mStarDemoModel`: ModelObj (an object, read-only)

## WormEatenPlanet

- `mCaterpillar`: GreenCaterpillarBig (an object, read-only)

## YesNoLayout

- `mHasInformationMessage`: bool

