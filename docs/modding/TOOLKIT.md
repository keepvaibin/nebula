# Toolkit

Every game function a script or plugin calls by name, generated from the game's headers (tools/lumacore). Letters: a none, b bool, c int, d float, e vec3, g text, h handle (an object, a sensor...); an upper-case letter is a value that comes back after the result (pass one in, or none for zero).

3552 functions.

| Name | Takes | Gives |
|---|---|---|
| `AreaObj::awake` | h self (AreaObj) |  |
| `AreaObj::getFollowMtx` | h self (AreaObj) | f |
| `AreaObj::getManagerName` | h self (AreaObj) | g |
| `AreaObj::init` | h self (AreaObj), h  |  |
| `AreaObj::invalidate` | h self (AreaObj) |  |
| `AreaObj::isInVolume` | h self (AreaObj), e  | b |
| `AreaObj::isOnSwitchA` | h self (AreaObj) | b |
| `AreaObj::isOnSwitchB` | h self (AreaObj) | b |
| `AreaObj::isValid` | h self (AreaObj) | b |
| `AreaObj::isValidSwitchA` | h self (AreaObj) | b |
| `AreaObj::isValidSwitchB` | h self (AreaObj) | b |
| `AreaObj::offSwitchA` | h self (AreaObj) |  |
| `AreaObj::onSwitchA` | h self (AreaObj) |  |
| `AreaObj::setFollowMtx` | h self (AreaObj), f  |  |
| `AreaObj::sleep` | h self (AreaObj) |  |
| `AreaObj::validate` | h self (AreaObj) |  |
| `LayoutActor::appear` | h self (LayoutActor) |  |
| `LayoutActor::calcAnim` | h self (LayoutActor) |  |
| `LayoutActor::control` | h self (LayoutActor) |  |
| `LayoutActor::createPaneMtxRef` | h self (LayoutActor), g  |  |
| `LayoutActor::draw` | h self (LayoutActor) |  |
| `LayoutActor::getLayoutManager` | h self (LayoutActor) | h |
| `LayoutActor::getNerveStep` | h self (LayoutActor) | c |
| `LayoutActor::getPaneMtxRef` | h self (LayoutActor), g  | f |
| `LayoutActor::getTrans` | h self (LayoutActor) | j |
| `LayoutActor::initEffectKeeper` | h self (LayoutActor), c , g , h  |  |
| `LayoutActor::initLayoutManager` | h self (LayoutActor), g , c  |  |
| `LayoutActor::initLayoutManagerNoConvertFilename` | h self (LayoutActor), g , c  |  |
| `LayoutActor::initLayoutManagerWithTextBoxBufferLength` | h self (LayoutActor), g , c , c  |  |
| `LayoutActor::initNerve` | h self (LayoutActor), h  |  |
| `LayoutActor::initPointingTarget` | h self (LayoutActor), c  |  |
| `LayoutActor::isNerve` | h self (LayoutActor), h  | b |
| `LayoutActor::kill` | h self (LayoutActor) |  |
| `LayoutActor::movement` | h self (LayoutActor) |  |
| `LayoutActor::setNerve` | h self (LayoutActor), h  |  |
| `LayoutActor::setTrans` | h self (LayoutActor), j  |  |
| `LayoutActor::updateSpine` | h self (LayoutActor) |  |
| `LiveActor::addToSoundObjHolder` | h self (LiveActor) |  |
| `LiveActor::appear` | h self (LiveActor) |  |
| `LiveActor::attackSensor` | h self (LiveActor), h pSender, h pReceiver |  |
| `LiveActor::calcAndSetBaseMtx` | h self (LiveActor) |  |
| `LiveActor::calcAnim` | h self (LiveActor) |  |
| `LiveActor::calcAnmMtx` | h self (LiveActor) |  |
| `LiveActor::calcViewAndEntry` | h self (LiveActor) |  |
| `LiveActor::control` | h self (LiveActor) |  |
| `LiveActor::editPosition` | h self (LiveActor) | e |
| `LiveActor::endClipped` | h self (LiveActor) |  |
| `LiveActor::getBaseMtx` | h self (LiveActor) | f |
| `LiveActor::getBinder` | h self (LiveActor) | h |
| `LiveActor::getGravity` | h self (LiveActor) | e |
| `LiveActor::getNerveStep` | h self (LiveActor) | c |
| `LiveActor::getPosition` | h self (LiveActor) | e |
| `LiveActor::getSensor` | h self (LiveActor), g pSensorName | h |
| `LiveActor::getSensorKeeper` | h self (LiveActor) | h |
| `LiveActor::getTakingMtx` | h self (LiveActor) | f |
| `LiveActor::getVelocity` | h self (LiveActor) | e |
| `LiveActor::init` | h self (LiveActor), h rIter |  |
| `LiveActor::initActorCollisionParts` | h self (LiveActor), g , h , h , F , b , b  |  |
| `LiveActor::initActorLightCtrl` | h self (LiveActor) |  |
| `LiveActor::initActorStarPointerTarget` | h self (LiveActor), d , e , F , e  |  |
| `LiveActor::initBinder` | h self (LiveActor), d radius, d offsetY, c planeNum |  |
| `LiveActor::initEffectKeeper` | h self (LiveActor), c extraEmitterNum, g pResourceName |  |
| `LiveActor::initHitSensor` | h self (LiveActor), c  |  |
| `LiveActor::initModelManagerWithAnm` | h self (LiveActor), g pModelArcName, g pAnimArcName, b deferMaterials |  |
| `LiveActor::initNerve` | h self (LiveActor), h  |  |
| `LiveActor::initRailRider` | h self (LiveActor), h rIter |  |
| `LiveActor::initShadowControllerList` | h self (LiveActor), c  |  |
| `LiveActor::initSound` | h self (LiveActor), c numHandles, b is2D |  |
| `LiveActor::initStageSwitch` | h self (LiveActor), h rIter |  |
| `LiveActor::isNerve` | h self (LiveActor), h pNerve | b |
| `LiveActor::kill` | h self (LiveActor) |  |
| `LiveActor::makeActorAppeared` | h self (LiveActor) |  |
| `LiveActor::makeActorDead` | h self (LiveActor) |  |
| `LiveActor::movement` | h self (LiveActor) |  |
| `LiveActor::prepareDrawClipped` | h self (LiveActor) |  |
| `LiveActor::receiveMessage` | h self (LiveActor), c msg, h pSender, h pReceiver | b |
| `LiveActor::receiveMsgApart` | h self (LiveActor), h pSender, h pReceiver | b |
| `LiveActor::receiveMsgEnemyAttack` | h self (LiveActor), c , h , h  | b |
| `LiveActor::receiveMsgPlayerAttack` | h self (LiveActor), c , h , h  | b |
| `LiveActor::receiveMsgPush` | h self (LiveActor), h , h  | b |
| `LiveActor::receiveMsgTake` | h self (LiveActor), h , h  | b |
| `LiveActor::receiveMsgTaken` | h self (LiveActor), h , h  | b |
| `LiveActor::receiveMsgThrow` | h self (LiveActor), h , h  | b |
| `LiveActor::receiveOtherMsg` | h self (LiveActor), c , h , h  | b |
| `LiveActor::setNerve` | h self (LiveActor), h pNerve |  |
| `LiveActor::startClipped` | h self (LiveActor) |  |
| `LiveActor::updateBinder` | h self (LiveActor) |  |
| `LiveActor::updateHitSensor` | h self (LiveActor), h  |  |
| `MR::MOD` | d x, d y | d |
| `MR::abs:c` | c x | c |
| `MR::abs:d` | d x | d |
| `MR::accelerateRailCoordSpeed` | h , d  |  |
| `MR::acos` | d x | d |
| `MR::acosEx` | d x | d |
| `MR::activateDefaultGameLayout` |  |  |
| `MR::activateGameSceneDraw3D` |  |  |
| `MR::activeStarPointerGuidance` |  |  |
| `MR::actorSoundMovement` | h  |  |
| `MR::addArchive` | h pFolder, h pArena, g pFilePath |  |
| `MR::addBaseMatrixFollowTarget` | h , h , f , h  |  |
| `MR::addBaseMatrixFollower` | h  |  |
| `MR::addBodyMessageSensor` | h pActor, c type | h |
| `MR::addBodyMessageSensorEnemy` | h pActor | h |
| `MR::addBodyMessageSensorMapObj` | h pActor | h |
| `MR::addBodyMessageSensorMapObjMoveCollision` | h pActor | h |
| `MR::addBodyMessageSensorMapObjPress` | h pActor | h |
| `MR::addBodyMessageSensorReceiver` | h pActor | h |
| `MR::addEffect` | h , g  |  |
| `MR::addEffectHitNormal` | h , g  |  |
| `MR::addFilePrefix` | h rDst, g pChild, g pParent |  |
| `MR::addHitSensor` | h pActor, g pName, c type, c groupSize, d radius, e rOffset | h |
| `MR::addHitSensorAtJoint` | h pActor, g pName, g pJointName, c type, c groupSize, d radius, e rOffset | h |
| `MR::addHitSensorAtJointEnemy` | h pActor, g pName, g pJointName, c groupSize, d radius, e rOffset | h |
| `MR::addHitSensorAtJointEnemyAttack` | h pActor, g pName, g pJointName, c groupSize, d radius, e rOffset | h |
| `MR::addHitSensorAtJointEnemySimple` | h pActor, g pName, g pJointName, c groupSize, d radius, e rOffset | h |
| `MR::addHitSensorAtJointEye` | h pActor, g pName, g pJointName, c groupSize, d radius, e rOffset | h |
| `MR::addHitSensorAtJointMapObj` | h pActor, g pName, g pJointName, c groupSize, d radius, e rOffset | h |
| `MR::addHitSensorAtJointMapObjSimple` | h pActor, g pName, g pJointName, c groupSize, d radius, e rOffset | h |
| `MR::addHitSensorAtJointNpc` | h pActor, g pName, g pJointName, c groupSize, d radius, e rOffset | h |
| `MR::addHitSensorAtJointRide` | h pActor, g pName, g pJointName, c groupSize, d radius, e rOffset | h |
| `MR::addHitSensorBinder` | h pActor, g pName, c groupSize, d radius, e rOffset | h |
| `MR::addHitSensorCallback` | h pActor, g pName, c type, c groupSize, d radius | h |
| `MR::addHitSensorCallbackBinder` | h pActor, g pName, c groupSize, d radius | h |
| `MR::addHitSensorCallbackEnemy` | h pActor, g pName, c groupSize, d radius | h |
| `MR::addHitSensorCallbackEnemyAttack` | h pActor, g pName, c groupSize, d radius | h |
| `MR::addHitSensorCallbackEye` | h pActor, g pName, c groupSize, d radius | h |
| `MR::addHitSensorCallbackMapObj` | h pActor, g pName, c groupSize, d radius | h |
| `MR::addHitSensorCallbackMapObjSimple` | h pActor, g pName, c groupSize, d radius | h |
| `MR::addHitSensorCallbackPriorBinder` | h pActor, g pName, c groupSize, d radius | h |
| `MR::addHitSensorCallbackRide` | h pActor, g pName, c groupSize, d radius | h |
| `MR::addHitSensorEnemy` | h pActor, g pName, c groupSize, d radius, e rOffset | h |
| `MR::addHitSensorEnemyAttack` | h pActor, g pName, c groupSize, d radius, e rOffset | h |
| `MR::addHitSensorEnemySimple` | h pActor, g pName, c groupSize, d radius, e rOffset | h |
| `MR::addHitSensorEye` | h pActor, g pName, c groupSize, d radius, e rOffset | h |
| `MR::addHitSensorMapObj` | h pActor, g pName, c groupSize, d radius, e rOffset | h |
| `MR::addHitSensorMapObjMoveCollision` | h pActor, g pName, c groupSize, d radius, e rOffset | h |
| `MR::addHitSensorMapObjPress` | h pActor, g pName, c groupSize, d radius, e rOffset | h |
| `MR::addHitSensorMapObjSimple` | h pActor, g pName, c groupSize, d radius, e rOffset | h |
| `MR::addHitSensorMtx` | h pActor, g pName, c type, c groupSize, d radius, F pMtx, e rOffset | h |
| `MR::addHitSensorMtxAnimal` | h pActor, g pName, c groupSize, d radius, F pMtx, e rOffset | h |
| `MR::addHitSensorMtxEnemy` | h pActor, g pName, c groupSize, d radius, F pMtx, e rOffset | h |
| `MR::addHitSensorMtxEnemyAttack` | h pActor, g pName, c groupSize, d radius, F pMtx, e rOffset | h |
| `MR::addHitSensorMtxMapObj` | h pActor, g pName, c groupSize, d radius, F pMtx, e rOffset | h |
| `MR::addHitSensorMtxNpc` | h pActor, g pName, c groupSize, d radius, F pMtx, e rOffset | h |
| `MR::addHitSensorMtxRide` | h pActor, g pName, c groupSize, d radius, F pMtx, e rOffset | h |
| `MR::addHitSensorNpc` | h pActor, g pName, c groupSize, d radius, e rOffset | h |
| `MR::addHitSensorPosBinder` | h pActor, g pName, c groupSize, d radius, e pPos, e rOffset | h |
| `MR::addHitSensorPosEye` | h pActor, g pName, c groupSize, d radius, e pPos, e rOffset | h |
| `MR::addHitSensorPosMapObj` | h pActor, g pName, c groupSize, d radius, e pPos, e rOffset | h |
| `MR::addHitSensorPosRide` | h pActor, g pName, c groupSize, d radius, e pPos, e rOffset | h |
| `MR::addHitSensorPriorBinder` | h pActor, g pName, c groupSize, d radius, e rOffset | h |
| `MR::addHitSensorPush` | h pActor, g pName, c groupSize, d radius, e rOffset | h |
| `MR::addHitSensorRide` | h pActor, g pName, c groupSize, d radius, e rOffset | h |
| `MR::addHitSensorTransferableBinder` | h pActor, g pName, c groupSize, d radius, e rOffset | h |
| `MR::addJointToSeKeeper` | h , g pJointName, g pJointName2 |  |
| `MR::addMessageSensorEnemy` | h pActor, g pName | h |
| `MR::addMessageSensorMapObj` | h pActor, g pName | h |
| `MR::addMessageSensorMapObjMoveCollision` | h pActor, g pName | h |
| `MR::addMessageSensorReceiver` | h pActor, g pName | h |
| `MR::addRandomVector` | E pDst, e rSrc, d range |  |
| `MR::addShadowSurfaceCircle` | h , g , d  |  |
| `MR::addShadowVolumeBox:hge` | h , g , e  |  |
| `MR::addShadowVolumeBox:hgeF` | h , g , e , F  |  |
| `MR::addShadowVolumeCylinder` | h , g , d  |  |
| `MR::addShadowVolumeFlatModel:hgg` | h , g pName, g pModelName |  |
| `MR::addShadowVolumeFlatModel:hggF` | h , g pName, g pModelName, F  |  |
| `MR::addShadowVolumeLine` | h , g pName, h pFromActor, g pFromName, d fromWidth, h pToActor, g pToName, d toWidth |  |
| `MR::addShadowVolumeOval` | h , g , e , F  |  |
| `MR::addShadowVolumeSphere` | h , g , d  |  |
| `MR::addStarPiece` | c  |  |
| `MR::addStarPieceGivingToTicoGalaxy` | c , c  |  |
| `MR::addStarPieceGivingToTicoSeed` | c , c  |  |
| `MR::addStarPointerTargetCircle` | h , g , d , j , g  |  |
| `MR::addStockedStarPiece` | c  |  |
| `MR::addToAttributeGroupReflectSpinningBox` | h  |  |
| `MR::addToAttributeGroupSearchTurtle` | h  |  |
| `MR::addTransMtx` | F , e  |  |
| `MR::addTransMtxLocal` | F , e  |  |
| `MR::addTransMtxLocalX` | F , d  |  |
| `MR::addTransMtxLocalY` | F , d  |  |
| `MR::addTransMtxLocalZ` | F , d  |  |
| `MR::addVelocity` | h , e  |  |
| `MR::addVelocityAwayFromTarget:hed` | h , e , d  |  |
| `MR::addVelocityAwayFromTarget:hedddd` | h , e , d , d , d , d  |  |
| `MR::addVelocityClockwiseToDirection` | h , e , d  |  |
| `MR::addVelocityClockwiseToPlayer` | h , d  |  |
| `MR::addVelocityClockwiseToTarget` | h , e , d  |  |
| `MR::addVelocityFromPush` | h , d , h , h  |  |
| `MR::addVelocityFromPushHorizon` | h , d , h , h  |  |
| `MR::addVelocityJump` | h , d  |  |
| `MR::addVelocityKeepHeight:hedd` | h , e , d , d  |  |
| `MR::addVelocityKeepHeight:heddd` | h , e , d , d , d  |  |
| `MR::addVelocityKeepHeightUseShadow:hddddg` | h , d , d , d , d , g  | b |
| `MR::addVelocityKeepHeightUseShadow:hdddg` | h , d , d , d , g  | b |
| `MR::addVelocityLimit` | h , e  | b |
| `MR::addVelocityMoveToDirection` | h , e , d  |  |
| `MR::addVelocityMoveToTarget:hed` | h , e , d  |  |
| `MR::addVelocityMoveToTarget:hedddd` | h , e , d , d , d , d  |  |
| `MR::addVelocitySeparateHV:hedd` | h , e , d , d  |  |
| `MR::addVelocitySeparateHV:hhhdd` | h , h , h , d , d  |  |
| `MR::addVelocityToCollisionNormal` | h , d  | b |
| `MR::addVelocityToGravity` | h , d  |  |
| `MR::addVelocityToGravityOrGround` | h , d  |  |
| `MR::adjustmentRailCoordSpeed` | h , d , d  |  |
| `MR::appearCoinCircle` | h , e , c  |  |
| `MR::appearCoinFix` | h , e , c  |  |
| `MR::appearCoinPop` | h , e , c  |  |
| `MR::appearCoinPopToDirection` | h , e , e , c  |  |
| `MR::appearCoinToVelocity` | h , e , e , c  |  |
| `MR::appearKinokoOneUpPop` | h , F , d  |  |
| `MR::appearKinokoSuper` | h , F , d  |  |
| `MR::appearPowerStarContinueCurrentDemo` | h , e  |  |
| `MR::appearPowerStarWithoutDemo` | h  |  |
| `MR::appearStarCounter` |  |  |
| `MR::appearStarPiece` | h , e , c , d , d , b  | b |
| `MR::appearStarPieceToDirection` | h , e , e , c , d , d , b  | b |
| `MR::appearYesNoSelector` | g , g , g  |  |
| `MR::applyFurFile` | h , h , h  |  |
| `MR::applyVelocityDampAndGravity` | h , d gravity, d groundDampH, d airDampH, d dampVUp, d velHMin |  |
| `MR::asin` | d x | d |
| `MR::atan2` | d x, d y | d |
| `MR::attenuateVelocity` | h , d  |  |
| `MR::attenuateVelocityExceptDirection` | h , e , d  |  |
| `MR::becomeContinuousBckPlayer` |  |  |
| `MR::becomePlayerNormalJumpStatus` |  |  |
| `MR::bindSpereToCone` | h pResult, e rMovement, h rSphere, h rCone | b |
| `MR::blendAngle` | d angleA, d angleB, d rate | d |
| `MR::blendColor` | h pDst, h rStart, h rEnd, d t |  |
| `MR::blendMtx` | F , F , d , F  |  |
| `MR::blendMtxRotate` | F , F , d , F  |  |
| `MR::blendMtxRotateSlerp` | F , F , d , F  |  |
| `MR::blendQuatFromGroundAndFront` | I , h , e , d , d  |  |
| `MR::blendQuatFrontUp:Ieedd` | I pDst, e rUp, e rFront, d upRate, d frontRate |  |
| `MR::blendQuatFrontUp:Iieedd` | I pDst, i rSrc, e rUp, e rFront, d upRate, d frontRate |  |
| `MR::blendQuatUpFront:Ieedd` | I pDst, e rUp, e rFront, d upRate, d frontRate |  |
| `MR::blendQuatUpFront:Iieedd` | I pDst, i rSrc, e rUp, e rFront, d upRate, d frontRate |  |
| `MR::blendVec` | E pDst, e rStart, e rEnd, d t |  |
| `MR::calcActorAxis` | E , E , E , h  |  |
| `MR::calcActorAxisX` | E , h  |  |
| `MR::calcActorAxisY` | E , h  |  |
| `MR::calcActorAxisZ` | E , h  |  |
| `MR::calcAndSetBaseMtxFromGravityAndZAxis` | h , e  |  |
| `MR::calcAndSetFloatBaseMtx` | h , d  |  |
| `MR::calcAnimDirect` | h  |  |
| `MR::calcAnimLayoutWithDrawInfo` | h , h  |  |
| `MR::calcAnimModelManager` | h  |  |
| `MR::calcAreaMoveVelocity` | E pVelocity, e rPos | b |
| `MR::calcCameraDistanceZ` | e  | d |
| `MR::calcClippingRangeIncludeShadow` | E , D , h , d  | b |
| `MR::calcCubeAxisZ` | h , E  |  |
| `MR::calcCubeLocalPos` | E , h , e  |  |
| `MR::calcCubePos` | h , E  |  |
| `MR::calcCubeRotate` | h , E  |  |
| `MR::calcCubeWorldBox` | h pBox, h pArea |  |
| `MR::calcCurrentGreenStarNum` |  | c |
| `MR::calcCylinderCenterPos` | E , h  |  |
| `MR::calcCylinderPos` | E , h  |  |
| `MR::calcCylinderUpVec` | E , h  |  |
| `MR::calcDemoPartStepRate` | g  | d |
| `MR::calcDifferenceRailCoord:hd` | h , d  | d |
| `MR::calcDifferenceRailCoord:hdd` | h , d , d  | d |
| `MR::calcDistance:he` | h , e  | d |
| `MR::calcDistance:hhE` | h , h , E  | d |
| `MR::calcDistanceHorizonToCurrentPos` | h  | d |
| `MR::calcDistanceHorizontal` | h , e , e  | d |
| `MR::calcDistanceToCurrentAndNextRailPoint` | h , D , D  |  |
| `MR::calcDistanceToNextRailPoint` | h , D  |  |
| `MR::calcDistanceToPlayer:e` | e  | d |
| `MR::calcDistanceToPlayer:h` | h  | d |
| `MR::calcDistanceToPlayerH` | h  | d |
| `MR::calcDistanceVertical:he` | h , e  | d |
| `MR::calcDistanceVertical:hee` | h , e , e  | d |
| `MR::calcDistanceXY` | e rPos1, e rPos2 | d |
| `MR::calcFloatOffset` | h , d , d  | d |
| `MR::calcFogStartEnd` | e pos, d strength, D pFogStart, D pFogEnd |  |
| `MR::calcFrontVec` | E , h  |  |
| `MR::calcGravity:h` | h  |  |
| `MR::calcGravity:he` | h , e  |  |
| `MR::calcGravityOrZero:h` | h  |  |
| `MR::calcGravityOrZero:he` | h , e  |  |
| `MR::calcGravitySpeed` | h  | d |
| `MR::calcHitPowerToGround` | h  | d |
| `MR::calcHitPowerToWall` | h  | d |
| `MR::calcLightPos0` | E , h  |  |
| `MR::calcLightPos1` | E , h  |  |
| `MR::calcLocalVec` | E pVec, F pMtx |  |
| `MR::calcMapGround` | e , E , d  | b |
| `MR::calcMapGroundUpper` | E , h  | b |
| `MR::calcModel` | h  |  |
| `MR::calcModelBoundingBox` | h , h  |  |
| `MR::calcModelBoundingRadius` | D , h  |  |
| `MR::calcMomentRollBall` | E pMoment, e rVel, e rUp, d radius |  |
| `MR::calcMovingDirectionAlongRail` | h , E , e , d , b , B  | d |
| `MR::calcMovingDirectionAlongRailH` | h , E , e , d , B  | d |
| `MR::calcMtxFromGravityAndZAxis` | F , h , e , e  |  |
| `MR::calcMtxRotAxis` | E , F , F  |  |
| `MR::calcNearestRailCoord` | h , e  | d |
| `MR::calcNearestRailDirection` | E , h , e  | d |
| `MR::calcNearestRailPos` | E , h , e  | d |
| `MR::calcNearestRailPosAndDirection` | E , E , h , e  | d |
| `MR::calcNerveEaseInOutRate:hc` | h , c  | d |
| `MR::calcNerveEaseInOutRate:hcc` | h , c , c  | d |
| `MR::calcNerveEaseOutValue` | h , c , d , d  | d |
| `MR::calcNormalizedScreenPosition` | E , e  | b |
| `MR::calcNormalizedScreenPositionFromView` | E , e  | b |
| `MR::calcOpenedAstroDomeNum` |  | c |
| `MR::calcParabolicFunctionParam` | D pAccel, D pVel, d max, d end |  |
| `MR::calcPerpendicFootToLine` | E pDst, e rPos, e rPointA, e rPointB | d |
| `MR::calcPerpendicFootToLineInside` | E pDst, e rPos, e rPointA, e rPointB | d |
| `MR::calcPlayerFaceStarePos` | E , F , F  | b |
| `MR::calcPlayerFaceStareVector` | E , F , F  | b |
| `MR::calcPlayerJointMtx` | F , g  |  |
| `MR::calcPlayerSpinPullVelocity` | E , e  |  |
| `MR::calcPlayerWorldPadDir` | E , d , d  |  |
| `MR::calcPointRadius2D` | e , d  | d |
| `MR::calcPosBetweenSensors` | E pPos, h pSensor1, h pSensor2, d offset |  |
| `MR::calcPositionUpOffset` | E , h , d  |  |
| `MR::calcRailClippingInfo` | E , D , h , d , d  |  |
| `MR::calcRailDirectionAtCoord` | E , h , d  |  |
| `MR::calcRailEndPointDirection` | E , h  |  |
| `MR::calcRailEndPointPos` | E , h  |  |
| `MR::calcRailEndPointPosAndDirection` | E , E , h  |  |
| `MR::calcRailEndPos` | E , h  |  |
| `MR::calcRailPointPos` | E , h , c  |  |
| `MR::calcRailPosAndDirectionAtCoord` | E , E , h , d  |  |
| `MR::calcRailPosAtCoord` | E , h , d  |  |
| `MR::calcRailPosFrontCoord` | E , h , d  |  |
| `MR::calcRailPosNearestPlayer` | E , h  | d |
| `MR::calcRailRateToNextPoint` | D , h  |  |
| `MR::calcRailStartPointDirection` | E , h  |  |
| `MR::calcRailStartPointPos` | E , h  |  |
| `MR::calcRailStartPointPosAndDirection` | E , E , h  |  |
| `MR::calcRailStartPos` | E , h  |  |
| `MR::calcRandomVec` | E pDst, d min, d max |  |
| `MR::calcReboundVelocity:Eed` | E pVel, e rNormal, d reboundRate | b |
| `MR::calcReboundVelocity:Eedd` | E pVel, e rNormal, d reboundRateV, d reboundRateH | b |
| `MR::calcReflectionVector` | E pDir, e rNorm, d rate, d cosAngleMin | b |
| `MR::calcRotateY` | d x, d z | d |
| `MR::calcRotateZ` | e rVecA, e rVecB | d |
| `MR::calcScreenPosition:Ee` | E , e  | b |
| `MR::calcScreenPosition:Je` | J , e  | b |
| `MR::calcSensorDirection` | E pDir, h pSensor1, h pSensor2 |  |
| `MR::calcSensorDirectionNormalize` | E pDir, h pSensor1, h pSensor2 |  |
| `MR::calcSensorHorizonNormalize` | E pHorizon, e rGravity, h pSensor1, h pSensor2 |  |
| `MR::calcSideVec` | E , h  |  |
| `MR::calcSpherePos` | E , h  |  |
| `MR::calcStarPointerPosOnPlane` | E , e , e , c , b  | b |
| `MR::calcStarPointerScreenDistanceToTarget` | h , D , c  | b |
| `MR::calcStarPointerStrokeRotateMoment` | E , e , d , c  | b |
| `MR::calcStarPointerWorldPointingPos` | E , e , c  |  |
| `MR::calcStarPointerWorldPointingPosInsideEdge` | E , e , c  |  |
| `MR::calcStarPointerWorldVelocityDirectionOnPlane` | E , e , e , c  | b |
| `MR::calcTexProjectionMtx` | F  |  |
| `MR::calcTextBoxRectRecursive` | h , h , g  |  |
| `MR::calcUpVec` | E , h  |  |
| `MR::calcVecFromPlayerH` | E , h  |  |
| `MR::calcVecFromTargetPosH` | E , h , e , e  |  |
| `MR::calcVecToPlayerH` | E , h , e  |  |
| `MR::calcVecToTargetPosH` | E , h , e , e  |  |
| `MR::calcVelocityAreaMoveOnGround` | E , h  | b |
| `MR::calcVelocityAreaOrRailMoveOnGround` | E , h  | b |
| `MR::calcVelocityLength` | h  | d |
| `MR::calcVelocityMoveToDirection:Ehed` | E , h , e , d  |  |
| `MR::calcVelocityMoveToDirection:Ehedddd` | E , h , e , d , d , d , d  |  |
| `MR::calcVelocityMoveToDirectionHorizon:Ehed` | E , h , e , d  |  |
| `MR::calcVelocityMoveToDirectionHorizon:Ehedddd` | E , h , e , d , d , d , d  |  |
| `MR::calcVelocityMoveToTarget` | E , h , e , d  |  |
| `MR::calcVelocityMovingPoint` | h , e , E  |  |
| `MR::calcVelocityRailMoveOnGround` | E , h  | b |
| `MR::calcWallNormalHorizontal` | E , h  |  |
| `MR::calcWatchEdgeDirection` | h pRailGraphIter, E pVec |  |
| `MR::calcWatchEdgeVector` | h pIter, E pEdge |  |
| `MR::calcWhirlPoolAccelInfo` | e , E  | b |
| `MR::calcWorldPositionFromCenterScreen` | E , j , d  | b |
| `MR::calcWorldPositionFromScreen` | E , j , d  | b |
| `MR::calcWorldRayDirectionFromScreen` | E , j  | b |
| `MR::calcWorldStickDirectionXZ:DDc` | D pDirX, D pDirZ, c channel |  |
| `MR::calcWorldStickDirectionXZ:Ec` | E pDir, c channel |  |
| `MR::callAppearAllGroupMember` | h  |  |
| `MR::callInvalidateClippingAllGroupMember` | h  |  |
| `MR::callKillAllGroupMember` | h  |  |
| `MR::callMakeActorAppearedAllGroupMember` | h  |  |
| `MR::callMakeActorDeadAllGroupMember` | h  |  |
| `MR::callRequestMovementOnAllGroupMember` | h  |  |
| `MR::callValidateClippingAllGroupMember` | h  |  |
| `MR::canAppearCoin100Comet` | g  | b |
| `MR::canAppearNormalComet` | g  | b |
| `MR::canOpenGalaxy` | g  | b |
| `MR::canStartDemo` |  | b |
| `MR::captureScreenIfAllow` | g  |  |
| `MR::changeEventCameraTarget` | h , g , h  |  |
| `MR::changeModelDataTexAll` | h , g , h  |  |
| `MR::changePlayerItemStatus` | c status |  |
| `MR::changeShowModelFlagSyncNearClipping` | h , d  | b |
| `MR::checkHitSegmentSphere` | e rSpherePos, e rPointA, e rPointB, d radius, E pDir | b |
| `MR::checkHitSemilinePlane` | E pHitPos, e rPos, e rDir, e rPlanePos, e rPlaneNorm | b |
| `MR::checkPassBckFrame` | h , d  | b |
| `MR::checkPlayerActionTrigger` |  | b |
| `MR::checkPlayerSwingTrigger` |  | b |
| `MR::checkStrikeBallToMap` | e , d  | b |
| `MR::checkStrikePointToMap` | e , h  | b |
| `MR::checkTableEntries` | h rIter | b |
| `MR::clamp` | d x, d min, d max | d |
| `MR::clamp01:D` | D pX |  |
| `MR::clamp01:d` | d x | d |
| `MR::clampBoth` | D pValue, d min, d max |  |
| `MR::clampLength` | E pDst, e rSrc, d length |  |
| `MR::clampMax` | D pValue, d max |  |
| `MR::clampVecAngleDeg` | E pDst, e rBase, d angle |  |
| `MR::cleanEventCameraTarget_temporally` |  |  |
| `MR::clearBgmQueue` |  |  |
| `MR::clearCurrentPlacementZoneId` |  |  |
| `MR::clearGotCountStarPieceReceiver` | h  |  |
| `MR::clearHitSensors` | h pActor |  |
| `MR::clearStencil` |  |  |
| `MR::clearTalkState` | h  |  |
| `MR::clearTextBoxMessageRecursive` | h , g  |  |
| `MR::clearZBuffer` |  |  |
| `MR::closeSystemWipeCircle` | c frame? |  |
| `MR::closeSystemWipeCircleWithCaptureScreen` | c frame? |  |
| `MR::closeSystemWipeFade` | c frame? |  |
| `MR::closeSystemWipeFadeWithCaptureScreen` | c frame? |  |
| `MR::closeSystemWipeWhiteFade` | c frame? |  |
| `MR::closeWipeCircle` | c frame? |  |
| `MR::closeWipeFade` | c frame? |  |
| `MR::closeWipeWhiteFade` | c frame? |  |
| `MR::completeCameraParameters` |  |  |
| `MR::connectToClippedMapParts` | h  |  |
| `MR::connectToScene3DModelFor2D` | h  |  |
| `MR::connectToSceneAir` | h  |  |
| `MR::connectToSceneAreaObj` | h  |  |
| `MR::connectToSceneCamera` | h  |  |
| `MR::connectToSceneCollisionEnemy` | h  |  |
| `MR::connectToSceneCollisionEnemyMovement` | h  |  |
| `MR::connectToSceneCollisionEnemyNoShadowedMapObjStrongLight` | h  |  |
| `MR::connectToSceneCollisionEnemyStrongLight` | h  |  |
| `MR::connectToSceneCollisionMapObj` | h  |  |
| `MR::connectToSceneCollisionMapObjMovementCalcAnim` | h  |  |
| `MR::connectToSceneCollisionMapObjStrongLight` | h  |  |
| `MR::connectToSceneCollisionMapObjWeakLight` | h  |  |
| `MR::connectToSceneCrystal` | h  |  |
| `MR::connectToSceneEnemy` | h  |  |
| `MR::connectToSceneEnemyDecoration` | h  |  |
| `MR::connectToSceneEnemyDecorationMovement` | h  |  |
| `MR::connectToSceneEnemyDecorationMovementCalcAnim` | h  |  |
| `MR::connectToSceneEnemyMovement` | h  |  |
| `MR::connectToSceneEnvironment` | h  |  |
| `MR::connectToSceneEnvironmentStrongLight` | h  |  |
| `MR::connectToSceneIndirectEnemy` | h  |  |
| `MR::connectToSceneIndirectMapObj` | h  |  |
| `MR::connectToSceneIndirectMapObjStrongLight` | h  |  |
| `MR::connectToSceneIndirectNpc` | h  |  |
| `MR::connectToSceneItem` | h  |  |
| `MR::connectToSceneItemStrongLight` | h  |  |
| `MR::connectToSceneLayout` | h  |  |
| `MR::connectToSceneLayoutDecoration` | h  |  |
| `MR::connectToSceneLayoutMovement` | h  |  |
| `MR::connectToSceneLayoutMovementCalcAnim` | h  |  |
| `MR::connectToSceneLayoutOnPause` | h  |  |
| `MR::connectToSceneLayoutOnPauseMovementCalcAnim` | h  |  |
| `MR::connectToSceneLayoutOnPauseNoMovement` | h  |  |
| `MR::connectToSceneMapObj` | h  |  |
| `MR::connectToSceneMapObjDecoration` | h  |  |
| `MR::connectToSceneMapObjDecorationMovement` | h  |  |
| `MR::connectToSceneMapObjDecorationStrongLight` | h  |  |
| `MR::connectToSceneMapObjMovement` | h  |  |
| `MR::connectToSceneMapObjMovementCalcAnim` | h  |  |
| `MR::connectToSceneMapObjNoCalcAnim` | h  |  |
| `MR::connectToSceneMapObjNoCalcAnimStrongLight` | h  |  |
| `MR::connectToSceneMapObjNoMovement` | h  |  |
| `MR::connectToSceneMapObjStrongLight` | h  |  |
| `MR::connectToSceneMapParts` | h  |  |
| `MR::connectToSceneMirrorMapObj` | h  |  |
| `MR::connectToSceneMirrorMapObjDecoration` | h  |  |
| `MR::connectToSceneMirrorMapObjNoMovement` | h  |  |
| `MR::connectToSceneMovie` | h  |  |
| `MR::connectToSceneNoShadowedMapObj` | h  |  |
| `MR::connectToSceneNoShadowedMapObjStrongLight` | h  |  |
| `MR::connectToSceneNoSilhouettedMapObj` | h  |  |
| `MR::connectToSceneNoSilhouettedMapObjStrongLight` | h  |  |
| `MR::connectToSceneNoSilhouettedMapObjWeakLightNoMovement` | h  |  |
| `MR::connectToSceneNormalMapObj` | h  |  |
| `MR::connectToSceneNpc` | h  |  |
| `MR::connectToSceneNpcMovement` | h  |  |
| `MR::connectToScenePlanet` | h  |  |
| `MR::connectToSceneRide` | h  |  |
| `MR::connectToSceneScreenEffectMovement` | h  |  |
| `MR::connectToSceneSky` | h  |  |
| `MR::connectToSceneSun` | h  |  |
| `MR::connectToSceneTalkLayout` | h  |  |
| `MR::connectToSceneTalkLayoutNoMovement` | h  |  |
| `MR::connectToSceneWipeLayout` | h  |  |
| `MR::convergeRadian` | d angle, d target, d speed | d |
| `MR::convertLayoutPosToScreenPos` | J , j  |  |
| `MR::convertPaneLocalPosToScreenPos` | J , h , j  |  |
| `MR::convertPosOnGround` | E , e  | b |
| `MR::convertScreenPosToFrame` | J , j  |  |
| `MR::convertScreenPosToLayoutPos` | J , j  |  |
| `MR::copyJointScale` | h , g , E  |  |
| `MR::copyLayoutDrawInfoWithAspect` | h , h , b  |  |
| `MR::copyPaneScale` | J , h , g  |  |
| `MR::copyPaneTrans` | J , h , g  |  |
| `MR::copyTransRotateScale` | h , h  |  |
| `MR::cos` | d x | d |
| `MR::cosDegree` | d x | d |
| `MR::countHideGroupMember` | h  | c |
| `MR::countMessageChar` | g  | c |
| `MR::countMessageFigure` | c  | c |
| `MR::countMessageLine` | g  | c |
| `MR::countShowGroupMember` | h  | c |
| `MR::coverScreenForSceneChange` |  | b |
| `MR::createActorCameraInfo` | h  | h |
| `MR::createAndAddGameLayoutHolder` | g pArcName | h |
| `MR::createAndAddGroupCtrl` | h , g , c  |  |
| `MR::createAndAddLayoutHolder` | g  | h |
| `MR::createAndAddLayoutHolderRawData` | g  | h |
| `MR::createAndAddPaneCtrl` | h , g , c  |  |
| `MR::createAndAddResourceHolder` | g  | h |
| `MR::createAndSetupIconAButton` | h , b , b  | h |
| `MR::createAreaPolygonList` | h , c , e , e  | c |
| `MR::createAreaPolygonListArray` | h , c , E , c  | c |
| `MR::createBloomModel` | h , F  | h |
| `MR::createBoundingBox` | e pPoints, c numPoints, E pMin, E pMax |  |
| `MR::createCenterScreenBlur` |  |  |
| `MR::createDepthOfFieldBlur` |  |  |
| `MR::createIndirectNPCGoods` | h , g , g  | h |
| `MR::createIndirectPlanetModel` | h , F  | h |
| `MR::createKinokoOneUp` |  | h |
| `MR::createKinokoSuper` |  | h |
| `MR::createLodCtrlMapObj` | h , h , d  | h |
| `MR::createLodCtrlNPC` | h , h  | h |
| `MR::createLodCtrlPlanet` | h , h , d  | h |
| `MR::createLytTexMap:gg` | g , g  | h |
| `MR::createLytTexMap:h` | h  | h |
| `MR::createMapFlag` | g pName, g pInfoName, e pPos, e rSide, d stickLength, d width, d height, c numPointsU, c numPointsV | h |
| `MR::createMapPartsRailGuideDrawer` | h , g , h  | h |
| `MR::createMirrorCamera` |  |  |
| `MR::createModel` | h , g , c  | h |
| `MR::createModelObjBloomModel` | g , g , F  | h |
| `MR::createModelObjEnemy` | g , g , F  | h |
| `MR::createModelObjIndirectMapObj` | g , g , F  | h |
| `MR::createModelObjMapObj` | g , g , F  | h |
| `MR::createModelObjMapObjStrongLight` | g , g , F  | h |
| `MR::createModelObjNoSilhouettedMapObj` | g , g , F  | h |
| `MR::createModelObjNoSilhouettedMapObjStrongLight` | g , g , F  | h |
| `MR::createModelObjNpc` | g , g , F  | h |
| `MR::createModelObjPlanetLow` | g , g , F  | h |
| `MR::createModelObjPlayerDecoration` | g , g , F  | h |
| `MR::createNPCGoods` | h , g , g  | h |
| `MR::createNormalBloom` |  |  |
| `MR::createPartsModelEnemy` | h , g , g , F  | h |
| `MR::createPartsModelEnemyAndFix` | h , g , g , F , e , e , g  | h |
| `MR::createPartsModelIndirectNpc` | h , g , g , F  | h |
| `MR::createPartsModelMapObj` | h , g , g , F  | h |
| `MR::createPartsModelMapObjStrongLight` | h , g , g , F  | h |
| `MR::createPartsModelNoSilhouettedMapObj` | h , g , g , F  | h |
| `MR::createPartsModelNpc` | h , g , g , F  | h |
| `MR::createPartsModelNpcAndFix` | h , g , g , g  | h |
| `MR::createPowerStarDemoModel` | h , g , F  | h |
| `MR::createPurpleCoinCounter` |  |  |
| `MR::createRailGraphFromPlacement` | h rIter | h |
| `MR::createRailGraphIter` | h pRailGraph | h |
| `MR::createScene` | c kind | h |
| `MR::createScreenBlur` |  |  |
| `MR::createSimpleBloom` |  |  |
| `MR::createSimpleLayout` | g , g , c  | h |
| `MR::createSimpleLayoutTalkParts` | g , g , c  | h |
| `MR::createStarPointerLayout` |  |  |
| `MR::createTalkCtrl` | h , h , g , e , F  | h |
| `MR::createTalkCtrlDirect` | h , h , g , e , F  | h |
| `MR::createTalkCtrlDirectOnRootNodeAutomatic` | h , h , g , e , F  | h |
| `MR::createWaterModel` | h , F  | h |
| `MR::crossOrPerpendicular` | E pDst, e rA, e rB |  |
| `MR::curePlayerElementMode` |  |  |
| `MR::deactivateDefaultGameLayout` |  |  |
| `MR::deactivateGameSceneDraw3D` |  |  |
| `MR::deactiveStarPointerGuidance` |  |  |
| `MR::decPlayerLeft` |  |  |
| `MR::decidePose` | h , e , e , e , d , d , d  |  |
| `MR::declareBlackHoleCamera` | g  |  |
| `MR::declareCameraRegisterMtx` | h , c , F  |  |
| `MR::declareCameraRegisterVec` | h , c , E  |  |
| `MR::declareCoin` | h , c  |  |
| `MR::declareEventCamera` | h , g  |  |
| `MR::declareEventCameraProgrammable` | g  |  |
| `MR::declareGlobalEventCamera` | g  |  |
| `MR::declareGlobalEventCameraAbyss` | g  |  |
| `MR::declareGlobalEventCameraDead` | g , d , c , c  |  |
| `MR::declareGlobalEventCameraFixedThere` | g , b , d  |  |
| `MR::declareLauncherCamera` |  |  |
| `MR::declareLauncherFlightCamera` |  |  |
| `MR::declarePowerStar:h` | h  |  |
| `MR::declarePowerStar:hc` | h , c  |  |
| `MR::declarePowerStarCoin100` |  |  |
| `MR::declareStarPiece` | h , c  |  |
| `MR::declareStarPieceReceiver` | h , c  |  |
| `MR::destroySceneMessage` |  |  |
| `MR::diffAngleAbs:dd` | d angle1, d angle2 | d |
| `MR::diffAngleAbs:ee` | e rA, e rB | d |
| `MR::diffAngleAbs:jj` | j rA, j rB | d |
| `MR::diffAngleAbsFast` | e rA, e rB | d |
| `MR::diffAngleAbsHorizontal` | e rA, e rB, e rAxis | d |
| `MR::diffAngleSignedHorizontal` | e rA, e rB, e rAxis | d |
| `MR::disableStarPointerShootStarPiece` |  |  |
| `MR::disappearInformationMessage` |  |  |
| `MR::disappearStarCounter` |  |  |
| `MR::disappearStarPieceCounterForTicoFat` |  |  |
| `MR::drawInitFor2DModel` |  |  |
| `MR::drawLayoutWithDrawInfoWithoutProjectionSetup` | h , h  |  |
| `MR::drawScreenColor` | h rColor |  |
| `MR::drawScreenPicture` | h rTexture |  |
| `MR::emitEffectHit:hFg` | h , F , g  |  |
| `MR::emitEffectHit:heeg` | h , e , e , g  |  |
| `MR::emitEffectHit:heg` | h , e , g  |  |
| `MR::emitEffectHitBetweenSensors` | h , h , h , d , g  |  |
| `MR::emitEffectWithEmitterCallBack` | h , g , h  | h |
| `MR::emitEffectWithParticleCallBack` | h , g , h  | h |
| `MR::emitEffectWithScale` | h , g pEffectName, d scale, c emitterIndex | h |
| `MR::enableStarPointerShootStarPiece` |  |  |
| `MR::endActorCamera` | h , h , b , c ? | b |
| `MR::endActorCameraAtLanding` | h , h , c ? | b |
| `MR::endActorCameraProgrammable` | h , c , b  |  |
| `MR::endAnimCamera` | h , h , g , c , b  |  |
| `MR::endBindAndPlayerAcidDamage` | h  |  |
| `MR::endBindAndPlayerDamage` | h , e  |  |
| `MR::endBindAndPlayerDamageMsg:hc` | h , c  |  |
| `MR::endBindAndPlayerDamageMsg:hce` | h , c , e  |  |
| `MR::endBindAndPlayerElectricDamage` | h  |  |
| `MR::endBindAndPlayerFireDamage` | h  |  |
| `MR::endBindAndPlayerFlip` | h , e  |  |
| `MR::endBindAndPlayerForceJump` | h , e , c  |  |
| `MR::endBindAndPlayerForceWeakGravityJump` | h , e  |  |
| `MR::endBindAndPlayerForceWeakGravityJumpInputOff` | h , e  |  |
| `MR::endBindAndPlayerFreezeDamage` | h  |  |
| `MR::endBindAndPlayerJump` | h , e , c  |  |
| `MR::endBindAndPlayerJumpWithRollLanding` | h , e , c  |  |
| `MR::endBindAndPlayerWait` | h  |  |
| `MR::endBindAndPlayerWeakGravityJump` | h , e  |  |
| `MR::endBindAndPlayerWeakGravityLimitJump` | h , e  |  |
| `MR::endBindAndSpinDriverJump` | h , e  |  |
| `MR::endDemo` | h , g  |  |
| `MR::endDemoWaitCameraInterpolating` | h , g  |  |
| `MR::endEventCamera` | h , g , b , c ? |  |
| `MR::endEventCameraAtLanding` | h , g , c  |  |
| `MR::endGalaxyCometEvent` |  |  |
| `MR::endGlobalEventCamera` | g , c , b  |  |
| `MR::endLauncherCamera` |  |  |
| `MR::endLauncherFlightCamera` |  |  |
| `MR::endMultiActorCamera` | h , h , g , b , c ? | b |
| `MR::endMultiActorCameraAtLanding` | h , h , g , c ? | b |
| `MR::endNPCTalkCamera` | b , c ? |  |
| `MR::endPlayerTalk` |  |  |
| `MR::endStartAnimCamera` |  |  |
| `MR::endStartPosCamera` |  |  |
| `MR::endSubjectiveCamera` | c endType |  |
| `MR::endTalkCamera` | b , c  |  |
| `MR::endTalkingSequence` | h  |  |
| `MR::endToCaptureScreen` | g  |  |
| `MR::epsilon` |  | d |
| `MR::escapeFromPlayer` | h , E , h  |  |
| `MR::excludeCalcShadowToActorAll` | h , h  |  |
| `MR::excludeCalcShadowToCollision` | h , g , h  |  |
| `MR::excludeCalcShadowToMyCollision` | h , g  |  |
| `MR::excludeCalcShadowToSensorAll` | h , h  |  |
| `MR::executeOnWelcomeAndRetry` |  |  |
| `MR::executeTextBoxRecursive` | h , g , h  |  |
| `MR::explainBeeMarioIfAtFirst` |  |  |
| `MR::explainEnableToSpin` | h  |  |
| `MR::explainFireMarioIfAtFirst` |  |  |
| `MR::explainFlyingMarioIfAtFirst` |  |  |
| `MR::explainHopperMarioIfAtFirst` |  |  |
| `MR::explainIceMarioIfAtFirst` |  |  |
| `MR::explainInvincibleMarioIfAtFirst` |  |  |
| `MR::explainLifeUpIfAtFirst` |  |  |
| `MR::explainOneUpIfAtFirst` |  |  |
| `MR::explainTeresaMarioIfAtFirst` |  |  |
| `MR::extractMtxTrans` | F , E  |  |
| `MR::extractMtxXDir` | F , E  |  |
| `MR::extractMtxXYZDir` | F , E , E , E  |  |
| `MR::extractMtxYDir` | F , E  |  |
| `MR::extractMtxZDir` | F , E  |  |
| `MR::fabs` | d x | d |
| `MR::faceToPoint` | F , e , d  | b |
| `MR::faceToVector:Fed` | F , e , d  | b |
| `MR::faceToVector:Ied` | I , e , d  | b |
| `MR::fillScreen` | h , b opaque? |  |
| `MR::fillScreenArea` | h , j rMin, j rMax |  |
| `MR::fillSilhouetteColor` |  |  |
| `MR::findLinkNamePos` | h , g , F  |  |
| `MR::findNamePos:gEE` | g , E , E  | b |
| `MR::findNamePos:gF` | g , F  | b |
| `MR::findNamePosOnGround` | g , F  |  |
| `MR::flattenMtx:FFe` | F , F , e  |  |
| `MR::flattenMtx:Fe` | F , e  |  |
| `MR::flyAndTurnAlongRailSearchingPlayer` | h , E , d , h , b  |  |
| `MR::followRailPose` | h , d , d  |  |
| `MR::followRailPoseOnGround:hd` | h , d  |  |
| `MR::followRailPoseOnGround:hhd` | h , h , d  |  |
| `MR::forceAppearDefaultGameLayout` |  |  |
| `MR::forceAppearStarPieceCounterForTicoFat` |  |  |
| `MR::forceBindOnGround` | h , d , d  |  |
| `MR::forceCloseSystemWipeFade` |  |  |
| `MR::forceCloseSystemWipeWhiteFade` |  |  |
| `MR::forceCloseWipeCircle` |  |  |
| `MR::forceCloseWipeFade` |  |  |
| `MR::forceCloseWipeWhiteFade` |  |  |
| `MR::forceFlyPlayer` | e , e , c  |  |
| `MR::forceJumpPlayer` | e  |  |
| `MR::forceKillPlayerByAbyss` |  |  |
| `MR::forceKillPlayerByGhostRace` |  |  |
| `MR::forceKillPlayerByGroundRace` |  |  |
| `MR::forceKillPlayerByWaterRace` |  |  |
| `MR::forceOffImageEffect` |  |  |
| `MR::forceOpenSystemWipeFade` |  |  |
| `MR::forceOpenWipeCircle` |  |  |
| `MR::forceOpenWipeFade` |  |  |
| `MR::forceOpenWipeWhiteFade` |  |  |
| `MR::forceSyncStarPieceCounter` |  |  |
| `MR::forceToBlankCinemaFrame` |  |  |
| `MR::forceToFrameCinemaFrame` |  |  |
| `MR::forceToNextStateGalaxyCometScheduler` |  |  |
| `MR::forceToScreenCinemaFrame` |  |  |
| `MR::forwardNode` | h  |  |
| `MR::forwardNodeCurrentBranchLeft` | h  |  |
| `MR::forwardNodeCurrentBranchRight` | h  |  |
| `MR::forwardNodeNextBranchLeft` | h  |  |
| `MR::forwardNodeNextBranchRight` | h  |  |
| `MR::getActorCameraFrames` | h , h  | c |
| `MR::getAnimCameraFrame:hg` | h , g  | c |
| `MR::getAnimCameraFrame:hhg` | h , h , g  | c |
| `MR::getAnimCtrl` | h , c  | h |
| `MR::getAnimFrame` | h , c  | d |
| `MR::getAnimFrameMax:hc` | h , c  | c |
| `MR::getAnimFrameMax:hg` | h , g  | c |
| `MR::getAppearPowerStarObjName` | c  | g |
| `MR::getArchive` | g pFilePath, h pArena? | h |
| `MR::getArchiveArena` | g pFilePath | h |
| `MR::getAreaObj` | g , e  | h |
| `MR::getAreaObjArg` | h , c  | c |
| `MR::getAreaObjManager` | g  | h |
| `MR::getAspect` |  | d |
| `MR::getBasename` | g pPath | g |
| `MR::getBckCtrl` | h  | h |
| `MR::getBckFrame` | h  | d |
| `MR::getBckFrameMax:h` | h  | d |
| `MR::getBckFrameMax:hg` | h , g  | c |
| `MR::getBckFrameMaxPlayer:` |  | d |
| `MR::getBckFrameMaxPlayer:g` | g  | c |
| `MR::getBckRate` | h  | d |
| `MR::getBindedFixReactionVector` | h  | e |
| `MR::getBindedHitPos` | h  | e |
| `MR::getBindedNormal` | h  | e |
| `MR::getBindedPlaneNormal` | h , c  | e |
| `MR::getBindedPlaneNum` | h  | c |
| `MR::getBindedPlaneSensor` | h , c  | h |
| `MR::getBinderRadius` | h  | d |
| `MR::getBodySensor` | h  | h |
| `MR::getBpkCtrl` | h  | h |
| `MR::getBrkCtrl` | h  | h |
| `MR::getBrkFrame` | h  | d |
| `MR::getBrkFrameMax:h` | h  | d |
| `MR::getBrkFrameMax:hg` | h , g  | c |
| `MR::getBtkCtrl` | h  | h |
| `MR::getBtkFrameMax` | h  | d |
| `MR::getBtpCtrl` | h  | h |
| `MR::getBtpFrame` | h  | d |
| `MR::getBvaCtrl` | h  | h |
| `MR::getBvaFrame` | h  | d |
| `MR::getBvaFrameMax` | h , g  | c |
| `MR::getCamPos` |  | e |
| `MR::getCamXdir` |  | e |
| `MR::getCamYdir` |  | e |
| `MR::getCamZdir` |  | e |
| `MR::getCameraCube` |  | h |
| `MR::getCameraID` | h  | c |
| `MR::getCameraInvViewMtx` |  | f |
| `MR::getCameraProjectionMtx` |  | h |
| `MR::getCameraViewMtx` |  | f |
| `MR::getCameraWatchPos` |  | e |
| `MR::getChildObjNum` | h  | c |
| `MR::getCinemaFont` |  | h |
| `MR::getCoinBestScore:g` | g  | c |
| `MR::getCoinBestScore:gc` | g , c  | c |
| `MR::getCoinNum` |  | c |
| `MR::getCollisionBoundingSphereRange` | h  | d |
| `MR::getCollisionParts` | h  | h |
| `MR::getCollisionSensorType` | h  | c |
| `MR::getCometNameFromId` | c  | g |
| `MR::getCometNameIdFromString` | g  | c |
| `MR::getConvergeVibrationValue` | d x, d start, d end, d dampScale, d rate | d |
| `MR::getCsvDataBool` | B , h , g , c  |  |
| `MR::getCsvDataColor` | h , h , g , c  |  |
| `MR::getCsvDataElementNum` | h  | c |
| `MR::getCsvDataF32` | D , h , g , c  |  |
| `MR::getCsvDataS32` | C , h , g , c  |  |
| `MR::getCsvDataU8` | C , h , g , c  |  |
| `MR::getCsvDataVec` | E , h , g , c  |  |
| `MR::getCubeLocalBox` | h  | h |
| `MR::getCurrentAstroOverlookAreaObj` |  | h |
| `MR::getCurrentDemoPartNameMain` | g  | g |
| `MR::getCurrentMarioStartIdInfo` |  | h |
| `MR::getCurrentNodePosition` | h pRailGraphIter | e |
| `MR::getCurrentPlacementZoneId` |  | c |
| `MR::getCurrentPlacementZoneName` |  | g |
| `MR::getCurrentRailPointArg0NoInit:hC` | h , C  | b |
| `MR::getCurrentRailPointArg0NoInit:hD` | h , D  | b |
| `MR::getCurrentRailPointArg0WithInit:hC` | h , C  | b |
| `MR::getCurrentRailPointArg0WithInit:hD` | h , D  | b |
| `MR::getCurrentRailPointArg1NoInit:hC` | h , C  | b |
| `MR::getCurrentRailPointArg1NoInit:hD` | h , D  | b |
| `MR::getCurrentRailPointArg1WithInit` | h , C  | b |
| `MR::getCurrentRailPointArg5NoInit` | h , C  | b |
| `MR::getCurrentRailPointArg7NoInit` | h , C  | b |
| `MR::getCurrentRailPointNo` | h  | c |
| `MR::getCurrentRushActor` |  | h |
| `MR::getCurrentRushSensor` |  | h |
| `MR::getCurrentScenarioNo` |  | c |
| `MR::getCurrentSelectedScenarioNo` |  | c |
| `MR::getCurrentStageName` |  | g |
| `MR::getCurrentStageNormalScenarioNum` |  | c |
| `MR::getCurrentStagePowerStarNum` |  | c |
| `MR::getCurrentStartCameraId` |  | c |
| `MR::getCurrentStartZoneId` |  | c |
| `MR::getCylinderRadius` | h  | d |
| `MR::getDeclareRemnantCoinCount` | h  | c |
| `MR::getDeclareRemnantStarPieceCount` | h  | c |
| `MR::getDeclareRemnantStarPieceCountNotExist` | h  | c |
| `MR::getDemoCastID` | h rIter | c |
| `MR::getDemoGroupLinkID` | h rIter | c |
| `MR::getDemoName` | h rIter | g |
| `MR::getDemoPartStep` | g  | c |
| `MR::getDemoPartTotalStep` | g  | c |
| `MR::getDemoSheetName` | h rIter | g |
| `MR::getDisplay` |  | h |
| `MR::getEaseInOutValue` | d x, d start, d end, d max | d |
| `MR::getEaseInValue` | d x, d start, d end, d max | d |
| `MR::getEaseOutValue` | d x, d start, d end, d max | d |
| `MR::getEncounterGalaxyCometNameId` | g  | c |
| `MR::getEncounterGalaxyCometPowerStarId` | g  | c |
| `MR::getEventCameraFrames` | h , g  | c |
| `MR::getFarZ` |  | d |
| `MR::getFirstPolyNormalOnLineToMap` | E , e , e , E , h  | b |
| `MR::getFirstPolyOnLineBFast` | e , e , E , h  | b |
| `MR::getFirstPolyOnLineToMap:Ehee` | E , h , e , e  | b |
| `MR::getFirstPolyOnLineToMap:Eheehh` | E , h , e , e , h , h  | b |
| `MR::getFirstPolyOnLineToMapAndMoveLimit` | E , h , e , e  | b |
| `MR::getFirstPolyOnLineToMapExceptActor` | E , h , e , e , h  | b |
| `MR::getFirstPolyOnLineToMapExceptSensor` | E , h , e , e , h  | b |
| `MR::getFirstPolyOnLineToWaterSurface:Ehee` | E , h , e , e  | b |
| `MR::getFirstPolyOnLineToWaterSurface:Eheehh` | E , h , e , e , h , h  | b |
| `MR::getFontOnCurrentLanguage` |  | h |
| `MR::getFootPoint` | e rPointA, e rPointB, e rPos, E pDst | d |
| `MR::getFovy` |  | d |
| `MR::getFullScreenBlurTexture` |  | h |
| `MR::getFurDrawManager` |  | h |
| `MR::getGameSystemObjHolder` |  | h |
| `MR::getGeneralPosNum` |  | c |
| `MR::getGroundHitPos` | h  | e |
| `MR::getGroundNormal` | h  | e |
| `MR::getGroundSensor` | h pActor | h |
| `MR::getGroupFromArray` | h  | h |
| `MR::getHashCode` | g  | c |
| `MR::getHashCodeLower` | g  | c |
| `MR::getInitializeStartIdInfo` |  | h |
| `MR::getInterpolateValue` | d t, d start, d end | d |
| `MR::getJapaneseObjectName` | g  | g |
| `MR::getJointIndex` | h , g  | c |
| `MR::getJointName` | h , c  | g |
| `MR::getJointNum` | h  | c |
| `MR::getJointPos` | h pActor, g pJointName | e |
| `MR::getJointTransX` | h  | d |
| `MR::getJointTransY` | h  | d |
| `MR::getJointTransZ` | h  | d |
| `MR::getJointTransform` | h , g  | h |
| `MR::getLayoutMessageID` | h , g , g  |  |
| `MR::getLightAmbientColor` | h  | h |
| `MR::getLightCtrl` | h  | h |
| `MR::getLightNum:h` | h  | c |
| `MR::getLightNum:hCCCC` | h , C , C , C , C  |  |
| `MR::getLinerValue` | d x, d start, d end, d max | d |
| `MR::getLinerValueFromMinMax` | d x, d min, d max, d start, d end | d |
| `MR::getLuigiLetterGalaxyName` |  | g |
| `MR::getLuigiLetterGalaxyNameForNPC` |  | g |
| `MR::getLytTexMap` | h , g , c  | h |
| `MR::getMainTilt` | E pAccel, c channel |  |
| `MR::getMapPartsArgAccelTime` | C , h  | b |
| `MR::getMapPartsArgMoveConditionType` | C , h  | b |
| `MR::getMapPartsArgMovePosture` | C , h  | b |
| `MR::getMapPartsArgMoveSpeed` | D , h  | b |
| `MR::getMapPartsArgMoveStopType` | C , h  | b |
| `MR::getMapPartsArgMoveTimeToNextPoint` | C , h  | b |
| `MR::getMapPartsArgRailGuideType` | C , h  | b |
| `MR::getMapPartsArgRailInitPosType` | C , h  | b |
| `MR::getMapPartsArgRailRotateAngle` | D , h , c  | b |
| `MR::getMapPartsArgRailRotateAxis` | C , h , c  | b |
| `MR::getMapPartsArgRailRotateSpeed` | D , h , c  | b |
| `MR::getMapPartsArgRailRotateTime` | C , h , c  | b |
| `MR::getMapPartsArgRailRotateType` | C , h , c  | b |
| `MR::getMapPartsArgRotateAccelType` | C , h  | b |
| `MR::getMapPartsArgRotateAngle` | D , h  | b |
| `MR::getMapPartsArgRotateAxis` | C , h  | b |
| `MR::getMapPartsArgRotateSpeed` | D , h  | b |
| `MR::getMapPartsArgRotateStopTime` | C , h  | b |
| `MR::getMapPartsArgRotateTime` | C , h  | b |
| `MR::getMapPartsArgRotateType` | C , h  | b |
| `MR::getMapPartsArgShadowType` | C , h  | c |
| `MR::getMapPartsArgSignMotionType` | C , h  | b |
| `MR::getMapPartsArgSpeedCalcType:Ch` | C , h  | b |
| `MR::getMapPartsArgSpeedCalcType:Chc` | C , h , c  | b |
| `MR::getMapPartsArgStopTime` | C , h  | b |
| `MR::getMapPartsObjectName:hgc` | h , g , c  |  |
| `MR::getMapPartsObjectName:hh` | h , h  |  |
| `MR::getMapPartsObjectNameIfExistShapeID` | h , h  |  |
| `MR::getMapSoundCodeFoot` | h  | c |
| `MR::getMarioShadowTex` |  | h |
| `MR::getMarioShadowTexForLoad` |  | h |
| `MR::getMarioShadowVec` |  | e |
| `MR::getMarioStartPose` | c marioNo, E pTrans, E pRotate | b |
| `MR::getMaterialName` | h , c  | g |
| `MR::getMaterialNum` | h  | c |
| `MR::getMaxAbsElement` | e rVec | d |
| `MR::getMaxAbsElementIndex` | e rVec | c |
| `MR::getMaxElement` | e rVec | d |
| `MR::getMaxElementIndex` | e rVec | c |
| `MR::getMenuFont` |  | h |
| `MR::getMessageBalloonFollowMatrix` | h  | f |
| `MR::getMessageBalloonFollowOffset` | h  | e |
| `MR::getMessageID` | h rIter | c |
| `MR::getMessageSensor` |  | h |
| `MR::getMinAbsElementIndex` | e rVec | c |
| `MR::getMirrorCameraViewMtx` |  | f |
| `MR::getMirrorModelTexMtx` |  | f |
| `MR::getModel` | h  | h |
| `MR::getModelData:g` | g  | h |
| `MR::getModelData:h` | h  | h |
| `MR::getModelResName` | h  | g |
| `MR::getModelResourceHolder` | h  | h |
| `MR::getMoveStickX` | c channel | d |
| `MR::getMoveStickY` | c channel | d |
| `MR::getMovieCurrentFrame` |  | c |
| `MR::getMovieTotalFrame` |  | c |
| `MR::getMultiActorCameraFrames` | h , h , g  | c |
| `MR::getNPCItemData` | h , c  | b |
| `MR::getNearNodeIndex` | h pGraph, e rPosition, d maxDistance, h pSelector | c |
| `MR::getNearPolyOnLineSort` | e , e , e , h  | c |
| `MR::getNearZ` |  | d |
| `MR::getNextLinkRailID` | h rIter, C  | b |
| `MR::getNextNodePosition` | h pRailGraphIter | e |
| `MR::getNextRailPointArg0NoInit` | h , D  | b |
| `MR::getNextRailPointArg0WithInit` | h , C  | b |
| `MR::getNextRailPointArg1NoInit:hC` | h , C  | b |
| `MR::getNextRailPointArg1NoInit:hD` | h , D  | b |
| `MR::getNextRailPointArg2WithInit` | h , B  | b |
| `MR::getNextRailPointNo` | h  | c |
| `MR::getNormal` | h  | e |
| `MR::getNumStarPieceGift` | c msg | c |
| `MR::getNumberFont` |  | h |
| `MR::getPairedGroupMember` | h  | h |
| `MR::getPane` | h , g  | h |
| `MR::getPaneAlpha` | h , g  | c |
| `MR::getPaneAnimCtrl` | h , g , c  | h |
| `MR::getPaneAnimFrame` | h , g , c  | d |
| `MR::getPaneAnimFrameMax` | h , g , c  | c |
| `MR::getPaneTransX` | h , g  | d |
| `MR::getPaneTransY` | h , g  | d |
| `MR::getParticleResourceHolder` |  | h |
| `MR::getPictureBookChapterAlreadyRead` |  | c |
| `MR::getPictureBookChapterCanRead` |  | c |
| `MR::getPictureFont` |  | h |
| `MR::getPlacedHiddenStarScenarioNo` | g , c  | c |
| `MR::getPlacedRailNum` | c  | c |
| `MR::getPlacedZoneId` | h  | c |
| `MR::getPlacementArg0NoInit:hB` | h rIter, B  | b |
| `MR::getPlacementArg0NoInit:hC` | h rIter, C  | b |
| `MR::getPlacementArg0NoInit:hD` | h rIter, D  | b |
| `MR::getPlacementArg0WithInit:hB` | h rIter, B  | b |
| `MR::getPlacementArg0WithInit:hC` | h rIter, C  | b |
| `MR::getPlacementArg0WithInit:hD` | h rIter, D  | b |
| `MR::getPlacementArg1NoInit:hB` | h rIter, B  | b |
| `MR::getPlacementArg1NoInit:hC` | h rIter, C  | b |
| `MR::getPlacementArg1NoInit:hD` | h rIter, D  | b |
| `MR::getPlacementArg1WithInit:hB` | h rIter, B  | b |
| `MR::getPlacementArg1WithInit:hC` | h rIter, C  | b |
| `MR::getPlacementArg1WithInit:hD` | h rIter, D  | b |
| `MR::getPlacementArg2NoInit:hB` | h rIter, B  | b |
| `MR::getPlacementArg2NoInit:hC` | h rIter, C  | b |
| `MR::getPlacementArg2NoInit:hD` | h rIter, D  | b |
| `MR::getPlacementArg2WithInit:hB` | h rIter, B  | b |
| `MR::getPlacementArg2WithInit:hC` | h rIter, C  | b |
| `MR::getPlacementArg2WithInit:hD` | h rIter, D  | b |
| `MR::getPlacementArg3NoInit:hB` | h rIter, B  | b |
| `MR::getPlacementArg3NoInit:hC` | h rIter, C  | b |
| `MR::getPlacementArg3NoInit:hD` | h rIter, D  | b |
| `MR::getPlacementArg3WithInit:hB` | h rIter, B  | b |
| `MR::getPlacementArg3WithInit:hC` | h rIter, C  | b |
| `MR::getPlacementArg3WithInit:hD` | h rIter, D  | b |
| `MR::getPlacementArg4NoInit:hB` | h rIter, B  | b |
| `MR::getPlacementArg4NoInit:hC` | h rIter, C  | b |
| `MR::getPlacementArg4NoInit:hD` | h rIter, D  | b |
| `MR::getPlacementArg4WithInit:hB` | h rIter, B  | b |
| `MR::getPlacementArg4WithInit:hC` | h rIter, C  | b |
| `MR::getPlacementArg5NoInit:hB` | h rIter, B  | b |
| `MR::getPlacementArg5NoInit:hC` | h rIter, C  | b |
| `MR::getPlacementArg5NoInit:hD` | h rIter, D  | b |
| `MR::getPlacementArg5WithInit` | h rIter, C  | b |
| `MR::getPlacementArg6NoInit:hB` | h rIter, B  | b |
| `MR::getPlacementArg6NoInit:hC` | h rIter, C  | b |
| `MR::getPlacementArg6NoInit:hD` | h rIter, D  | b |
| `MR::getPlacementArg6WithInit` | h rIter, C  | b |
| `MR::getPlacementArg7NoInit:hB` | h rIter, B  | b |
| `MR::getPlacementArg7NoInit:hC` | h rIter, C  | b |
| `MR::getPlacementArg7NoInit:hD` | h rIter, D  | b |
| `MR::getPlacementArg7WithInit:hB` | h rIter, B  | b |
| `MR::getPlacementArg7WithInit:hC` | h rIter, C  | b |
| `MR::getPlacementCameraSetID` | h rIter, C  | b |
| `MR::getPlacementClippingGroupID` | h rIter, C  | b |
| `MR::getPlacementDemoGroupID` | h rIter, C  | b |
| `MR::getPlacementFollowID` | h rIter, C  | b |
| `MR::getPlacementGravityPower` | h rIter, h pGravity |  |
| `MR::getPlacementGravityType` | h rIter, h pGravity |  |
| `MR::getPlacementGroupID` | h rIter, C  | b |
| `MR::getPlacementLinkID` | h rIter, C  | b |
| `MR::getPlacementMatrixFromRT` | h rIter, F  | b |
| `MR::getPlacementMessageID` | h rIter, C  | b |
| `MR::getPlacementRailArg0NoInit` | h rIter, C  | b |
| `MR::getPlacementRotate` | h rIter, E  | b |
| `MR::getPlacementRotateLocal` | h rIter, E  | b |
| `MR::getPlacementScale` | h rIter, E  | b |
| `MR::getPlacementShapeIdWithInit` | h rIter, C  | b |
| `MR::getPlacementTrans` | h rIter, E  | b |
| `MR::getPlacementTransLocal` | h rIter, E  | b |
| `MR::getPlacementV3f` | h rIter, g , E  | b |
| `MR::getPlacementViewGroupID` | h rIter, C  | b |
| `MR::getPlayerBaseMtx` |  | f |
| `MR::getPlayerCenterPos` |  | e |
| `MR::getPlayerCurrentBckName` |  | g |
| `MR::getPlayerDemoActor` |  | h |
| `MR::getPlayerFrontVec` | E  |  |
| `MR::getPlayerGravity` |  | e |
| `MR::getPlayerGroundNormal` |  | e |
| `MR::getPlayerGroundPos` | E  |  |
| `MR::getPlayerGroundingPolygon` |  | h |
| `MR::getPlayerHeldCamera` |  | b |
| `MR::getPlayerHeldCrouch` |  | b |
| `MR::getPlayerHeldFire` |  | b |
| `MR::getPlayerHeldJump` |  | b |
| `MR::getPlayerHitRadius` |  | d |
| `MR::getPlayerLastMove` |  | e |
| `MR::getPlayerLeft` |  | c |
| `MR::getPlayerMaxCount` |  | c |
| `MR::getPlayerMovementTimer` |  | c |
| `MR::getPlayerPos` |  | e |
| `MR::getPlayerPressedCamera` |  | b |
| `MR::getPlayerPressedCrouch` |  | b |
| `MR::getPlayerPressedFire` |  | b |
| `MR::getPlayerPressedJump` |  | b |
| `MR::getPlayerRestartIdInfo` |  | h |
| `MR::getPlayerRotate` |  | e |
| `MR::getPlayerShadowHeight` |  | d |
| `MR::getPlayerShadowRotate` |  | e |
| `MR::getPlayerSideVec` | E  |  |
| `MR::getPlayerStickX` |  | d |
| `MR::getPlayerStickY` |  | d |
| `MR::getPlayerTakePos` | E  |  |
| `MR::getPlayerThrowVec` | E  |  |
| `MR::getPlayerUpVec` | E  |  |
| `MR::getPlayerVelocity` |  | e |
| `MR::getPlayerXanimeResource` |  | h |
| `MR::getPlayingBckName` | h  | g |
| `MR::getPointerDistance` | c channel | d |
| `MR::getPointerPastCount` | c channel | c |
| `MR::getPointerPastPos` | J pPos, c idx, c channel |  |
| `MR::getPointerPos` | J pPos, c channel |  |
| `MR::getPointerPosOnScreen` | J pPos, c channel |  |
| `MR::getPowerStarLeftToDisplayCountDownPlate` |  | c |
| `MR::getPowerStarNum` |  | c |
| `MR::getPowerStarNumSucceed` | g  | c |
| `MR::getPowerStarNumToOpenGalaxy` | g  | c |
| `MR::getPurpleCoinNum` |  | c |
| `MR::getRaceBestTime` | c  | c |
| `MR::getRaceCurrentTime` |  | c |
| `MR::getRailArg0NoInit` | h , C  | b |
| `MR::getRailArg0WithInit` | h , C  | b |
| `MR::getRailArg1NoInit` | h , C  | b |
| `MR::getRailArg1WithInit` | h , C  | b |
| `MR::getRailArg2NoInit` | h , C  | b |
| `MR::getRailArg3NoInit` | h , C  | b |
| `MR::getRailArg4NoInit` | h , C  | b |
| `MR::getRailCoord` | h  | d |
| `MR::getRailCoordSpeed` | h  | d |
| `MR::getRailDirection` | h  | e |
| `MR::getRailId` | h rIter, C  | b |
| `MR::getRailPartLength` | h , c  | d |
| `MR::getRailPointArg0NoInit:hcC` | h , c , C  | b |
| `MR::getRailPointArg0NoInit:hcD` | h , c , D  | b |
| `MR::getRailPointArg0WithInit` | h , c , C  | b |
| `MR::getRailPointArg1NoInit:hcC` | h , c , C  | b |
| `MR::getRailPointArg1NoInit:hcD` | h , c , D  | b |
| `MR::getRailPointArg2WithInit:hcC` | h , c , C  | b |
| `MR::getRailPointArg2WithInit:hcD` | h , c , D  | b |
| `MR::getRailPointArg3WithInit` | h , c , D  | b |
| `MR::getRailPointArg4NoInit` | h , c , C  | b |
| `MR::getRailPointArg6NoInit` | h , c , C  | b |
| `MR::getRailPointArg7NoInit` | h , c , C  | b |
| `MR::getRailPointCoord` | h , c  | d |
| `MR::getRailPointNum` | h  | c |
| `MR::getRailPointPos0` | h rIter, E  |  |
| `MR::getRailPointPos1` | h rIter, E  |  |
| `MR::getRailPointPos2` | h rIter, E  |  |
| `MR::getRailPointPosEnd` | h  | e |
| `MR::getRailPointPosStart` | h  | e |
| `MR::getRailPos` | h  | e |
| `MR::getRailTotalLength` | h  | d |
| `MR::getRandom:` |  | d |
| `MR::getRandom:cc` | c min, c max | c |
| `MR::getRandom:dd` | d min, d max | d |
| `MR::getRandomDegree` |  | d |
| `MR::getRandomVector` | E pDst, d range |  |
| `MR::getReduceVibrationValue` | d x, d time, d base, d amplitude, d freq | d |
| `MR::getRemixMelodyNoteNum` | c  | c |
| `MR::getRenderFovy` |  | d |
| `MR::getRenderNearZ` |  | d |
| `MR::getResourceHolder` | h  | h |
| `MR::getRoofHitPos` | h  | e |
| `MR::getRoofNormal` | h  | e |
| `MR::getRoofSensor` | h pActor | h |
| `MR::getRootPane` | h  | h |
| `MR::getRotatedAxisY` | E pDst, e rSrc |  |
| `MR::getRotatedAxisZ` | E pDst, e rSrc |  |
| `MR::getScaleWithReactionValueZeroToOne` | d x, d rate, d amplitude | d |
| `MR::getSceneKindName` | c kind | g |
| `MR::getScreenAspect` |  | d |
| `MR::getScreenHeight` |  | c |
| `MR::getScreenMargin` |  | c |
| `MR::getScreenResTIMG` |  | h |
| `MR::getScreenWidth` |  | c |
| `MR::getSelectEdgeArg0` | h pRailGraphIter | c |
| `MR::getSelectEdgeArg1` | h pRailGraphIter | c |
| `MR::getSelectEdgeArg2` | h pRailGraphIter | c |
| `MR::getSelectEdgeArg3` | h pRailGraphIter | c |
| `MR::getSensor` | h pActor, c index | h |
| `MR::getSensorHost` | h pSensor | h |
| `MR::getSensorPos` | h pSensor | e |
| `MR::getSensorWithIndex` | h pActor, c index | h |
| `MR::getShadowNearProjectionLength` | h  | d |
| `MR::getShadowProjectedSensor` | h , g  | h |
| `MR::getShadowProjectionLength` | h , g  | d |
| `MR::getShadowProjectionNormal` | h , g , E  |  |
| `MR::getShadowProjectionPos` | h , g , E  |  |
| `MR::getSignHalfProbability` |  | d |
| `MR::getSortedPoly` | E , h , c  | b |
| `MR::getSphereRadius` | h  | d |
| `MR::getStageArchive` | g  | h |
| `MR::getStarPieceNum` |  | c |
| `MR::getStarPieceNumGivingToTicoGalaxy` | c  | c |
| `MR::getStarPieceNumGivingToTicoSeed` | c  | c |
| `MR::getStarPieceNumMaxGivingToTicoGalaxy` | c  | c |
| `MR::getStarPiecePlayer` |  |  |
| `MR::getStarPointerScreenPosition` | c  | j |
| `MR::getStarPointerScreenPositionOrEdge` | c  | j |
| `MR::getStarPointerScreenSpeed` | c  | d |
| `MR::getStarPointerScreenVelocity` | c  | j |
| `MR::getStarPointerViewMtx` |  | f |
| `MR::getStarPointerWorldPosUsingDepth` | c  | e |
| `MR::getStarPointerWorldVelocityDirection` | E , c  |  |
| `MR::getStartAnimCameraFrame` |  | c |
| `MR::getStartCameraIdInfoFromStartDataIndex` | h , c  |  |
| `MR::getStartPosNum` |  | c |
| `MR::getStockedStarPieceNum` |  | c |
| `MR::getStringLengthWithMessageTag` | g  | c |
| `MR::getTaken` | h pActor | h |
| `MR::getTaking` | h pActor | h |
| `MR::getTalkingActor` |  | h |
| `MR::getTexFromArc` | g , h  | h |
| `MR::getTexFromModel` | g , h  | h |
| `MR::getTextLineNumMaxRecursive` | h , g  | c |
| `MR::getTexture` | h , g  | h |
| `MR::getTextureName` | h , c  | g |
| `MR::getUpVecFromQuat` | E pOut, i rQuat |  |
| `MR::getVelocityHorizon` | h pActor | e |
| `MR::getWallCodeIndex` | h  | c |
| `MR::getWallHitPos` | h  | e |
| `MR::getWallNormal` | h  | e |
| `MR::getWallSensor` | h pActor | h |
| `MR::getWatchEdgeArg7` | h pRailGraphIter | c |
| `MR::getWaterAreaInfo` | h pInfo, e rPos, e rGravity, b skipConnectedArea | b |
| `MR::getWaterAreaObj` | h , e  | b |
| `MR::getZoneNameFromZoneId` | c  | g |
| `MR::getZoneNum` |  | c |
| `MR::getZonePlacementMtx:c` | c  | f |
| `MR::getZonePlacementMtx:h` | h  | f |
| `MR::hasGrandStar` | c  | b |
| `MR::hasME` |  | b |
| `MR::hasMapPartsMoveStartSignMotion` | c  | b |
| `MR::hasMapPartsShadow` | c  | b |
| `MR::hasMapPartsVanishSignMotion` | c  | b |
| `MR::hasPowerStarAtResultSequence` |  | b |
| `MR::hasPowerStarInCurrentStage` | c  | b |
| `MR::hasPowerStarInCurrentStageWithDeclarer` | g , c  | b |
| `MR::hasRetryGalaxySequence` |  | b |
| `MR::hasStartAnimCamera` |  | b |
| `MR::hasStringSpace` | g pStr | b |
| `MR::hideLayout` | h  |  |
| `MR::hideModel` | h  |  |
| `MR::hideModelAndOnCalcAnim` | h  |  |
| `MR::hideModelAndOnCalcAnimIfShown` | h  |  |
| `MR::hideModelIfShown` | h  |  |
| `MR::hidePane` | h , g  |  |
| `MR::hidePaneRecursive:h` | h  |  |
| `MR::hidePaneRecursive:hg` | h , g  |  |
| `MR::hidePlayer` |  |  |
| `MR::hidePlayerJoint` | g  |  |
| `MR::hideScreen` | h  |  |
| `MR::holdClosedSceneWipeOnSystemWipe` |  |  |
| `MR::holdSceneInitialize` |  |  |
| `MR::hopCoin` | h , e , e  |  |
| `MR::hopStarPiece` | h , e , e  |  |
| `MR::inMessageArea` | h  | b |
| `MR::incCoin` | c  |  |
| `MR::incPlayerGameOverNum` |  |  |
| `MR::incPlayerLeft` |  |  |
| `MR::incPlayerLife` | c  |  |
| `MR::incPlayerOxygen` | c  |  |
| `MR::incPurpleCoin` |  |  |
| `MR::initActorCameraProgrammable` | h  |  |
| `MR::initActorLightInfoDrawBuffer` | h , h  |  |
| `MR::initActorLightInfoLightType` | h , c  |  |
| `MR::initActorState` | h pExecutor, h pActorState, h pNerve |  |
| `MR::initActorStateKeeper` | h pExecutor |  |
| `MR::initAndSetRailClipping` | E , h , d , d  |  |
| `MR::initAnimCamera:hhg` | h , h , g  |  |
| `MR::initAnimCamera:hhgg` | h , h , g , g  |  |
| `MR::initChildObj` | h , h , c  |  |
| `MR::initCollisionParts` | h , g , h , F  |  |
| `MR::initCollisionPartsAutoEqualScale` | h , g , h , F  |  |
| `MR::initCollisionPartsAutoEqualScaleOne` | h , g , h , F  |  |
| `MR::initCollisionPartsFromResourceHolder` | h , g , h , h , F  |  |
| `MR::initDefaultPos` | h , h  |  |
| `MR::initDefaultPosAndQuat` | h , h  |  |
| `MR::initDefaultPosNoRepeat` | h , h  |  |
| `MR::initDefaultPose` | h , h  |  |
| `MR::initDemoSheetTalkAnim` | h , h , g pDemoName, g pFileBaseName, h  |  |
| `MR::initDemoSheetTalkAnimWithMessageName` | h , h , g , g , g  |  |
| `MR::initEffectAfterPlacement` | h  |  |
| `MR::initFur` | h  |  |
| `MR::initFurPlanet` | h  |  |
| `MR::initFurPlayer` | h  | h |
| `MR::initJointTransform` | h  |  |
| `MR::initLightCtrl` | h  |  |
| `MR::initLightCtrlForPlayer` | h  |  |
| `MR::initLightCtrlNoDrawEnemy` | h  |  |
| `MR::initLightCtrlNoDrawMapObj` | h  |  |
| `MR::initMapPartsClipping` | h , h , E , b  |  |
| `MR::initMapPartsShadow` | h , h  |  |
| `MR::initMaterialColor0` | h , g , h  |  |
| `MR::initMaterialFog` | h , b  |  |
| `MR::initMaterialProjmapEffectMtxSetter` | h  | h |
| `MR::initMaterialTexMtx` | h , g  | h |
| `MR::initMirrorReflection` | h  |  |
| `MR::initMultiActorCameraNoInit` | h , h , g  | b |
| `MR::initMultiFur` | h , c  | h |
| `MR::initPlayerAfterOpeningDemo` |  |  |
| `MR::initSceneMessage` |  |  |
| `MR::initShadowController` | h , c  |  |
| `MR::initShadowFromCSV` | h , g  |  |
| `MR::initShadowSurfaceCircle` | h , d  |  |
| `MR::initShadowVolumeBox:he` | h , e  |  |
| `MR::initShadowVolumeBox:heF` | h , e , F  |  |
| `MR::initShadowVolumeCylinder` | h , d  |  |
| `MR::initShadowVolumeFlatModel:hg` | h , g  |  |
| `MR::initShadowVolumeFlatModel:hgF` | h , g , F  |  |
| `MR::initShadowVolumeMaterial` | h rMaterial, h rColor |  |
| `MR::initShadowVolumeOval` | h , e  |  |
| `MR::initShadowVolumeSphere` | h , d  |  |
| `MR::initStarPieceGetCSSound` |  |  |
| `MR::initStarPointerGameScene` |  |  |
| `MR::initStarPointerTarget` | h , d , e rOffset? |  |
| `MR::initStarPointerTargetAtJoint` | h , g , d , e  |  |
| `MR::initStarPointerTargetAtMtx` | h , d , F , e  |  |
| `MR::initStarPointerTargetAtPos` | h , d , e , e  |  |
| `MR::initTalkAnimDemoCast` | h , h , g pDemoName, g pFileBaseName |  |
| `MR::internName` | g pName | g |
| `MR::invalidateClipping` | h  |  |
| `MR::invalidateExCollisionParts` | h  |  |
| `MR::invalidateHitSensor` | h pActor, g pName |  |
| `MR::invalidateHitSensors` | h pActor |  |
| `MR::invalidateJointCallback` | h  |  |
| `MR::invalidateLodCtrl` | h  |  |
| `MR::invalidateMtxCalc` | h  |  |
| `MR::invalidateParentAnim` | h  |  |
| `MR::invalidateShadow` | h , g  |  |
| `MR::invalidateShadowAll` | h  |  |
| `MR::invalidateShadowGroup` | h , g  |  |
| `MR::isActionContinuous` | h  | b |
| `MR::isActionEnd` | h  | b |
| `MR::isActionLoopedOrStopped` | h  | b |
| `MR::isActionStart` | h , g  | b |
| `MR::isActiveDefaultGameLayout` |  | b |
| `MR::isActiveLauncherCamera` |  | b |
| `MR::isActiveLauncherFlightCamera` |  | b |
| `MR::isActiveLuigiHideAndSeekEvent` |  | b |
| `MR::isActiveMoviePlayer` |  | b |
| `MR::isActiveStarPointerGuidance` |  | b |
| `MR::isActorOnPlayer` | h  | b |
| `MR::isAlreadyVisitedCurrentStageAndScenario` |  | b |
| `MR::isAlreadyVisitedStage` | g  | b |
| `MR::isAngleBetween` | d angle, d min, d max | b |
| `MR::isAnimCameraEnd:hg` | h , g  | b |
| `MR::isAnimCameraEnd:hhg` | h , h , g  | b |
| `MR::isAnimStopped` | h , c  | b |
| `MR::isAnyAnimOneTimeAndStopped` | h , g  | b |
| `MR::isAnyAnimStopped` | h , g  | b |
| `MR::isAnyPlayerLeftSupply` |  | b |
| `MR::isAppearGalaxy` | g  | b |
| `MR::isBckExist` | h , g  | b |
| `MR::isBckLooped` | h  | b |
| `MR::isBckOneTimeAndStopped` | h  | b |
| `MR::isBckOneTimeAndStoppedPlayer` |  | b |
| `MR::isBckStopped` | h  | b |
| `MR::isBckStoppedPlayer` |  | b |
| `MR::isBeginScenarioStarter` |  | b |
| `MR::isBinded:h` | h  | b |
| `MR::isBinded:hh` | h , h  | b |
| `MR::isBindedDamageElectric` | h  | b |
| `MR::isBindedDamageFire` | h  | b |
| `MR::isBindedGround:h` | h  | b |
| `MR::isBindedGround:hh` | h , h  | b |
| `MR::isBindedGroundAreaMove` | h  | b |
| `MR::isBindedGroundBrake` | h  | b |
| `MR::isBindedGroundDamageFire` | h  | b |
| `MR::isBindedGroundIce` | h  | b |
| `MR::isBindedGroundRailMove` | h  | b |
| `MR::isBindedGroundSand` | h  | b |
| `MR::isBindedGroundSinkDeath` | h  | b |
| `MR::isBindedGroundWater` | h  | b |
| `MR::isBindedGroundWaterBottomH` | h  | b |
| `MR::isBindedGroundWaterBottomM` | h  | b |
| `MR::isBindedRoof:h` | h  | b |
| `MR::isBindedRoof:hh` | h , h  | b |
| `MR::isBindedWall:h` | h  | b |
| `MR::isBindedWall:hh` | h , h  | b |
| `MR::isBindedWallFront` | h , e , d  | b |
| `MR::isBindedWallOfMap` | h  | b |
| `MR::isBindedWallOfMoveLimit` | h  | b |
| `MR::isBindedWallOrSlopeGround` | h , d , E  | b |
| `MR::isBodySensorTypeMapObj` | h  | b |
| `MR::isBpkExist` | h , g  | b |
| `MR::isBpkPlaying` | h , g  | b |
| `MR::isBpkStopped` | h  | b |
| `MR::isBrkExist` | h , g  | b |
| `MR::isBrkOneTimeAndStopped` | h  | b |
| `MR::isBrkPlaying` | h , g  | b |
| `MR::isBrkStopped` | h  | b |
| `MR::isBtkExist` | h , g  | b |
| `MR::isBtkPlaying` | h , g  | b |
| `MR::isBtkStopped` | h  | b |
| `MR::isBtpExist` | h , g  | b |
| `MR::isBtpPlaying` | h , g  | b |
| `MR::isBtpStopped` | h  | b |
| `MR::isButlerMapAppear` |  | b |
| `MR::isBvaExist` | h , g  | b |
| `MR::isBvaPlaying` | h , g  | b |
| `MR::isBvaStopped` | h  | b |
| `MR::isCalcGravity` | h  | b |
| `MR::isCameraCodeThrough` | h  | b |
| `MR::isCameraControlNG` |  | b |
| `MR::isCameraInWater` |  | b |
| `MR::isCameraInterpolatingNearlyEnd` |  | b |
| `MR::isCameraPossibleToRoundLeft` |  | b |
| `MR::isCameraPossibleToRoundRight` |  | b |
| `MR::isCeilingPolygon` | d  | b |
| `MR::isClipped` | h  | b |
| `MR::isClockwiseToDir` | h , e , e  | b |
| `MR::isClockwiseToPlayer` | h , e  | b |
| `MR::isCodeSand` | h  | b |
| `MR::isConnectedInput` | c channel | b |
| `MR::isConnectedWithRail` | h rIter | b |
| `MR::isCubeBgmChangeInvalid` |  | b |
| `MR::isCurrentScene` | c  | b |
| `MR::isDeadInformationMessage` |  | b |
| `MR::isDemoActive:` |  | b |
| `MR::isDemoActive:g` | g  | b |
| `MR::isDemoActiveRegistered` | h  | b |
| `MR::isDemoCast` | h , g  | b |
| `MR::isDemoExist` | g  | b |
| `MR::isDemoLastStep` |  | b |
| `MR::isDemoPartActive` | g  | b |
| `MR::isDemoPartExist` | h , g  | b |
| `MR::isDemoPartFirstStep` | g  | b |
| `MR::isDemoPartGreaterStep` | g , c  | b |
| `MR::isDemoPartLastStep` | g  | b |
| `MR::isDemoPartLessEqualStep` | g , c  | b |
| `MR::isDemoPartStep` | g , c  | b |
| `MR::isDemoPartTalk` | g  | b |
| `MR::isDigitStringTail` | g pStr, c digitNum | b |
| `MR::isEffectValid` | h , g  | b |
| `MR::isEnableStarPointerShootStarPiece` | c  | b |
| `MR::isEndLuigiHideAndSeekEvent` |  | b |
| `MR::isEndPowerStarAppearDemo` | h  | b |
| `MR::isEqualObjectName` | h rIter, g  | b |
| `MR::isEqualRailUsage` | h rIter, g  | b |
| `MR::isEqualStageName` | g  | b |
| `MR::isEqualString` | g pStr1, g pStr2 | b |
| `MR::isEqualStringCase` | g pStr1, g pStr2 | b |
| `MR::isEqualStringPrefix` | g pStr, g pPrefix | b |
| `MR::isEqualSubString` | g pStr, g pSubStr | b |
| `MR::isEventCameraActive:` |  | b |
| `MR::isEventCameraActive:hg` | h , g  | b |
| `MR::isExecScenarioStarter` |  | b |
| `MR::isExecuteCalcAnimLayout` | h  | b |
| `MR::isExecuteDrawLayout` | h  | b |
| `MR::isExistActorCamera` | h  | b |
| `MR::isExistAnim` | h , g  | b |
| `MR::isExistBck` | h , g  | b |
| `MR::isExistBinder` | h  | b |
| `MR::isExistBpk` | h , g  | b |
| `MR::isExistBrk` | h , g  | b |
| `MR::isExistBtk` | h , g  | b |
| `MR::isExistBtp` | h , g  | b |
| `MR::isExistBva` | h , g  | b |
| `MR::isExistCollisionParts` | h  | b |
| `MR::isExistCollisionResource` | h , g  | b |
| `MR::isExistGameMessage` | g  | b |
| `MR::isExistInAttributeGroupReflectSpinningBox` | h  | b |
| `MR::isExistInAttributeGroupSearchTurtle` | h  | b |
| `MR::isExistIndirectTexture` | h  | b |
| `MR::isExistJoint` | h , g  | b |
| `MR::isExistKcl` | h , g  | b |
| `MR::isExistMapCollision` | e , e  | b |
| `MR::isExistMapCollisionExceptActor` | e , e , h  | b |
| `MR::isExistMario` |  | b |
| `MR::isExistMirrorCamera` |  | b |
| `MR::isExistModel` | g  | b |
| `MR::isExistMoveLimitCollision` | e , e  | b |
| `MR::isExistNextNode` | h  | b |
| `MR::isExistObjectOrLayoutArchive` | g pName | b |
| `MR::isExistPaneCtrl` | h , g  | b |
| `MR::isExistPlacementArg` | h rIter | b |
| `MR::isExistRail` | h  | b |
| `MR::isExistResourceInArc` | g , g  | b |
| `MR::isExistShadow` | h , g  | b |
| `MR::isExistStageSwitchA` | h rIter | b |
| `MR::isExistStageSwitchAppear` | h rIter | b |
| `MR::isExistStageSwitchB` | h rIter | b |
| `MR::isExistStageSwitchDead` | h rIter | b |
| `MR::isExistStageSwitchSleep` | h rIter | b |
| `MR::isExistStarPointerGuidance` |  | b |
| `MR::isExistStarPointerGuidanceFrame1P` |  | b |
| `MR::isExistStarPointerTarget` | h  | b |
| `MR::isExistSubModel` | g , g  | b |
| `MR::isExistTexture` | h , g  | b |
| `MR::isFaceToPlayerDegree:hd` | h , d  | b |
| `MR::isFaceToPlayerDegree:hed` | h , e , d  | b |
| `MR::isFaceToPlayerDegreeHV` | h , e , d , d  | b |
| `MR::isFaceToPlayerHorizontalDegree` | h , e , d  | b |
| `MR::isFaceToTargetDegree:hed` | h , e , d  | b |
| `MR::isFaceToTargetDegree:heed` | h , e , e , d  | b |
| `MR::isFaceToTargetHorizontalDegree` | h , e , e , d  | b |
| `MR::isFallNextMove:eeedddh` | e , e , e , d , d , d , h  | b |
| `MR::isFallNextMove:hdddh` | h , d , d , d , h  | b |
| `MR::isFallOrDangerNextMove:eeeddd` | e , e , e , d , d , d  | b |
| `MR::isFallOrDangerNextMove:hddd` | h , d , d , d  | b |
| `MR::isFileExist` | g pFilePath, b considerLanguage | b |
| `MR::isFirstPersonCamera` |  | b |
| `MR::isFloorPolygon:d` | d  | b |
| `MR::isFloorPolygon:ee` | e , e  | b |
| `MR::isFloorPolygonCos` | e , e , d  | b |
| `MR::isFpViewChangingFailure` |  | b |
| `MR::isGalaxyAnyCometAppearInCurrentStage` |  | b |
| `MR::isGalaxyAppearGreenDriver` | g  | b |
| `MR::isGalaxyBlackCometAppearInCurrentStage` |  | b |
| `MR::isGalaxyCometLandInStage` | g  | b |
| `MR::isGalaxyDarkCometAppearInCurrentStage` |  | b |
| `MR::isGalaxyGhostCometAppearInCurrentStage` |  | b |
| `MR::isGalaxyPurpleCometLaunch` |  | b |
| `MR::isGalaxyQuickCometAppearInCurrentStage` |  | b |
| `MR::isGalaxyRedCometAppearInCurrentStage` |  | b |
| `MR::isGrandStarInCurrentStage` | c  | b |
| `MR::isGrandStarInCurrentStageWithDeclarer` | g , c  | b |
| `MR::isGroundCodeAreaMove` | h  | b |
| `MR::isGroundCodeBrake` | h  | b |
| `MR::isGroundCodeDamage` | h  | b |
| `MR::isGroundCodeDamageElectric` | h  | b |
| `MR::isGroundCodeDamageFire` | h  | b |
| `MR::isGroundCodeDeath` | h  | b |
| `MR::isGroundCodeFireDance` | h  | b |
| `MR::isGroundCodeIce` | h  | b |
| `MR::isGroundCodeNoStampSand` | h  | b |
| `MR::isGroundCodeRailMove` | h  | b |
| `MR::isGroundCodeSand` | h  | b |
| `MR::isGroundCodeSinkDeath` | h  | b |
| `MR::isGroundCodeSinkDeathMud` | h  | b |
| `MR::isGroundCodeWaterBottomH` | h  | b |
| `MR::isGroundCodeWaterBottomM` | h  | b |
| `MR::isGroundCodeWaterIter` | h  | b |
| `MR::isHalfProbability` |  | b |
| `MR::isHeadString` | g pStr, g pHead | b |
| `MR::isHeldAnyWithoutHome` | c channel | b |
| `MR::isHeldCamera` | c channel | b |
| `MR::isHeldCrouch` | c channel | b |
| `MR::isHeldDown` | c channel | b |
| `MR::isHeldFire` | c channel | b |
| `MR::isHeldJump` | c channel | b |
| `MR::isHeldLeft` | c channel | b |
| `MR::isHeldMap` | c channel | b |
| `MR::isHeldPause` | c channel | b |
| `MR::isHeldRight` | c channel | b |
| `MR::isHeldUp` | c channel | b |
| `MR::isHiddenLayout` | h  | b |
| `MR::isHiddenModel` | h  | b |
| `MR::isHiddenPane` | h , g  | b |
| `MR::isInAreaObj` | g , e  | b |
| `MR::isInDarkMatter` | e  | b |
| `MR::isInDeath:e` | e  | b |
| `MR::isInDeath:he` | h , e  | b |
| `MR::isInRange` | d x, d min, d max | b |
| `MR::isInSightConePlayer` | h , e , d , d  | b |
| `MR::isInSightFanPlayer` | h , e , d , d , d  | b |
| `MR::isInSpinStormRange` | c msg, h pSender, h pReceiver, d range | b |
| `MR::isInWater:e` | e  | b |
| `MR::isInWater:he` | h , e  | b |
| `MR::isInitializeStateEnd` |  | b |
| `MR::isInitializeStatePlacementSomething` |  | b |
| `MR::isInvalidClipping` | h  | b |
| `MR::isJointUnder` | h , h  | b |
| `MR::isJudgedToClipFrustum` | e , d  | b |
| `MR::isJudgedToClipFrustum100m` | e , d  | b |
| `MR::isJudgedToClipFrustum300m` | e , d  | b |
| `MR::isJudgedToNearClip` | e , d  | b |
| `MR::isKinopioExplorerCompleteTrickComet` |  | b |
| `MR::isKinopioExplorerOrganize` |  | b |
| `MR::isKinopioExplorerRescued` |  | b |
| `MR::isKinopioExplorerStartMessenger` |  | b |
| `MR::isKinopioExplorerTalkGetGrandStar2` |  | b |
| `MR::isKinopioExplorerTalkGetGrandStar3` |  | b |
| `MR::isKinopioExplorerTalkGetGrandStar4` |  | b |
| `MR::isKinopioExplorerTalkGetGrandStar5` |  | b |
| `MR::isKinopioExplorerTalkGetGrandStar6` |  | b |
| `MR::isKinopioExplorerTalkGoFinalBattle` |  | b |
| `MR::isKinopioExplorerTrickComet` |  | b |
| `MR::isKoopaFortressAppearInGalaxy` |  | b |
| `MR::isLightGravity` | h rInfo | b |
| `MR::isLoopRail` | h  | b |
| `MR::isLoopRailPathIter` | h rIter | b |
| `MR::isLuigiDisappearFromAstroGalaxy` |  | b |
| `MR::isLuigiDisappearFromAstroGalaxyOrHiding` |  | b |
| `MR::isLuigiHidingGalaxyAndScenario` | g , c  | b |
| `MR::isLuigiLeftSupply` |  | b |
| `MR::isLuigiLetterArrivalAtMessenger` |  | b |
| `MR::isMainSpin` | c channel | b |
| `MR::isMainSpinPressed` | c channel | b |
| `MR::isMapPartsPressOn` | h  | b |
| `MR::isMapPartsRailInitPosTypePoint0` | c  | b |
| `MR::isMapPartsRailInitPosTypeRailPoint` | c  | b |
| `MR::isMapPartsRailInitPosTypeRailPos` | c  | b |
| `MR::isMapPartsRailMovePassedEndPointRepeat` | h  | b |
| `MR::isMapPartsRailMovePassedStartPointRepeat` | h  | b |
| `MR::isMapPartsRailSpeedCalcTypeTime` | c  | b |
| `MR::isMapPartsShadowTypeNone` | c  | b |
| `MR::isMapPartsSignMotionTypeMoveStart` | c  | b |
| `MR::isMapPartsSignMotionTypeMoveWait` | c  | b |
| `MR::isMessageEditorNextTag` | g  | b |
| `MR::isMissLayoutAnimEnd` |  | b |
| `MR::isMoveStartTypePlayerOnStopEnd` | c  | b |
| `MR::isMoveStartTypeUnconditional` | c  | b |
| `MR::isMoveStickOperated` | c channel | b |
| `MR::isMoveStickPressedDown` | c channel | b |
| `MR::isMoveStickPressedLeft` | c channel | b |
| `MR::isMoveStickPressedRight` | c channel | b |
| `MR::isMoveStickPressedUp` | c channel | b |
| `MR::isMoviePlayerPlaying` |  | b |
| `MR::isMsgAutoRushBegin` | c msg | b |
| `MR::isMsgBallDashGround` | c msg | b |
| `MR::isMsgBallDashWall` | c msg | b |
| `MR::isMsgCeilTouch` | c msg | b |
| `MR::isMsgEnemyAttack` | c msg | b |
| `MR::isMsgEnemyAttackElectric` | c msg | b |
| `MR::isMsgEnemyAttackFire` | c msg | b |
| `MR::isMsgEnemyAttackFireStrong` | c msg | b |
| `MR::isMsgExplosionAttack` | c msg | b |
| `MR::isMsgFireBallAttack` | c msg | b |
| `MR::isMsgFloorTouch` | c msg | b |
| `MR::isMsgFreezeAttack` | c msg | b |
| `MR::isMsgHitmarkEmit` | c msg | b |
| `MR::isMsgInhaleBlackHole` | c msg | b |
| `MR::isMsgInvalidHit` | c msg | b |
| `MR::isMsgInvincibleAttack` | c msg | b |
| `MR::isMsgIsRushTakeOver` | c msg | b |
| `MR::isMsgItemEndMove` | c msg | b |
| `MR::isMsgItemGet` | c msg | b |
| `MR::isMsgItemHide` | c msg | b |
| `MR::isMsgItemPull` | c msg | b |
| `MR::isMsgItemShow` | c msg | b |
| `MR::isMsgItemStartMove` | c msg | b |
| `MR::isMsgJetTurtleAttack` | c msg | b |
| `MR::isMsgLockOnStarPieceShoot` | c msg | b |
| `MR::isMsgPlayerHipDrop` | c msg | b |
| `MR::isMsgPlayerHipDropFloor` | c msg | b |
| `MR::isMsgPlayerHitAll` | c msg | b |
| `MR::isMsgPlayerKick` | c msg | b |
| `MR::isMsgPlayerSpinAttack` | c msg | b |
| `MR::isMsgPlayerTrample` | c msg | b |
| `MR::isMsgPlayerUpperPunch` | c msg | b |
| `MR::isMsgRaceReady` | c msg | b |
| `MR::isMsgRaceReset` | c msg | b |
| `MR::isMsgRaceStart` | c msg | b |
| `MR::isMsgRushBegin` | c msg | b |
| `MR::isMsgRushCancel` | c msg | b |
| `MR::isMsgSearchlightAttack` | c msg | b |
| `MR::isMsgSpinStormRange` | c msg | b |
| `MR::isMsgStarPieceAttack` | c msg | b |
| `MR::isMsgStarPieceGift` | c msg | b |
| `MR::isMsgStarPieceReflect` | c msg | b |
| `MR::isMsgStartPowerStarGet` | c msg | b |
| `MR::isMsgToEnemyAttackBlow` | c msg | b |
| `MR::isMsgToEnemyAttackShockWave` | c msg | b |
| `MR::isMsgToEnemyAttackTrample` | c msg | b |
| `MR::isMsgTouchPlantItem` | c msg | b |
| `MR::isMsgTutorialNext` | c msg | b |
| `MR::isMsgTutorialOmit` | c msg | b |
| `MR::isMsgTutorialPass` | c msg | b |
| `MR::isMsgTutorialPrev` | c msg | b |
| `MR::isMsgTutorialStart` | c msg | b |
| `MR::isMsgUpdateBaseMtx` | c msg | b |
| `MR::isMsgWallTouch` | c msg | b |
| `MR::isNPCItemFileExist` | g  | b |
| `MR::isName` | h , g  | b |
| `MR::isNan` | e rVec | b |
| `MR::isNearAngleDegree` | e rA, e rB, d angle | b |
| `MR::isNearAngleDegreeHV` | e rA, e rB, e rAxis, d angleH, d angleV | b |
| `MR::isNearAngleRadian` | e rA, e rB, d angle | b |
| `MR::isNearAngleRadianHV` | e rA, e rB, e rAxis, d angleH, d angleV | b |
| `MR::isNearPlayerAnyTime` | h , d  | b |
| `MR::isNearPlayerHorizontal` | h , d  | b |
| `MR::isNearPlayerPose` | h , d , d  | b |
| `MR::isNearZero:dd` | d x, d tolerance? | b |
| `MR::isNearZero:ed` | e rVec, d tolerance? | b |
| `MR::isNearZero:jd` | j rVec, d tolerance? | b |
| `MR::isNoBind` | h  | b |
| `MR::isNoCalcAnim` | h  | b |
| `MR::isNoCalcView` | h  | b |
| `MR::isNoEntryDrawBuffer` | h  | b |
| `MR::isNormalBloomForEachModel` |  | b |
| `MR::isNormalBloomOn` |  | b |
| `MR::isNormalTalking` |  | b |
| `MR::isNormalTexMtx` | h  | b |
| `MR::isNormalize` | e rVec, d tolerance? | b |
| `MR::isNullOrEmptyString` | g pStr | b |
| `MR::isObjectName` | h rIter, g pName | b |
| `MR::isOffPlayerControl` |  | b |
| `MR::isOnGameEventFlagAstroGalaxyBgmBright` |  | b |
| `MR::isOnGameEventFlagBeeMarioAtFirst` |  | b |
| `MR::isOnGameEventFlagChildsRoom` |  | b |
| `MR::isOnGameEventFlagEndButlerDomeLecture` |  | b |
| `MR::isOnGameEventFlagEndButlerGalaxyMoveLecture` |  | b |
| `MR::isOnGameEventFlagEndButlerStarPieceLecture` |  | b |
| `MR::isOnGameEventFlagEndTicoGuideDemo` |  | b |
| `MR::isOnGameEventFlagFireMarioAtFirst` |  | b |
| `MR::isOnGameEventFlagFlyingMarioAtFirst` |  | b |
| `MR::isOnGameEventFlagGalaxyOpen` | g  | b |
| `MR::isOnGameEventFlagGreenDriver` |  | b |
| `MR::isOnGameEventFlagHopperMarioAtFirst` |  | b |
| `MR::isOnGameEventFlagIceMarioAtFirst` |  | b |
| `MR::isOnGameEventFlagLibraryRoom` |  | b |
| `MR::isOnGameEventFlagLuigiRescued` |  | b |
| `MR::isOnGameEventFlagOffAstroDomeGuidance` |  | b |
| `MR::isOnGameEventFlagPlayMoviePrologueA` |  | b |
| `MR::isOnGameEventFlagPowerStarSuccess` | g , c  | b |
| `MR::isOnGameEventFlagRedDriver` |  | b |
| `MR::isOnGameEventFlagRosettaTalkAboutTicoInTower` |  | b |
| `MR::isOnGameEventFlagSurfingTutorialAtFirst` |  | b |
| `MR::isOnGameEventFlagTamakoroTutorialAtFirst` |  | b |
| `MR::isOnGameEventFlagTeresaMarioAtFirst` |  | b |
| `MR::isOnGameEventFlagUseAstroDome` |  | b |
| `MR::isOnGameEventFlagViewCompleteEnding` |  | b |
| `MR::isOnGameEventFlagViewCompleteEndingMarioAndLuigi` |  | b |
| `MR::isOnGameEventFlagViewNormalEnding` |  | b |
| `MR::isOnGround` | h  | b |
| `MR::isOnGroundCos` | h , d  | b |
| `MR::isOnGroundPlayer` |  | b |
| `MR::isOnLuigiHiding` |  | b |
| `MR::isOnLuigiHidingCurrentStage` |  | b |
| `MR::isOnMessageAlreadyRead` | c  | b |
| `MR::isOnPlayerShadow` | h  | b |
| `MR::isOnSwitchA` | h  | b |
| `MR::isOnSwitchAppear` | h  | b |
| `MR::isOnSwitchB` | h  | b |
| `MR::isOnTicoGalaxyAlreadyTalk` | c  | b |
| `MR::isOnWarpPodPathFlag` | c  | b |
| `MR::isOppositeDirection` | e rVec1, e rVec2, d tolerance? | b |
| `MR::isOppositeDirectionRadian` | d angle, d target | b |
| `MR::isPaneAnimStopped` | h , g , c  | b |
| `MR::isPermitSE` |  | b |
| `MR::isPlacedCoin` | g , c  | b |
| `MR::isPlacementLocalStage` |  | b |
| `MR::isPlayerCarryAny` |  | b |
| `MR::isPlayerConfrontDeath` |  | b |
| `MR::isPlayerDamaging` |  | b |
| `MR::isPlayerDead` |  | b |
| `MR::isPlayerDisableFpView` |  | b |
| `MR::isPlayerElementMode` | c  | b |
| `MR::isPlayerElementModeBee` |  | b |
| `MR::isPlayerElementModeHopper` |  | b |
| `MR::isPlayerElementModeIce` |  | b |
| `MR::isPlayerElementModeInvincible` |  | b |
| `MR::isPlayerElementModeNormal` |  | b |
| `MR::isPlayerElementModeTeresa` |  | b |
| `MR::isPlayerElementModeTornado` |  | b |
| `MR::isPlayerExistDown` | h , d , d  | b |
| `MR::isPlayerExistSide` | h , d , d  | b |
| `MR::isPlayerExistUp` | h , d , d  | b |
| `MR::isPlayerFlying` |  | b |
| `MR::isPlayerHidden` |  | b |
| `MR::isPlayerHipDropFalling` |  | b |
| `MR::isPlayerHipDropLand` |  | b |
| `MR::isPlayerInAreaObj` | g  | b |
| `MR::isPlayerInBind` |  | b |
| `MR::isPlayerInRush` |  | b |
| `MR::isPlayerInWaterMode` |  | b |
| `MR::isPlayerJumpRising` |  | b |
| `MR::isPlayerLeftSide` | h  | b |
| `MR::isPlayerLeftSupply` |  | b |
| `MR::isPlayerLeftSupplyByMissAndGameOver` |  | b |
| `MR::isPlayerLuigi` |  | b |
| `MR::isPlayerNeedBrakingCamera` |  | b |
| `MR::isPlayerOnPress` |  | b |
| `MR::isPlayerOnWaterSurface` |  | b |
| `MR::isPlayerParalyzing` |  | b |
| `MR::isPlayerPointedBy2POnTriggerButton` |  | b |
| `MR::isPlayerRefuseTalk` |  | b |
| `MR::isPlayerSkating` |  | b |
| `MR::isPlayerSleeping` |  | b |
| `MR::isPlayerSquat` |  | b |
| `MR::isPlayerStaggering` |  | b |
| `MR::isPlayerSwimming` |  | b |
| `MR::isPlayerSwingAction` |  | b |
| `MR::isPlayerTakingActor` | g  | b |
| `MR::isPlayerTeresaDisappear` |  | b |
| `MR::isPlayingStageBgm` |  | b |
| `MR::isPlayingStageBgmID` | c  | b |
| `MR::isPlayingStageBgmName` | g  | b |
| `MR::isPlayingSubBgmID` | c  | b |
| `MR::isPointerInScreen` | c channel | b |
| `MR::isPossibleToShiftToFirstPersonCamera` |  | b |
| `MR::isPowerStarGetDemoActive` |  | b |
| `MR::isPowerStarGetDemoWithLuigiCurrentGalaxy` |  | b |
| `MR::isPowerStarGetDemoWithLuigiCurrentGalaxyAndScenario` | c  | b |
| `MR::isPowerStarGreen` | g , c  | b |
| `MR::isPowerStarGreenAtResultSequence` |  | b |
| `MR::isPowerStarGreenInCurrentStage` | c  | b |
| `MR::isPowerStarGreenInCurrentStageWithDeclarer` | g , c  | b |
| `MR::isPowerStarRedAtResultSequence` |  | b |
| `MR::isPowerStarRedInCurrentStage` | c  | b |
| `MR::isPowerStarRedInCurrentStageWithDeclarer` | g , c  | b |
| `MR::isPreparedStageBgm` |  | b |
| `MR::isPressedCamera` | c channel | b |
| `MR::isPressedCrouch` | c channel | b |
| `MR::isPressedDown` | c channel | b |
| `MR::isPressedFire` | c channel | b |
| `MR::isPressedHome` | c channel | b |
| `MR::isPressedJump` | c channel | b |
| `MR::isPressedLeft` | c channel | b |
| `MR::isPressedMap` | c channel | b |
| `MR::isPressedMovingWall` | h  | b |
| `MR::isPressedPause` | c channel | b |
| `MR::isPressedRight` | c channel | b |
| `MR::isPressedRoofAndGround` | h  | b |
| `MR::isPressedUp` | c channel | b |
| `MR::isRailGoingToEnd` | h  | b |
| `MR::isRailReachedEdge` | h  | b |
| `MR::isRailReachedGoal` | h  | b |
| `MR::isRailReachedHorizonCurrentPos` | h , d  | b |
| `MR::isRailReachedNearGoal` | h , d  | b |
| `MR::isRailReachedNearNextPoint` | h , d  | b |
| `MR::isRegisteredDemoActionAppear` | h  | b |
| `MR::isRegisteredDemoActionNerve` | h  | b |
| `MR::isReleasedCrouch` | c channel | b |
| `MR::isRosettaTalkAstroDomeRecover` |  | b |
| `MR::isRosettaTalkCountDownStart` |  | b |
| `MR::isRosettaTalkKoopa` |  | b |
| `MR::isRosettaTalkTorchLecture` |  | b |
| `MR::isRosettaTalkTorchProgress` |  | b |
| `MR::isRosettaTalkTrickComet` |  | b |
| `MR::isRotAxisY` | F , F  | b |
| `MR::isSame` | h , h  | b |
| `MR::isSameDirection` | e rVec1, e rVec2, d tolerance? | b |
| `MR::isSameMtx` | F , F  | b |
| `MR::isSameMtxRot` | F , F  | b |
| `MR::isScenarioDecided` |  | b |
| `MR::isScreen16Per9` |  | b |
| `MR::isScreenCoveredByWipe` |  | b |
| `MR::isSelectedEdge` | h pRailGraphIter | b |
| `MR::isSensor` | h pSensor, g pName | b |
| `MR::isSensorAutoRush` | h pSensor | b |
| `MR::isSensorBinder` | h pSensor | b |
| `MR::isSensorEnemy` | h pSensor | b |
| `MR::isSensorEnemyAttack` | h pSensor | b |
| `MR::isSensorEye` | h pSensor | b |
| `MR::isSensorItem` | h pSensor | b |
| `MR::isSensorMapObj` | h pSensor | b |
| `MR::isSensorNpc` | h pSensor | b |
| `MR::isSensorPlayer` | h pSensor | b |
| `MR::isSensorPlayerOrRide` | h pSensor | b |
| `MR::isSensorPressObj` | h pSensor | b |
| `MR::isSensorPush` | h pSensor | b |
| `MR::isSensorRide` | h pSensor | b |
| `MR::isSensorRush` | h pSensor | b |
| `MR::isSensorType` | h pSensor, c type | b |
| `MR::isShadowProjected` | h , g  | b |
| `MR::isShadowProjectedAny` | h  | b |
| `MR::isShortTalk` | h  | b |
| `MR::isSpin` | c channel | b |
| `MR::isStageAstroLocation` |  | b |
| `MR::isStageBeginFadeWipe` |  | b |
| `MR::isStageBeginPrologueEvent` |  | b |
| `MR::isStageBeginTitleWipe` |  | b |
| `MR::isStageBeginWithoutWipe` |  | b |
| `MR::isStageDisablePauseMenu` |  | b |
| `MR::isStageEpilogueDemo` |  | b |
| `MR::isStageFileSelect` |  | b |
| `MR::isStageKoopaVs` |  | b |
| `MR::isStageKoopaVs3` |  | b |
| `MR::isStageStarPieceFollowGroupLimit` |  | b |
| `MR::isStageStatePowerStarAppeared` |  | b |
| `MR::isStageStateScenarioOpeningCamera` |  | b |
| `MR::isStageSuddenDeathDodoryu` |  | b |
| `MR::isStageSwimAngleLimit` |  | b |
| `MR::isStarComplete` | g  | b |
| `MR::isStarCompleteAllGalaxy` |  | b |
| `MR::isStarCompleteNormalScenario` | g  | b |
| `MR::isStarPieceCounterStop` |  | b |
| `MR::isStarPointer1PInvalid2PValidMode` |  | b |
| `MR::isStarPointer2PTransparencyMode` |  | b |
| `MR::isStarPointerCommandStream` | h , c  | b |
| `MR::isStarPointerInScreen` | c  | b |
| `MR::isStarPointerInScreenAnyPort` | C  | b |
| `MR::isStarPointerModeBlueStarReady` |  | b |
| `MR::isStarPointerModeErrorWindow` |  | b |
| `MR::isStarPointerModeHomeButton` |  | b |
| `MR::isStarPointerModeMarioLauncher` |  | b |
| `MR::isStarPointerModeStarPieceTarget` |  | b |
| `MR::isStarPointerPointing1P` | h , g , b , b  | b |
| `MR::isStarPointerPointing1PWithoutCheckZ` | h , g , b , b  | b |
| `MR::isStarPointerPointing1Por2P` | h , g , b , b  | b |
| `MR::isStarPointerPointing2P` | h , g , b , b  | b |
| `MR::isStarPointerPointing2POnPressButton` | h , g , b , b  | b |
| `MR::isStarPointerPointing2POnTriggerButton` | h , g , b , b  | b |
| `MR::isStarPointerPointing:edcb` | e , d , c , b  | b |
| `MR::isStarPointerPointing:hcbg` | h , c , b , g  | b |
| `MR::isStarPointerPointingFileSelect` | h  | b |
| `MR::isStarPointerPointingPane:hgcbg` | h , g , c , b , g  | b |
| `MR::isStarPointerPointingPane:hhgcbg` | h , h pPane, g pName, c , b , g  | b |
| `MR::isStarPointerPointingPaneForMeterLayout` | h , g , c , b , g  | b |
| `MR::isStarPointerPointingTarget` | h , g , c , b , g  | b |
| `MR::isStarPointerValid` | c  | b |
| `MR::isStartAnimCameraEnd` |  | b |
| `MR::isStartPosCameraEnd` |  | b |
| `MR::isStopAnimFrame` | h  | b |
| `MR::isStopCinemaFrame` |  | b |
| `MR::isStopOrFadeoutBgmName` | g  | b |
| `MR::isStopOrFadeoutStageBgmID` | c  | b |
| `MR::isSubSpin` | c channel | b |
| `MR::isSubjectiveCameraOnForObjClipping` |  | b |
| `MR::isSystemTalking` |  | b |
| `MR::isSystemWipeActive` |  | b |
| `MR::isTalkEnableEnd` | h  | b |
| `MR::isTalkEnd` | h  | b |
| `MR::isTalkEntry` | h  | b |
| `MR::isTalkNone` | h  | b |
| `MR::isTalkStart` | h  | b |
| `MR::isTalkTalking` | h  | b |
| `MR::isThroughPolygon` | h  | b |
| `MR::isTimeKeepDemoActive` |  | b |
| `MR::isUFOKinokoBeforeConstruction` |  | b |
| `MR::isUFOKinokoUnderConstruction` |  | b |
| `MR::isUseTex` | h , c  | b |
| `MR::isUseTexMtx` | h , c  | b |
| `MR::isValidCalcAnim` | h  | b |
| `MR::isValidCollisionParts` | h  | b |
| `MR::isValidDraw` | h  | b |
| `MR::isValidFollowID` | h  | b |
| `MR::isValidHitSensor` | h pActor, g pName | b |
| `MR::isValidInfo` | h rIter | b |
| `MR::isValidMovement` | h  | b |
| `MR::isValidSwitchA` | h  | b |
| `MR::isValidSwitchAppear` | h  | b |
| `MR::isValidSwitchB` | h  | b |
| `MR::isValidSwitchDead` | h  | b |
| `MR::isWallCodeGhostThrough` | h  | b |
| `MR::isWallCodeNoAction` | h  | b |
| `MR::isWallCodeRebound` | h  | b |
| `MR::isWallPolygon:d` | d  | b |
| `MR::isWallPolygon:ee` | e , e  | b |
| `MR::isWatchedPrevEdge` | h pRailGraphIter | b |
| `MR::isWaterPolygon` | h  | b |
| `MR::isWipeActive` |  | b |
| `MR::isWipeBlank` |  | b |
| `MR::isWipeOpen` |  | b |
| `MR::isYesNoSelected` |  | b |
| `MR::isYesNoSelectedYes` |  | b |
| `MR::isZeroGravity` | h pActor | b |
| `MR::joinToGroup` | h , g  | h |
| `MR::joinToGroupArray` | h , h , g , c  | h |
| `MR::joinToMovementOnOffGroup` | g , h , c  | h |
| `MR::joinToNameObjGroup` | h , g  | h |
| `MR::jumpPlayer` | e  |  |
| `MR::killAtAnimStopped` | h , c  |  |
| `MR::killGravity` | h pActor, e rVec | e |
| `MR::killVelocityOnGroundCosH` | h , d  |  |
| `MR::killVelocityOnGroundH` | h  |  |
| `MR::killVelocityToTarget` | h , e  |  |
| `MR::killVelocityVertical` | h pActor |  |
| `MR::lerp` | c start, c end, d t | c |
| `MR::limitedSound` | g , c delay |  |
| `MR::limitedStarPieceHitSound` |  |  |
| `MR::loadActorLight` | h  |  |
| `MR::loadLight` | c  |  |
| `MR::loadLightPlayer` |  |  |
| `MR::loadProjectionMtx` |  |  |
| `MR::loadProjectionMtxFor2D` |  |  |
| `MR::loadTexFromArc:g` | g  | h |
| `MR::loadTexFromArc:gg` | g , g  | h |
| `MR::loadViewMtx` |  |  |
| `MR::loadViewMtxFor2DModel` |  |  |
| `MR::makeAxisAndCosignVecToVec` | E pAxis, D pCos, e rFrom, e rTo | b |
| `MR::makeAxisCrossPlane` | E pVec, E pCross, e rNorm |  |
| `MR::makeAxisFrontSide` | E pUp, E pSide, e rFront, e rSide |  |
| `MR::makeAxisFrontUp` | E pSide, E pUp, e rFront, e rUp |  |
| `MR::makeAxisUpFront` | E pSide, E pFront, e rUp, e rFront |  |
| `MR::makeAxisUpSide` | E pFront, E pSide, e rUp, e rSide |  |
| `MR::makeAxisVerticalZX` | E pVec, e rAxis |  |
| `MR::makeCometMessageID` | h , g  |  |
| `MR::makeFileNameConsideringLanguage` | h rDst, g pFilePath |  |
| `MR::makeGameLayoutArchiveFileName` | h rDst, g pName | b |
| `MR::makeLayoutArchiveFileName` | h rDst, g pName | b |
| `MR::makeLayoutArchiveFileNameFromPrefix` | h rDst, g pName, b fallback | b |
| `MR::makeMtxFrontNoSupport` | F , e  |  |
| `MR::makeMtxFrontNoSupportPos` | F , e , e  |  |
| `MR::makeMtxFrontSidePos` | F , e , e , e  |  |
| `MR::makeMtxFrontUpPos` | F , e , e , e  |  |
| `MR::makeMtxMoment` | F , e  |  |
| `MR::makeMtxOnMapCollision` | F , h , d  | b |
| `MR::makeMtxRotate:Fccc` | F , c , c , c  |  |
| `MR::makeMtxRotate:Fddd` | F , d , d , d  |  |
| `MR::makeMtxRotate:Fe` | F , e  |  |
| `MR::makeMtxRotateY` | F , d  |  |
| `MR::makeMtxSideFront` | F , e , e  |  |
| `MR::makeMtxSideUp` | F , e , e  |  |
| `MR::makeMtxSideUpPos` | F , e , e , e  |  |
| `MR::makeMtxTR:Fdddddd` | F , d , d , d , d , d , d  |  |
| `MR::makeMtxTR:Fee` | F , e , e  |  |
| `MR::makeMtxTR:Fh` | F , h  |  |
| `MR::makeMtxTRS:Fddddddddd` | F , d , d , d , d , d , d , d , d , d  |  |
| `MR::makeMtxTRS:Feee` | F , e , e , e  |  |
| `MR::makeMtxTRS:Fh` | F , h  |  |
| `MR::makeMtxTransRotateY:Fdddd` | F , d , d , d , d  |  |
| `MR::makeMtxTransRotateY:Fh` | F , h  |  |
| `MR::makeMtxUpFrontPos` | F , e , e , e  |  |
| `MR::makeMtxUpNoSupport` | F , e  |  |
| `MR::makeMtxUpNoSupportPos` | F , e , e  |  |
| `MR::makeMtxUpSidePos` | F , e , e , e  |  |
| `MR::makeMtxWithoutScale` | F , f  |  |
| `MR::makeObjectArchiveFileName` | h rDst, g pName | b |
| `MR::makeQuatAndFrontFromRotate` | I , E , h  |  |
| `MR::makeQuatFromRotate` | I , h  |  |
| `MR::makeQuatFromVec` | I pQuat, e rFront, e rUp |  |
| `MR::makeQuatRotateDegree` | I pQuat, e rRot |  |
| `MR::makeQuatRotateRadian` | I pQuat, e rRot |  |
| `MR::makeQuatSideUp` | I pDst, e rSide, e rUp |  |
| `MR::makeQuatUpFront` | I pQuat, e rUp, e rFront |  |
| `MR::makeQuatUpNoSupport` | I pQuat, e rUp |  |
| `MR::makeRTFromMtxPtr` | E , E , F , b  |  |
| `MR::makeScenarioArchiveFileName` | h rDst, g pGalaxyName |  |
| `MR::max:cc` | c a, c b | c |
| `MR::max:dd` | d a, d b | d |
| `MR::min:cc` | c a, c b | c |
| `MR::min:dd` | d a, d b | d |
| `MR::miniTornadoJumpPlayer` |  |  |
| `MR::mod` | d x, d y | d |
| `MR::modRadian` | d angle | d |
| `MR::moveAndTurnAlongRail` | h , d , h , B  |  |
| `MR::moveAndTurnToDirection:hEeh` | h , E , e , h  |  |
| `MR::moveAndTurnToDirection:heh` | h , e , h  |  |
| `MR::moveAndTurnToPlayer:hEh` | h , E , h  |  |
| `MR::moveAndTurnToPlayer:hh` | h , h  |  |
| `MR::moveAndTurnToTarget:hEeh` | h , E , e , h  |  |
| `MR::moveAndTurnToTarget:heh` | h , e , h  |  |
| `MR::moveCoord` | h , d  |  |
| `MR::moveCoordAndCheckPassPointNo` | h , d  | c |
| `MR::moveCoordAndFollowTrans:h` | h  |  |
| `MR::moveCoordAndFollowTrans:hd` | h , d  |  |
| `MR::moveCoordAndFollowTransAndCheckPassPointNo` | h  | c |
| `MR::moveCoordAndTransToNearestRailPoint` | h  |  |
| `MR::moveCoordAndTransToNearestRailPos:h` | h  |  |
| `MR::moveCoordAndTransToNearestRailPos:he` | h , e  |  |
| `MR::moveCoordAndTransToNextPoint` | h  |  |
| `MR::moveCoordAndTransToRailPoint` | h , c  |  |
| `MR::moveCoordAndTransToRailStartPoint` | h  |  |
| `MR::moveCoordToEndPos` | h  |  |
| `MR::moveCoordToNearestPos` | h , e  |  |
| `MR::moveCoordToRailPoint` | h , c  |  |
| `MR::moveCoordToStartPos` | h  |  |
| `MR::moveNextNode` | h pRailGraphIter |  |
| `MR::moveNodeNearPosition` | h pRailGraphIter, e rPosition, d maxDistance, h pSelector |  |
| `MR::moveRailRider` | h  |  |
| `MR::moveTransToCurrentRailPos` | h  |  |
| `MR::moveTransToOtherActorRailPos` | h , h  |  |
| `MR::moveVolumeStageBGM` | d volume, c fadeTime |  |
| `MR::moveVolumeStageBGMForNoteFairy` | d volume, c fadeTime |  |
| `MR::moveVolumeSubBGM` | d volume, c fadeTime |  |
| `MR::needStageSwitchReadA` | h , h  | b |
| `MR::needStageSwitchReadAppear` | h , h  | b |
| `MR::needStageSwitchReadB` | h , h  | b |
| `MR::needStageSwitchWriteA` | h , h  | b |
| `MR::needStageSwitchWriteB` | h , h  | b |
| `MR::needStageSwitchWriteDead` | h , h  | b |
| `MR::newXanimePlayer` | h , g , h , c , h  | h |
| `MR::newXanimeResourceTable` | h  | h |
| `MR::normalize:E` | E pVec |  |
| `MR::normalize:F` | F pMtx |  |
| `MR::normalize:J` | J pVec |  |
| `MR::normalize:ddd` | d x, d min, d max | d |
| `MR::normalize:eE` | e rSrc, E pDst |  |
| `MR::normalize:jJ` | j rSrc, J pDst |  |
| `MR::normalizeAbs` | d x, d min, d max | d |
| `MR::normalizeAngleAbs` | d angle | d |
| `MR::normalizeOrZero:E` | E pVec | b |
| `MR::normalizeOrZero:J` | J pVec | b |
| `MR::normalizeOrZero:eE` | e rSrc, E pDst | b |
| `MR::noticePlayerDashChance` |  |  |
| `MR::notifyDirectGetStarPiecePlayer` |  |  |
| `MR::offAllPlayerLeftSupply` |  |  |
| `MR::offBind` | h  |  |
| `MR::offCalcGravity` | h  |  |
| `MR::offCalcShadow` | h , g  |  |
| `MR::offCalcShadowAll` | h  |  |
| `MR::offCalcShadowDropPrivateGravity` | h , g  |  |
| `MR::offDrawEffect` | h  |  |
| `MR::offEntryDrawBuffer` | h  |  |
| `MR::offPlayerControl` |  |  |
| `MR::offReadNodeAutomatic` | h  |  |
| `MR::offRootNodeAutomatic` | h  |  |
| `MR::offShadowVisibleSyncHost` | h , g  |  |
| `MR::offShadowVisibleSyncHostAll` | h  |  |
| `MR::offSwitchA` | h  |  |
| `MR::offSwitchB` | h  |  |
| `MR::offSwitchDead` | h  |  |
| `MR::offUpdateCollisionParts` | h  |  |
| `MR::onBind` | h  |  |
| `MR::onCalcGravity` | h  |  |
| `MR::onCalcShadow` | h , g  |  |
| `MR::onCalcShadowAll` | h  |  |
| `MR::onCalcShadowDropGravity` | h , g  |  |
| `MR::onCalcShadowDropGravityOneTime` | h , g  |  |
| `MR::onCalcShadowDropPrivateGravity` | h , g  |  |
| `MR::onCalcShadowDropPrivateGravityOneTime` | h , g  |  |
| `MR::onCalcShadowOneTime` | h , g  |  |
| `MR::onCalcShadowOneTimeAll` | h  |  |
| `MR::onDrawEffect` | h  |  |
| `MR::onEmitEffectSyncClipping` | h , g  |  |
| `MR::onEntryDrawBuffer` | h  |  |
| `MR::onFollowDemoEffect` |  |  |
| `MR::onForceDeleteEffectSyncClipping` | h , g  |  |
| `MR::onGameEventFlagBeeMarioAtFirst` |  |  |
| `MR::onGameEventFlagEnableToSpinAndStarPointer` |  |  |
| `MR::onGameEventFlagEndButlerDomeLecture` |  |  |
| `MR::onGameEventFlagEndButlerGalaxyMoveLecture` |  |  |
| `MR::onGameEventFlagEndButlerStarPieceLecture` |  |  |
| `MR::onGameEventFlagEndTicoGuideDemo` |  |  |
| `MR::onGameEventFlagFireMarioAtFirst` |  |  |
| `MR::onGameEventFlagFlyingMarioAtFirst` |  |  |
| `MR::onGameEventFlagGalaxyOpen` | g  |  |
| `MR::onGameEventFlagGetLuigiLetter` |  |  |
| `MR::onGameEventFlagHopperMarioAtFirst` |  |  |
| `MR::onGameEventFlagIceMarioAtFirst` |  |  |
| `MR::onGameEventFlagInvincibleMarioAtFirst` |  |  |
| `MR::onGameEventFlagLifeUpAtFirst` |  |  |
| `MR::onGameEventFlagOneUpAtFirst` |  |  |
| `MR::onGameEventFlagPlayMoviePrologueA` |  |  |
| `MR::onGameEventFlagPlayMoviePrologueB` |  |  |
| `MR::onGameEventFlagRosettaTalkAboutTicoInTower` |  |  |
| `MR::onGameEventFlagSurfingTutorialAtFirst` |  |  |
| `MR::onGameEventFlagTalkedToLuigiAfterRescued` |  |  |
| `MR::onGameEventFlagTamakoroTutorialAtFirst` |  |  |
| `MR::onGameEventFlagTeresaMarioAtFirst` |  |  |
| `MR::onGameEventFlagViewCompleteEnding` |  |  |
| `MR::onGameEventFlagViewNormalEnding` |  |  |
| `MR::onMessageAlreadyRead` | c  |  |
| `MR::onMovementOnOffGroup` | g  |  |
| `MR::onPlayerControl` | b  |  |
| `MR::onReadNodeAutomatic` | h  |  |
| `MR::onRootNodeAutomatic` | h  |  |
| `MR::onShadowFollowHostScale` | h , g  |  |
| `MR::onShadowFollowHostScaleAll` | h  |  |
| `MR::onShadowVolumeCutDropLength` | h , g  |  |
| `MR::onStarPointerSceneOut` |  |  |
| `MR::onStartOnlyFront` | h  |  |
| `MR::onSwitchA` | h  |  |
| `MR::onSwitchB` | h  |  |
| `MR::onSwitchDead` | h  |  |
| `MR::onUpdateCollisionParts` | h  |  |
| `MR::onUpdateCollisionPartsOnetimeImmediately` | h  |  |
| `MR::openSystemWipeFade` | c frame? |  |
| `MR::openSystemWipeWhiteFade` | c frame? |  |
| `MR::openWipeCircle` | c frame? |  |
| `MR::openWipeFade` | c frame? |  |
| `MR::openWipeWhiteFade` | c frame? |  |
| `MR::orderRotateMtx` | c , e , F  |  |
| `MR::orthogonalize` | F  |  |
| `MR::overlayWithPreviousScreen` | c  |  |
| `MR::pauseOffAnimCamera` | h , g  |  |
| `MR::pauseOffCameraDirector` |  |  |
| `MR::pauseOffLensFlare` |  |  |
| `MR::pauseOnAnimCamera` | h , g  |  |
| `MR::pauseOnCameraDirector` |  |  |
| `MR::pauseTimeKeepDemo` | h  |  |
| `MR::permitLevelSE` |  |  |
| `MR::permitSE` |  |  |
| `MR::permitTrigSE` |  |  |
| `MR::pi` |  | d |
| `MR::pi2` |  | d |
| `MR::playLevelMarioPinchBGM` | b secondPinch |  |
| `MR::playSceneForScenarioOpeningCamera` |  |  |
| `MR::preScaleMtx:Fd` | F , d  |  |
| `MR::preScaleMtx:Fddd` | F , d , d , d  |  |
| `MR::preScaleMtx:Fe` | F , e  |  |
| `MR::prepareMaterials` | h  |  |
| `MR::preventPlayerRush` |  |  |
| `MR::progressPlayerBckOnPause` |  |  |
| `MR::pushPlayer` | e  |  |
| `MR::pushPlayerFromArea` | e  |  |
| `MR::readMessage` | h  |  |
| `MR::readyPlayerDemo` |  |  |
| `MR::reboundVelocityFromCollision` | h , d ?, d ?, d ? | b |
| `MR::reboundVelocityFromEachCollision` | h , d , d , d , d  | b |
| `MR::receiveItemHideMsg` | c msg, h pSender, h pReceiver | b |
| `MR::receiveItemShowMsg` | c msg, h pSender, h pReceiver | b |
| `MR::receiveMapPartsRotateMsg` | h , c , h , h  | b |
| `MR::recoverSoundVolumeSetting` | c maxSteps |  |
| `MR::reflectBckCtrlData` | h , h  |  |
| `MR::reflectVelocityH` | h , h , h , d  | b |
| `MR::registerAnimeFunc` | h , h  |  |
| `MR::registerBranchFunc` | h , h  |  |
| `MR::registerDemoActionNerve` | h , h , g  |  |
| `MR::registerDemoCast` | h , g , h  |  |
| `MR::registerEventFunc` | h , h  |  |
| `MR::registerGravity` | h pGravity |  |
| `MR::registerKillFunc` | h , h  |  |
| `MR::registerStorageSpinDriverPathDrawRange` | h , h , c , D  | c |
| `MR::releaseSoundHandle` | h , g  |  |
| `MR::removeExtensionString` | h rDst, g pPath |  |
| `MR::removeResourceHolders` | h pArena |  |
| `MR::repeat` | d value, d min, d max | d |
| `MR::repeatDegree:D` | D pValue |  |
| `MR::repeatDegree:d` | d value | d |
| `MR::replacePaneTexture` | h , g , h , c  |  |
| `MR::reportStarPointerGrabTarget` | h pActor |  |
| `MR::requestAppearPowerStar:h` | h  |  |
| `MR::requestAppearPowerStar:hce` | h , c , e  |  |
| `MR::requestAppearPowerStar:he` | h , e  |  |
| `MR::requestAppearPowerStar:hhd` | h , h , d  |  |
| `MR::requestAppearPowerStarCoin100` |  |  |
| `MR::requestBigBubbleGuidance` |  | b |
| `MR::requestBlueStarGuidance` |  | b |
| `MR::requestChangeScene` | c  |  |
| `MR::requestChangeSceneAfterBoot` |  |  |
| `MR::requestChangeSceneAfterGameOver` |  |  |
| `MR::requestChangeSceneTitle` |  |  |
| `MR::requestChangeStageAfterMiss` |  |  |
| `MR::requestChangeStageAfterStageClear` |  |  |
| `MR::requestChangeStageGoBackAstroDome` |  |  |
| `MR::requestChangeStageInGameAfterLoadingGameData` |  |  |
| `MR::requestChangeStageInGameMoving:gc` | g , c  |  |
| `MR::requestChangeStageInGameMoving:gch` | g , c , h  |  |
| `MR::requestCounterLayoutAppearanceForTicoEat` | b  |  |
| `MR::requestEffectStopSceneEnd` |  |  |
| `MR::requestEffectStopSceneStart` |  |  |
| `MR::requestEndGameOverDemo` |  |  |
| `MR::requestEndMissDemo` |  |  |
| `MR::requestFileSelectCopyGuidance` |  | b |
| `MR::requestFileSelectGuidance` |  | b |
| `MR::requestForceAppearHPMeter` |  |  |
| `MR::requestGoToAstroDomeFromAstroGalaxy` | c scenarioNo, c startId |  |
| `MR::requestGoToAstroGalaxy` | c startId |  |
| `MR::requestGrandStarGetDemo` |  |  |
| `MR::requestMarioLauncherGuidance` |  | b |
| `MR::requestMovementOff` | h  |  |
| `MR::requestMovementOnImageEffect` |  |  |
| `MR::requestMovementOnPlayer` |  |  |
| `MR::requestOneUp` |  |  |
| `MR::requestPointerGuidanceNoInformation` |  | b |
| `MR::requestPowerStarGetDemo` |  |  |
| `MR::requestPowerUpHPMeter` |  |  |
| `MR::requestShowGalaxyMap` |  |  |
| `MR::requestStarPieceLectureGuidance` |  | b |
| `MR::requestStartDemo` | h , g , h , h  | b |
| `MR::requestStartDemoMarioPuppetable:hghh` | h , g , h , h  | b |
| `MR::requestStartDemoMarioPuppetable:hhghh` | h , h , g , h , h  | b |
| `MR::requestStartDemoMarioPuppetableWithoutCinemaFrame` | h , g , h , h  | b |
| `MR::requestStartDemoRegistered` | h , h , h , g  | b |
| `MR::requestStartDemoRegisteredMarioPuppetable` | h , h , h , g  | b |
| `MR::requestStartGameOverDemo` |  |  |
| `MR::requestStartScenarioSelect` | g  |  |
| `MR::requestStartScenarioSelectForComet` | g , c  |  |
| `MR::requestStartTimeKeepDemo:hgg` | h , g , g  | b |
| `MR::requestStartTimeKeepDemo:hghhg` | h , g , h , h , g  | b |
| `MR::requestStartTimeKeepDemoMarioPuppetable:hgg` | h , g , g  | b |
| `MR::requestStartTimeKeepDemoMarioPuppetable:hghhg` | h , g , h , h , g  | b |
| `MR::requestStartTimeKeepDemoMarioPuppetable:hhghhg` | h , h , g , h , h , g  | b |
| `MR::requestStartTimeKeepDemoWithoutCinemaFrame` | h , g , h , h , g  | b |
| `MR::requestTicoSeedGuidance` | c  | b |
| `MR::requestTicoSeedGuidanceForce` |  | b |
| `MR::resetAllCollisionMtx` | h  |  |
| `MR::resetAndForwardNode` | h , c  |  |
| `MR::resetCameraLocalOffset` |  |  |
| `MR::resetCameraMan` |  |  |
| `MR::resetDrawState` |  |  |
| `MR::resetNode` | h  |  |
| `MR::resetPlayerEffect` |  |  |
| `MR::resetPlayerStatus` |  |  |
| `MR::resetPosition:h` | h  |  |
| `MR::resetPosition:he` | h , e  |  |
| `MR::resetPosition:hg` | h , g  |  |
| `MR::resetSystemAndGameStatus` |  |  |
| `MR::resetYesNoSelectorSE` |  |  |
| `MR::restrictVelocity` | h , d  |  |
| `MR::resumeAllSceneNameObj` |  |  |
| `MR::resumeTimeKeepDemo` | h  |  |
| `MR::reverseRailDirection` | h  |  |
| `MR::rotAxisVecRad` | e rVec, e rAxis, E pOut, d rad |  |
| `MR::rotateDirectionGravityDegree` | h , E , d  |  |
| `MR::rotateMtxLocalX` | F , d  |  |
| `MR::rotateMtxLocalXDegree` | F , d  |  |
| `MR::rotateMtxLocalY` | F , d  |  |
| `MR::rotateMtxLocalYDegree` | F , d  |  |
| `MR::rotateMtxLocalZ` | F , d  |  |
| `MR::rotateMtxLocalZDegree` | F , d  |  |
| `MR::rotateMtxMoment:Fe` | F , e  |  |
| `MR::rotateMtxMoment:Ffe` | F , f , e  |  |
| `MR::rotateQuatMoment` | I pQuat, e rMoment |  |
| `MR::rotateQuatRollBall` | I pQuat, e rVel, e rNorm, d radius |  |
| `MR::rotateVecDegree:DDd` | D pX, D pY, d degree |  |
| `MR::rotateVecDegree:Eed` | E pDst, e rAxis, d degree |  |
| `MR::rotateVecDegree:Eeed` | E pDst, e rSrc, e rAxis, d degree |  |
| `MR::rotateVecDegree:Jd` | J pDst, d degree |  |
| `MR::rotateVecRadian:Eed` | E pDst, e rAxis, d angle |  |
| `MR::rotateVecRadian:Eeed` | E pDst, e rSrc, e rAxis, d angle |  |
| `MR::scaleMtxToDir` | F , e , e  |  |
| `MR::scaleMtxToLocalMtx:FFFe` | F , F , F , e  |  |
| `MR::scaleMtxToLocalMtx:FFe` | F , F , e  |  |
| `MR::scaleVelocity` | h , d  |  |
| `MR::scan16` | g pSrc, g pSubStr, C pDst |  |
| `MR::scan32` | g pSrc, g pSubStr, C pDst |  |
| `MR::scan8` | g pSrc, g pSubStr, C pDst |  |
| `MR::scans16x4` | g pSrc, g pSubStr, C pDst |  |
| `MR::scatterStarPiecePlayer` | c  |  |
| `MR::sceneOpaqueEnd` |  |  |
| `MR::searchChildJoint` | h pRoot, h pJoint | h |
| `MR::selectReverseEdge` | h pRailGraphIter |  |
| `MR::sendArbitraryMsg` | c msg, h pReceiver, h pSender | b |
| `MR::sendMsgAwayJump` | h pReceiver, h pSender | b |
| `MR::sendMsgEnemyAttack` | h pReceiver, h pSender | b |
| `MR::sendMsgEnemyAttackCounterHipDrop` | h pReceiver, h pSender | b |
| `MR::sendMsgEnemyAttackCounterSpin` | h pReceiver, h pSender | b |
| `MR::sendMsgEnemyAttackElectric` | h pReceiver, h pSender | b |
| `MR::sendMsgEnemyAttackExplosion` | h pReceiver, h pSender | b |
| `MR::sendMsgEnemyAttackExplosionToBindedSensor` | h pActor, h pSender | b |
| `MR::sendMsgEnemyAttackFire` | h pReceiver, h pSender | b |
| `MR::sendMsgEnemyAttackFireStrong` | h pReceiver, h pSender | b |
| `MR::sendMsgEnemyAttackFlip` | h pReceiver, h pSender | b |
| `MR::sendMsgEnemyAttackFlipJump` | h pReceiver, h pSender | b |
| `MR::sendMsgEnemyAttackFlipMaximum` | h pReceiver, h pSender | b |
| `MR::sendMsgEnemyAttackFlipMaximumToDir` | h pReceiver, h pSender, e rDir | b |
| `MR::sendMsgEnemyAttackFlipRot` | h pReceiver, h pSender | b |
| `MR::sendMsgEnemyAttackFlipToDir` | h pReceiver, h pSender, e rDir | b |
| `MR::sendMsgEnemyAttackFlipWeak` | h pReceiver, h pSender | b |
| `MR::sendMsgEnemyAttackFlipWeakJump` | h pReceiver, h pSender | b |
| `MR::sendMsgEnemyAttackFreeze` | h pReceiver, h pSender | b |
| `MR::sendMsgEnemyAttackHeatBeam` | h pReceiver, h pSender | b |
| `MR::sendMsgEnemyAttackMaximum` | h pReceiver, h pSender | b |
| `MR::sendMsgEnemyAttackMaximumToDir` | h pReceiver, h pSender, e rDir | b |
| `MR::sendMsgEnemyAttackMsgToDir` | c msg, h pReceiver, h pSender, e rDir | b |
| `MR::sendMsgEnemyAttackStrong` | h pReceiver, h pSender | b |
| `MR::sendMsgEnemyAttackStrongToDir` | h pReceiver, h pSender, e rDir | b |
| `MR::sendMsgEnemyAttackToBindedSensor` | h pActor, h pSender | b |
| `MR::sendMsgExplosionToNearActor` | h pSender, d distance |  |
| `MR::sendMsgJump` | h pReceiver, h pSender | b |
| `MR::sendMsgKick` | h pReceiver, h pSender | b |
| `MR::sendMsgLockOnStarPieceShoot` | h pReceiver, h pSender | b |
| `MR::sendMsgPlayerPunch` | h pReceiver, h pSender | b |
| `MR::sendMsgPlayerTrample` | h pReceiver, h pSender | b |
| `MR::sendMsgPush` | h pReceiver, h pSender | b |
| `MR::sendMsgPushAndKillVelocityToTarget` | h , h , h  | b |
| `MR::sendMsgStarPieceAttack` | h pReceiver, h pSender | b |
| `MR::sendMsgStarPieceGift` | h pReceiver, h pSender, c num | b |
| `MR::sendMsgStartDemo` | h pActor | b |
| `MR::sendMsgTaken` | h pReceiver, h pSender | b |
| `MR::sendMsgToAllLiveActor` | c msg, h pActor |  |
| `MR::sendMsgToBindedSensor:ch` | c msg, h pSender | b |
| `MR::sendMsgToBindedSensor:chh` | c msg, h pActor, h pSender | b |
| `MR::sendMsgToEnemyAttackBlow` | h pReceiver, h pSender | b |
| `MR::sendMsgToEnemyAttackBlowOrTrample` | h pReceiver, h pSender, d ratio | b |
| `MR::sendMsgToEnemyAttackShockWave` | h pReceiver, h pSender | b |
| `MR::sendMsgToEnemyAttackTrample` | h pReceiver, h pSender | b |
| `MR::sendMsgToGroundSensor` | c msg, h pSender | b |
| `MR::sendMsgToGroupMember` | c msg, h pActor, h pSensor, g pName |  |
| `MR::sendMsgToWallSensor` | c msg, h pSender | b |
| `MR::sendMsgTouchJump` | h pReceiver, h pSender | b |
| `MR::sendSimpleMsgToActor` | c msg, h pActor | b |
| `MR::sendStageResultSequenceParam` | c  |  |
| `MR::separateScalarAndDirection:DEe` | D pScalar, E pDir, e rVec |  |
| `MR::separateScalarAndDirection:DJj` | D pScalar, J pDir, j rVec |  |
| `MR::setAllAnimFrame` | h , g , d  |  |
| `MR::setAllAnimFrameAndStop` | h , g , d  |  |
| `MR::setAllAnimFrameAtEnd` | h , g  |  |
| `MR::setAnimFrame` | h , d , c  |  |
| `MR::setAnimFrameAndStop` | h , d , c  |  |
| `MR::setAnimFrameAndStopAdjustTextHeight` | h , g , c  |  |
| `MR::setAnimFrameAndStopAdjustTextWidth` | h , g , c  |  |
| `MR::setAnimFrameAndStopAtEnd` | h , c  |  |
| `MR::setAnimRate` | h , d , c  |  |
| `MR::setAudioEffectType` | c effectType, c priority |  |
| `MR::setBaseScale` | h , e  |  |
| `MR::setBaseTRMtx:hF` | h , F  |  |
| `MR::setBaseTRMtx:hf` | h , f  |  |
| `MR::setBaseTRMtx:hi` | h , i  |  |
| `MR::setBckBlendWeight:dd` | d , d  |  |
| `MR::setBckBlendWeight:ddd` | d , d , d  |  |
| `MR::setBckBlendWeight:dddd` | d , d , d , d  |  |
| `MR::setBckFrame` | h , d  |  |
| `MR::setBckFrameAndStop` | h , d  |  |
| `MR::setBckFrameAtRandom` | h  |  |
| `MR::setBckRate` | h , d  |  |
| `MR::setBckRatePlayer` | d  |  |
| `MR::setBindTriangleFilter` | h , h  |  |
| `MR::setBinderCollisionPartsFilter` | h , h  |  |
| `MR::setBinderExceptActor` | h , h  |  |
| `MR::setBinderExceptSensorType` | h , e , d  |  |
| `MR::setBinderIgnoreMovingCollision` | h  |  |
| `MR::setBinderOffsetVec` | h pActor, e pVec, b isInHostFrame |  |
| `MR::setBinderRadius` | h , d  |  |
| `MR::setBodySensorType` | h , c  |  |
| `MR::setBodySensorTypeMapObj` | h  |  |
| `MR::setBodySensorTypeMoveCollision` | h  |  |
| `MR::setBodySensorTypePress` | h  |  |
| `MR::setBpkFrame` | h , d  |  |
| `MR::setBpkFrameAndStop` | h , d  |  |
| `MR::setBrkFrame` | h , d  |  |
| `MR::setBrkFrameAndStop` | h , d  |  |
| `MR::setBrkFrameEndAndStop` | h  |  |
| `MR::setBrkRate` | h , d  |  |
| `MR::setBtkFrame` | h , d  |  |
| `MR::setBtkFrameAndStop` | h , d  |  |
| `MR::setBtkFrameAtRandom` | h  |  |
| `MR::setBtkRate` | h , d  |  |
| `MR::setBtpFrame` | h , d  |  |
| `MR::setBtpFrameAndStop` | h , d  |  |
| `MR::setBvaFrame` | h , d  |  |
| `MR::setBvaFrameAndStop` | h , d  |  |
| `MR::setBvaRate` | h , d  |  |
| `MR::setCameraTargetToPlayer` | h  |  |
| `MR::setCameraViewMtx` | f rMtx |  |
| `MR::setClippingFar` | h , d  |  |
| `MR::setClippingFar100m` | h  |  |
| `MR::setClippingFar200m` | h  |  |
| `MR::setClippingFar300m` | h  |  |
| `MR::setClippingFar50m` | h  |  |
| `MR::setClippingFarMax` | h  |  |
| `MR::setClippingRangeIncludeShadow` | h , E , d  |  |
| `MR::setClippingTypeSphere:hd` | h , d  |  |
| `MR::setClippingTypeSphere:hde` | h , d , e  |  |
| `MR::setClippingTypeSphereContainsModelBoundingBox` | h , d  |  |
| `MR::setCollisionMtx:h` | h  |  |
| `MR::setCollisionMtx:hh` | h , h  |  |
| `MR::setCometAnimFromId` | h , c , c  |  |
| `MR::setCometPaneAnimFromId` | h , g , c , c  |  |
| `MR::setCubeBgmChangeInvalid` |  |  |
| `MR::setCurrentPlacementZoneId` | c  |  |
| `MR::setCurrentScenarioNo` | c , c  |  |
| `MR::setDefaultPose` | h  |  |
| `MR::setDefaultViewportAndScissor` |  |  |
| `MR::setDepthOfFieldBlurIntensity` | d  |  |
| `MR::setDistanceToTalk` | h , d  |  |
| `MR::setEffectBaseScale` | h , g , d  |  |
| `MR::setEffectColor` | h , g , c , c , c , c , c , c  |  |
| `MR::setEffectDirectionalSpeed` | h , g , d  |  |
| `MR::setEffectEnvColor` | h , g , c , c , c  |  |
| `MR::setEffectHostSRT` | h , g , e pTranslation, e pRotation, e pScale |  |
| `MR::setEffectLocalScale` | h , g , e  |  |
| `MR::setEffectName` | h , g , g  |  |
| `MR::setEffectPrmColor` | h , g , c , c , c  |  |
| `MR::setEffectRate` | h , g , d  |  |
| `MR::setFollowPos` | j , h , g  |  |
| `MR::setFollowTypeAdd` | h , g  |  |
| `MR::setFollowTypeReplace` | h , g  |  |
| `MR::setFovy` | d  |  |
| `MR::setGameCameraTarget` | h  |  |
| `MR::setGameCameraTargetToPlayer` |  |  |
| `MR::setGroupClipping` | h , h , c capacity |  |
| `MR::setHitSensorApart` | h pSender, h pReceiver |  |
| `MR::setImageEffectControlAuto` |  |  |
| `MR::setInfluencedAlphaToChild` | h  |  |
| `MR::setInformationMessageReplaceString` | g , c  |  |
| `MR::setInitializeStateAfterPlacement` |  |  |
| `MR::setInitializeStatePlacement` |  |  |
| `MR::setInitializeStatePlacementHighPriority` |  |  |
| `MR::setInitializeStatePlacementPlayer` |  |  |
| `MR::setJointControllerParam:hhc` | h , h , c  |  |
| `MR::setJointControllerParam:hhg` | h , h , g  |  |
| `MR::setJointTransformLocalMtx` | h , g , F  |  |
| `MR::setLauncherCameraAngle` | d , d , d , d  |  |
| `MR::setLayoutAlpha` | h , c  |  |
| `MR::setLayoutAlphaFloat` | h , d  |  |
| `MR::setLayoutPosAtPaneTrans` | h , h , g  |  |
| `MR::setLayoutScaleAtPaneScale` | h , h , g  |  |
| `MR::setLayoutScalePosAtPaneScaleTrans` | h , h , g  |  |
| `MR::setLayoutScalePosAtPaneScaleTransIfExecCalcAnim` | h , h , g  |  |
| `MR::setMapSondCodeGravity` | h , c  |  |
| `MR::setMarioShadowTex` | h  |  |
| `MR::setMarioShadowVec` | e  |  |
| `MR::setMessageArg:hc` | h , c  |  |
| `MR::setMessageArg:hg` | h , g  |  |
| `MR::setMessageArgToCurrentGalaxyName` | h pCtrl, g pName |  |
| `MR::setMessageBalloonFollowOffset` | h , e  |  |
| `MR::setMicMtx` | F , c mic |  |
| `MR::setMirrorReflectionInfoFromModel` | h  |  |
| `MR::setMirrorReflectionInfoFromMtxYUp` | f  |  |
| `MR::setMovieVolume` | d , c  |  |
| `MR::setMtxAxisXYZ` | F , e , e , e  |  |
| `MR::setMtxTrans:Fddd` | F , d , d , d  |  |
| `MR::setMtxTrans:Fe` | F pMtx, e rVec |  |
| `MR::setNPCActorPos:he` | h , e  |  |
| `MR::setNPCActorPos:hg` | h , g  |  |
| `MR::setNPCActorPose` | h , e , e , e  |  |
| `MR::setNan` | E rDst |  |
| `MR::setNearZ` | d  |  |
| `MR::setNerveAtAnimStopped` | h , h , c  |  |
| `MR::setNerveAtBckStopped` | h , h  |  |
| `MR::setNerveAtPaneAnimStopped` | h , g , h , c  |  |
| `MR::setNextStageBGM` | g  |  |
| `MR::setNormalBloomBlurIntensity1` | c  |  |
| `MR::setNormalBloomBlurIntensity2` | c  |  |
| `MR::setNormalBloomIntensity` | c  |  |
| `MR::setNormalBloomThreshold` | c  |  |
| `MR::setPaneAlpha` | h , g , c  |  |
| `MR::setPaneAlphaFloat` | h , g , d  |  |
| `MR::setPaneAnimFrame` | h , g , d , c  |  |
| `MR::setPaneAnimFrameAndStop` | h , g , d , c  |  |
| `MR::setPaneAnimFrameAndStopAtEnd` | h , g , c  |  |
| `MR::setPaneAnimRate` | h , g , d , c  |  |
| `MR::setPaneRotate` | h , d , d , d , g  |  |
| `MR::setPaneScale` | h , d , d , g  |  |
| `MR::setPictureBookChapterAlreadyRead` | c  |  |
| `MR::setPlayerBaseMtx` | F  |  |
| `MR::setPlayerFrontTargetVec` | e , c  |  |
| `MR::setPlayerFrontVec` | e , c  |  |
| `MR::setPlayerJumpVec` | e  |  |
| `MR::setPlayerLinkPosAndWait` | h , g  |  |
| `MR::setPlayerPos:e` | e  |  |
| `MR::setPlayerPos:g` | g  |  |
| `MR::setPlayerPosAndWait:e` | e  |  |
| `MR::setPlayerPosAndWait:g` | g  |  |
| `MR::setPlayerPosOnGround` | g  |  |
| `MR::setPlayerPosOnGroundAndWait` | g  |  |
| `MR::setPlayerRestartIdInfo` | h  |  |
| `MR::setPlayerStateWait` |  |  |
| `MR::setPlayerSwingInhibitTimer` | c  |  |
| `MR::setPlayerSwingPermission` | b  |  |
| `MR::setPlayerUpperRotateY` | d  |  |
| `MR::setPlayerWalkingResist` | d  |  |
| `MR::setProgrammableCameraParam:geeeb` | g , e , e , e , b  |  |
| `MR::setProgrammableCameraParam:heee` | h , e , e , e  |  |
| `MR::setProgrammableCameraParamFovy:gd` | g , d  |  |
| `MR::setProgrammableCameraParamFovy:hd` | h , d  |  |
| `MR::setRaceBestTime` | c , c  |  |
| `MR::setRailCoord` | h , d  |  |
| `MR::setRailCoordSpeed` | h , d  |  |
| `MR::setRailDirectionCloseToCoord` | h , d  |  |
| `MR::setRailDirectionCloseToNearestPos` | h , e  |  |
| `MR::setRailDirectionToEnd` | h  |  |
| `MR::setRailDirectionToStart` | h  |  |
| `MR::setRandomSeedFromStageName` |  |  |
| `MR::setRasterScroll` | c , c , c  |  |
| `MR::setSeVersion` | h , c version |  |
| `MR::setSensorOffset` | h pActor, g pName, e rOffset |  |
| `MR::setSensorPos` | h pSensor, e rPos |  |
| `MR::setSensorRadius` | h pActor, g pName, d radius |  |
| `MR::setShadowDropDirection` | h , g , e  |  |
| `MR::setShadowDropDirectionPtr` | h , g , e  |  |
| `MR::setShadowDropLength` | h , g , d  |  |
| `MR::setShadowDropPosition` | h , g , e  |  |
| `MR::setShadowDropPositionAtJoint` | h , g pName, g pJointName, e  |  |
| `MR::setShadowDropPositionMtxPtr` | h , g , F , e  |  |
| `MR::setShadowDropPositionPtr` | h , g , e  |  |
| `MR::setShadowDropStartOffset` | h , g , d  |  |
| `MR::setShadowProjection` | h , g , e , e , b  |  |
| `MR::setShadowProjectionPtr` | h , g , e , e  |  |
| `MR::setShadowSurfaceOvalAlpha` | h , g , c  |  |
| `MR::setShadowVolumeBoxSize` | h , g , e  |  |
| `MR::setShadowVolumeCylinderRadius` | h , g , d  |  |
| `MR::setShadowVolumeEndDropOffset` | h , g , d  |  |
| `MR::setShadowVolumeSphereRadius` | h , g , d  |  |
| `MR::setShadowVolumeStartDropOffset` | h , g , d  |  |
| `MR::setShakeOffset` | d , d  |  |
| `MR::setSoundVolumeSetting` | c volumeSet, c maxSteps |  |
| `MR::setStageBGMState` | c state, c time |  |
| `MR::setStageBGMStateBit` | c  |  |
| `MR::setStarPointerCameraMtxAtGameScene` |  |  |
| `MR::setStarPointerDrawSyncToken` |  |  |
| `MR::setStarPointerModeBase` |  |  |
| `MR::setStarPointerTargetRadius3d` | h , d  |  |
| `MR::setTextBoxArgGameMessageRecursive` | h , g , g , c  |  |
| `MR::setTextBoxArgNumberRecursive` | h , g , c , c  |  |
| `MR::setTextBoxArgStringRecursive` | h , g , g , c  |  |
| `MR::setTextBoxFontRecursive` | h , g , h  |  |
| `MR::setTextBoxGameMessageRecursive` | h , g , g  |  |
| `MR::setTextBoxHorizontalPositionCenterRecursive` | h , g  |  |
| `MR::setTextBoxHorizontalPositionLeftRecursive` | h , g  |  |
| `MR::setTextBoxLayoutMessageRecursive` | h , g , g  |  |
| `MR::setTextBoxMessageRecursive` | h , g , g  |  |
| `MR::setTextBoxNumberRecursive` | h , g , c  |  |
| `MR::setTextBoxSystemMessageRecursive` | h , g , g  |  |
| `MR::setTextBoxVerticalPositionBottomRecursive` | h , g  |  |
| `MR::setTextBoxVerticalPositionCenterRecursive` | h , g  |  |
| `MR::setTextBoxVerticalPositionTopRecursive` | h , g  |  |
| `MR::setTicoGalaxyAlreadyTalk` | c , b  |  |
| `MR::setVelocity` | h , e  |  |
| `MR::setVelocityBlowAttack` | h , h , h , d , d , c  |  |
| `MR::setVelocityJump` | h , d  |  |
| `MR::setVelocityJumpAwayFromPlayer` | h , d , d  |  |
| `MR::setVelocitySeparateHV:hedd` | h , e , d , d  |  |
| `MR::setVelocitySeparateHV:hhhdd` | h , h , h , d , d  |  |
| `MR::setWarpPodPathFlag` | c , b  |  |
| `MR::setWipeCircleCenterPos` | e  |  |
| `MR::setYesNoSelectorSE` | g , g , g  |  |
| `MR::settingGravityParamFromPlacement` | h pGravity, h rIter |  |
| `MR::setupAlreadyDoneFlag` | g , h , C  | c |
| `MR::setupDrawForLayout` |  |  |
| `MR::setupScreen2D` |  |  |
| `MR::shakeCameraInfinity` | h , d , d  |  |
| `MR::shakeCameraNormal` |  |  |
| `MR::shakeCameraNormalStrong` |  |  |
| `MR::shakeCameraNormalWeak` |  |  |
| `MR::shakeCameraStrong` |  |  |
| `MR::shakeCameraVeryStrong` |  |  |
| `MR::shakeCameraVeryWeak` |  |  |
| `MR::shakeCameraWeak` |  |  |
| `MR::showJointAndChildrenOn` | h , h , b  |  |
| `MR::showLayout` | h  |  |
| `MR::showMaterial` | h , g  |  |
| `MR::showModel` | h  |  |
| `MR::showModelIfHidden` | h  |  |
| `MR::showPane` | h , g  |  |
| `MR::showPaneRecursive:h` | h  |  |
| `MR::showPaneRecursive:hg` | h , g  |  |
| `MR::showPlayer` |  |  |
| `MR::showPlayerJoint` | g  |  |
| `MR::showScreen` | h  |  |
| `MR::sign` | d x | d |
| `MR::simpleDraw` | h , h  |  |
| `MR::sin` | d x | d |
| `MR::sinDegree` | d x | d |
| `MR::slowDownRailCoordSpeed` | h , d  |  |
| `MR::sortSmall` | c length, D pSortArray, C pIndexArray |  |
| `MR::start2PAttackAssistSound` |  |  |
| `MR::start2PJumpAssistJustSound` |  |  |
| `MR::start2PJumpAssistSound` |  |  |
| `MR::startAction` | h , g  |  |
| `MR::startActorCameraNoTarget` | h , h , c ? | b |
| `MR::startActorCameraProgrammable` | h , c ? |  |
| `MR::startActorCameraTargetOther` | h , h , h , c  | b |
| `MR::startActorCameraTargetPlayer` | h , h , c ? | b |
| `MR::startActorCameraTargetSelf` | h , h , c ? | b |
| `MR::startAfterBossBGM` |  | h |
| `MR::startAllAnim` | h , g  |  |
| `MR::startAnim` | h , g , c  |  |
| `MR::startAnimAndSetFrameAndStop` | h , g , d , c  |  |
| `MR::startAnimAtFirstStep` | h , g , c  |  |
| `MR::startAnimCameraTargetOther` | h , h , g , h , c , d  |  |
| `MR::startAnimCameraTargetPlayer` | h , h , g , c , d  |  |
| `MR::startAnimCameraTargetSelf` | h , h , g , c , d  |  |
| `MR::startAnimReverseOneTime` | h , g , c  |  |
| `MR::startAtmosphereLevelSE` | g pName, c level?, c subLevel? | h |
| `MR::startAtmosphereSE` | g pName, c level?, c subLevel? | h |
| `MR::startBas` | h , g , b reversed, d loopStartFrame, d loopEndFrame |  |
| `MR::startBck` | h , g , g pBrkName? |  |
| `MR::startBckAtFirstStep` | h , g  |  |
| `MR::startBckIfExist` | h , g  | b |
| `MR::startBckNoInterpole` | h , g  |  |
| `MR::startBckPlayer:gc` | g , c  |  |
| `MR::startBckPlayer:gg` | g , g pBrkName? |  |
| `MR::startBckPlayer:gh` | g , h  |  |
| `MR::startBckPlayerJ` | g  |  |
| `MR::startBckWithInterpole` | h , g , c  |  |
| `MR::startBlackHoleCamera` | g , e , e  |  |
| `MR::startBlowHitSound` | h  |  |
| `MR::startBossBGM` | c  | h |
| `MR::startBpk` | h , g  |  |
| `MR::startBpkIfExist` | h , g  | b |
| `MR::startBrk` | h , g  |  |
| `MR::startBrkAndSetFrameAndStop` | h , g , d  |  |
| `MR::startBrkIfExist` | h , g  | b |
| `MR::startBtk` | h , g  |  |
| `MR::startBtkAndSetFrameAndStop` | h , g , d  |  |
| `MR::startBtkIfExist` | h , g  | b |
| `MR::startBtp` | h , g  |  |
| `MR::startBtpAndSetFrameAndStop` | h , g , d  |  |
| `MR::startBtpIfExist` | h , g  | b |
| `MR::startBva` | h , g  |  |
| `MR::startBvaIfExist` | h , g  | b |
| `MR::startCSSound` | g , g , c padChannel |  |
| `MR::startCSSound2P` | g , g  |  |
| `MR::startCameraInterpolation` | c  |  |
| `MR::startCenterScreenBlur` | c , d , c , c , c  |  |
| `MR::startCurrentStageBGM` |  |  |
| `MR::startDownWipe` |  |  |
| `MR::startEventBGM` | c  | h |
| `MR::startEventCamera` | h , g , h , c ? |  |
| `MR::startEventCameraAnim` | h , g , h , c , d  |  |
| `MR::startEventCameraNoTarget` | h , g , c ? |  |
| `MR::startEventCameraTargetPlayer` | h , g , c ? |  |
| `MR::startGalaxyCometEvent` |  |  |
| `MR::startGameOverWipe` |  |  |
| `MR::startGlobalEventCamera` | g , h , c  |  |
| `MR::startGlobalEventCameraNoTarget` | g , c ? |  |
| `MR::startGlobalEventCameraTargetPlayer` | g , c  |  |
| `MR::startLastStageBGM` |  | h |
| `MR::startLauncherCamera` | h  |  |
| `MR::startLauncherFlightCamera` | c  |  |
| `MR::startLevelSound` | h pActor, g pName, c level?, c subLevel?, c lifeTime? | h |
| `MR::startLevelSoundPlayer` | g , c ? |  |
| `MR::startLevelSoundSeKeeper` | h , g pJointName, g pSoundName |  |
| `MR::startLevelSoundSeVer` | h pActor, g pName, c level?, c subLevel?, c lifeTime? | h |
| `MR::startMissLayout` |  |  |
| `MR::startMoveAction` | h  |  |
| `MR::startMoviePlayer` | g  |  |
| `MR::startMultiActorCameraNoTarget` | h , h , g , c ? | b |
| `MR::startMultiActorCameraTargetOther` | h , h , g , h , c ? | b |
| `MR::startMultiActorCameraTargetPlayer` | h , h , g , c ? | b |
| `MR::startMultiActorCameraTargetSelf` | h , h , g , c ? | b |
| `MR::startNPCTalkCamera:hFFdc` | h , F , F , d , c  |  |
| `MR::startNPCTalkCamera:hFdc` | h , F , d , c  |  |
| `MR::startPaneAnim` | h , g , g , c  |  |
| `MR::startPaneAnimAtFirstStep` | h , g , g , c  |  |
| `MR::startPaneAnimAtStep` | h , g , g , c , c  |  |
| `MR::startPaneAnimReverseOneTime` | h , g , g , c  |  |
| `MR::startPlayerDownWipe` |  |  |
| `MR::startPlayerEvent` | g  |  |
| `MR::startPlayerTalk` | h  |  |
| `MR::startPointerFreezeLevelSound` | h  |  |
| `MR::startPointerHitSound` |  |  |
| `MR::startRemixSound` | c melodyNo, c noteNo, d beatLength |  |
| `MR::startRumbleWithShakeCameraNormalWeak` | h , g , g , d , d  |  |
| `MR::startRumbleWithShakeCameraStrong` | h , g , g , d , d  |  |
| `MR::startRumbleWithShakeCameraWeak` | h , g , g , d , d  |  |
| `MR::startSound` | h , g , c level?, c subLevel? | h |
| `MR::startSoundObject` | h pSoundObject, g pName | h |
| `MR::startSoundObjectLevel` | h pSoundObject, g pName, c lifeTime? | h |
| `MR::startSoundObjectLevelParam` | h pSoundObject, g pName, c level?, c subLevel?, c lifeTime? | h |
| `MR::startSoundPlayer` | g , c ? |  |
| `MR::startSoundPlayerJ` | g  |  |
| `MR::startSoundSeVer` | h pActor, g pName, c level?, c subLevel? | h |
| `MR::startSpinHitSound` | h  |  |
| `MR::startStageBGM` | g , b lock | h |
| `MR::startStageBGMFromStageName` | g , g , c  | h |
| `MR::startStartAnimCamera` |  |  |
| `MR::startStartPosCamera` | b  |  |
| `MR::startSubBGM` | g , b lock | h |
| `MR::startSubjectiveCamera` | c  |  |
| `MR::startSystemLevelSE` | g , c level?, c subLevel? | h |
| `MR::startSystemME` | g  |  |
| `MR::startSystemSE` | g pName, c level?, c subLevel? | h |
| `MR::startTalkCamera` | e , e , d , d , c  |  |
| `MR::startTalkSound` | c , h  |  |
| `MR::startTalkingSequence` | h  |  |
| `MR::startTalkingSequenceWithoutCinemaFrame` | h  |  |
| `MR::startTimeKeepDemo` | h , g , g  |  |
| `MR::startTimeKeepDemoMarioPuppetable` | h , g , g  |  |
| `MR::startToCaptureScreen` | g  |  |
| `MR::stopAllSound` | c  |  |
| `MR::stopAnim` | h , c  |  |
| `MR::stopBck` | h  |  |
| `MR::stopBrk` | h  |  |
| `MR::stopBtk` | h  |  |
| `MR::stopBtp` | h  |  |
| `MR::stopBva` | h  |  |
| `MR::stopMarioPinchBGMSoon` |  |  |
| `MR::stopMoviePlayer` |  |  |
| `MR::stopPaneAnim` | h , g , c  |  |
| `MR::stopPlayerFpView` |  |  |
| `MR::stopScene` | c  |  |
| `MR::stopSceneAtStep` | h , c , c  |  |
| `MR::stopSceneForDefaultHit` | c  |  |
| `MR::stopSceneForScenarioOpeningCamera` |  |  |
| `MR::stopShakingCamera` | h  |  |
| `MR::stopSound` | h pActor, g pName, c fadeTime? |  |
| `MR::stopSoundPlayer` | g , c  |  |
| `MR::stopStageBGM` | c fadeTime |  |
| `MR::stopSubBGM` | c fadeTime |  |
| `MR::stopSystemSE` | g pName, c fadeTime? |  |
| `MR::strcasecmp` | g pStr1, g pStr2 | c |
| `MR::submitLevelSE` |  |  |
| `MR::submitSE` |  |  |
| `MR::submitTrigSE` |  |  |
| `MR::suspendAllSceneNameObj` |  |  |
| `MR::syncStageSwitchAppear` | h  |  |
| `MR::syncWithFlagsAllSceneNameObj` |  |  |
| `MR::tan` | d x | d |
| `MR::tanDegree` | d x | d |
| `MR::testFpViewOutTrigger` |  | b |
| `MR::testFpViewStartTrigger` |  | b |
| `MR::testPointerMenuPadDecideTrigger` |  | b |
| `MR::testSystemPadTriggerDecide` |  | b |
| `MR::testSystemTriggerA` |  | b |
| `MR::testSystemTriggerB` |  | b |
| `MR::timeKeepDemoFadeIn` |  |  |
| `MR::timeKeepDemoFadeOut` |  |  |
| `MR::tmpMtxRotXDeg` | d  | f |
| `MR::tmpMtxRotXRad` | d  | f |
| `MR::tmpMtxRotYDeg` | d  | f |
| `MR::tmpMtxRotYRad` | d  | f |
| `MR::tmpMtxRotZDeg` | d  | f |
| `MR::tmpMtxRotZRad` | d  | f |
| `MR::tmpMtxScale` | d , d , d  | f |
| `MR::tmpMtxTrans` | e  | f |
| `MR::toDegree` | d angle | d |
| `MR::toRadian` | d angle | d |
| `MR::tornadoJumpPlayer` |  |  |
| `MR::tryBlankToFrameCinemaFrame` |  |  |
| `MR::tryChangeTalkActionRandom` | h , g , g , g  | b |
| `MR::tryCreateCollisionMoveLimit:hFh` | h , F , h  | h |
| `MR::tryCreateCollisionMoveLimit:hh` | h , h  | h |
| `MR::tryCreateCollisionSunshade:hFh` | h , F , h  | h |
| `MR::tryCreateCollisionSunshade:hh` | h , h  | h |
| `MR::tryCreateCollisionWaterSurface:hFh` | h , F , h  | h |
| `MR::tryCreateCollisionWaterSurface:hh` | h , h  | h |
| `MR::tryCreateMirrorActor` | h , g  | h |
| `MR::tryDeleteEffect` | h , g  | b |
| `MR::tryEmitEffect` | h , g  | h |
| `MR::tryEndMapPartsRotateAtPoint` | h , h , h  | b |
| `MR::tryEndStarPointerCommandStream` | h , c  | b |
| `MR::tryFindLinkNamePos:hgEE` | h , g , E , E  | b |
| `MR::tryFindLinkNamePos:hgF` | h , g , F  | b |
| `MR::tryFindNamePos:gEE` | g , E , E  | b |
| `MR::tryFindNamePos:gF` | g , F  | b |
| `MR::tryForceKillIfMsgStartPowerStarGet` | h pActor, c msg | b |
| `MR::tryForwardNode` | h  | b |
| `MR::tryFrameToBlankCinemaFrame` |  |  |
| `MR::tryFrameToScreenCinemaFrame` |  |  |
| `MR::tryGetItem` | h pSender, h pReceiver | b |
| `MR::tryInitDemoSheetTalkAnim` | h , h , g , g , h  | b |
| `MR::tryPlayerCoinPull` |  |  |
| `MR::tryPlayerDropTakingActor` |  |  |
| `MR::tryPlayerKillTakingActor` |  |  |
| `MR::tryPlayerPullActor` | h  |  |
| `MR::tryRegisterDemoActionNerve` | h , h , g  | b |
| `MR::tryRegisterDemoCast:hgh` | h , g , h  | b |
| `MR::tryRegisterDemoCast:hh` | h , h  | b |
| `MR::tryRegisterNamePosLinkObj` | h , h  | b |
| `MR::tryResumeSceneInitialize` |  | b |
| `MR::tryRumblePadAndCameraDistanceMiddle` | h , d , d , d  |  |
| `MR::tryRumblePadAndCameraDistanceStrong` | h , d , d , d  |  |
| `MR::tryRumblePadAndCameraDistanceVeryStrong` | h , d , d , d  |  |
| `MR::tryScreenToFrameCinemaFrame` |  |  |
| `MR::trySetMoveLimitCollision` | h  | b |
| `MR::trySetNerve` | h , h  | b |
| `MR::tryShowTimeoutedStarPointerGuidance` |  |  |
| `MR::tryStarPointerCheck` | h , b , g  | b |
| `MR::tryStarPointerCheckWithoutRumble` | h , b  | b |
| `MR::tryStartAction` | h , g  | b |
| `MR::tryStartAllAnim` | h , g  | b |
| `MR::tryStartBck` | h , g , g pBrkName? | b |
| `MR::tryStartBckAndBtp` | h , g , g  | b |
| `MR::tryStartDemoMarioPuppetable` | h , g  | b |
| `MR::tryStartDemoMarioPuppetableWithoutCinemaFrame` | h , g  | b |
| `MR::tryStartDemoRegistered` | h , g  | b |
| `MR::tryStartDemoRegisteredMarioPuppetable` | h , g  | b |
| `MR::tryStartDemoWithoutCinemaFrame` | h , g  | b |
| `MR::tryStartDemoWithoutCinemaFrameValidStarPointer` | h , g  | b |
| `MR::tryStartMapPartsRotateAtPoint` | h , h , h  | b |
| `MR::tryStartMapPartsRotateBetweenPoints` | h , h , h  | b |
| `MR::tryStartMoveTalkAction` | h  | b |
| `MR::tryStartMoveTurnAction` | h  | b |
| `MR::tryStartReaction` | h  | b |
| `MR::tryStartReactionAndPopNerve` | h  | b |
| `MR::tryStartReactionAndPushNerve` | h , h  | b |
| `MR::tryStartStarPointerCommandStream` | h , e , c , b  | b |
| `MR::tryStartTalkAction` | h  | b |
| `MR::tryStartTimeKeepDemo` | h , g , g  | b |
| `MR::tryStartTurnAction` | h  | b |
| `MR::tryTalkForce` | h  | b |
| `MR::tryTalkForceAndStartMoveTalkAction` | h  | b |
| `MR::tryTalkForceAtEnd` | h  | b |
| `MR::tryTalkForceAtEndAndStartTalkAction` | h  | b |
| `MR::tryTalkForceWithoutDemo` | h  | b |
| `MR::tryTalkForceWithoutDemoAtEnd` | h  | b |
| `MR::tryTalkForceWithoutDemoMarioPuppetable` | h  | b |
| `MR::tryTalkForceWithoutDemoMarioPuppetableAtEnd` | h  | b |
| `MR::tryTalkNearPlayer` | h  | b |
| `MR::tryTalkNearPlayerAndStartMoveTalkAction` | h  | b |
| `MR::tryTalkNearPlayerAndStartTalkAction` | h  | b |
| `MR::tryTalkNearPlayerAtEnd` | h  | b |
| `MR::tryTalkNearPlayerAtEndAndStartMoveTalkAction` | h  | b |
| `MR::tryTalkNearPlayerAtEndAndStartTalkAction` | h  | b |
| `MR::tryTalkRequest` | h  | b |
| `MR::tryTalkSelectLeft` | h  | b |
| `MR::tryTalkSelectRight` | h  | b |
| `MR::tryTalkTimeKeepDemo` | h  | b |
| `MR::tryTalkTimeKeepDemoMarioPuppetable` | h  | b |
| `MR::tryTalkTimeKeepDemoWithoutPauseMarioPuppetable` | h  | b |
| `MR::tryToUpdatePlayerRestartIdInfo` | e  |  |
| `MR::tryUpdateHitSensorsAll` | h pActor | b |
| `MR::turnDirection` | h , E , e , d  |  |
| `MR::turnDirectionAndGravityH:Ihedd` | I , h , e , d , d  |  |
| `MR::turnDirectionAndGravityH:hedd` | h , e , d , d  |  |
| `MR::turnDirectionDegree` | h , E , e , d  |  |
| `MR::turnDirectionFromPlayerDegree` | h , E , d  |  |
| `MR::turnDirectionFromPlayerDegreeHorizon` | h , E , d  |  |
| `MR::turnDirectionFromTargetDegree` | h , E , e , d  |  |
| `MR::turnDirectionFromTargetDegreeHorizon` | h , E , e , d  |  |
| `MR::turnDirectionToGround` | h , E  |  |
| `MR::turnDirectionToPlayerDegree` | h , E , d  |  |
| `MR::turnDirectionToPlayerDegreeHorizon` | h , E , d  |  |
| `MR::turnDirectionToTarget` | h , E , e , d  |  |
| `MR::turnDirectionToTargetDegree` | h , E , e , d  |  |
| `MR::turnDirectionToTargetDegreeHorizon` | h , E , e , d  |  |
| `MR::turnDirectionToTargetUseGroundNormalDegree` | h , E , e , d  | b |
| `MR::turnMtxToLocalDirDegree` | F , e , e , d  |  |
| `MR::turnMtxToXDirDegree` | F , e , d  |  |
| `MR::turnMtxToXDirRate` | F , e , d  |  |
| `MR::turnMtxToYDirDegree` | F , e , d  |  |
| `MR::turnMtxToYDirRate` | F , e , d  |  |
| `MR::turnOffDOFInSubjective` |  |  |
| `MR::turnOffImageEffect` |  |  |
| `MR::turnOnDOFInSubjective` |  |  |
| `MR::turnOnDepthOfField` | b  |  |
| `MR::turnOnNormalBloom` |  |  |
| `MR::turnPlayerToActor` | h , d  |  |
| `MR::turnQuat` | I pDst, i rSrc, e rFrom, e rTo, d angle | b |
| `MR::turnQuatUpToGravity` | I , i , h  |  |
| `MR::turnQuatXDirRad` | I pDst, i rSrc, e rTo, d angle | b |
| `MR::turnQuatXDirRate` | I pDst, i rSrc, e rTo, d rate |  |
| `MR::turnQuatYDirRad` | I pDst, i rSrc, e rTo, d angle | b |
| `MR::turnQuatYDirRate` | I pDst, i rSrc, e rTo, d rate |  |
| `MR::turnQuatZDirRad` | I pDst, i rSrc, e rTo, d angle | b |
| `MR::turnRandomVector` | E pDst, e rSrc, d range |  |
| `MR::turnVecToPlane` | E pDst, e rSrc, e rNorm | d |
| `MR::turnVecToVecCos` | E pDst, e rFrom, e rTo, d cosAngle, e rAxis, d fallbackLength? | b |
| `MR::turnVecToVecCosOnPlane:Eeed` | E pVec, e rTo, e rAxis, d cosAngle | b |
| `MR::turnVecToVecCosOnPlane:Eeeed` | E pDst, e rFrom, e rTo, e rAxis, d cosAngle | b |
| `MR::turnVecToVecDegree` | E pDst, e rFrom, e rTo, d angle, e rAxis | d |
| `MR::turnVecToVecRadian` | E pDst, e rFrom, e rTo, d angle, e rAxis | d |
| `MR::unlockPlayerAnimation` |  |  |
| `MR::unlockStageBGM` |  |  |
| `MR::updateAlreadyDoneFlag` | c , c  |  |
| `MR::updateBindPositionAndVelocity` | E pPosition, E pVelocity, h rResult, d factor |  |
| `MR::updateClearTimeTextBox` | h , g , c  |  |
| `MR::updateEffectFloorCode` | h , h  |  |
| `MR::updateEffectFloorCodeLineToMap:hdd` | h , d endDist, d startDist |  |
| `MR::updateEffectFloorCodeLineToMap:hee` | h , e rPosition, e rDirection |  |
| `MR::updateHitSensorsAll` | h pActor |  |
| `MR::updateLightCtrl` | h  |  |
| `MR::updateLightCtrlDirect` | h  |  |
| `MR::updateMaterial` | h  |  |
| `MR::updateMinuteAndSecondTextBox` | h , g , c  |  |
| `MR::updateModelAnimPlayer` | h  |  |
| `MR::updateModelDiffMaterials` | h  |  |
| `MR::updateModelManager` | h  |  |
| `MR::updateStarPointerModeWhileSceneHeld` |  |  |
| `MR::updateStorageSpinDriverPathDrawRange` | c , d  |  |
| `MR::useStageSwitchReadA` | h , h  | b |
| `MR::useStageSwitchReadAppear` | h , h  | b |
| `MR::useStageSwitchReadB` | h , h  | b |
| `MR::useStageSwitchSleep` | h , h  |  |
| `MR::useStageSwitchWriteA` | h , h  | b |
| `MR::useStageSwitchWriteB` | h , h  | b |
| `MR::useStageSwitchWriteDead` | h , h  | b |
| `MR::validateClipping` | h  |  |
| `MR::validateExCollisionParts` | h  |  |
| `MR::validateHitSensor` | h pActor, g pName |  |
| `MR::validateHitSensors` | h pActor |  |
| `MR::validatePlayerSensor` |  |  |
| `MR::validatePurpleCoinCounter` |  |  |
| `MR::validateShadow` | h , g  |  |
| `MR::validateShadowAll` | h  |  |
| `MR::validateShadowGroup` | h , g  |  |
| `MR::vecBlend` | e rFrom, e rTo, E pDst, d rate |  |
| `MR::vecBlendNormal` | e rFrom, e rTo, E pVec, d rate |  |
| `MR::vecBlendSphere` | e rFrom, e rTo, E pVec, d rate | b |
| `MR::vecKillElement` | e rSrc, e rKillDir, E pDst | d |
| `MR::vecRotAxis` | e rFrom, e rTo, e rAxis, E pVec, d angle |  |
| `MR::vecScaleAdd` | E pDst, e pAdd, d scale |  |
| `MR::zeroVelocity` | h  |  |
| `MR::zoomInTargetGameCamera` |  |  |
| `MR::zoomOutTargetGameCamera` |  |  |
| `MapObjActor::appear` | h self (MapObjActor) |  |
| `MapObjActor::calcAndSetBaseMtx` | h self (MapObjActor) |  |
| `MapObjActor::connectToScene` | h self (MapObjActor), h rInfo |  |
| `MapObjActor::control` | h self (MapObjActor) |  |
| `MapObjActor::endClipped` | h self (MapObjActor) |  |
| `MapObjActor::endMapPartsFunctions` | h self (MapObjActor) |  |
| `MapObjActor::exeDone` | h self (MapObjActor) |  |
| `MapObjActor::exeMove` | h self (MapObjActor) |  |
| `MapObjActor::exeWait` | h self (MapObjActor) |  |
| `MapObjActor::init` | h self (MapObjActor), h rIter |  |
| `MapObjActor::initAfterPlacement` | h self (MapObjActor) |  |
| `MapObjActor::initCaseNoUseSwitchA` | h self (MapObjActor), h rInfo |  |
| `MapObjActor::initCaseNoUseSwitchB` | h self (MapObjActor), h rInfo |  |
| `MapObjActor::initCaseUseSwitchA` | h self (MapObjActor), h rInfo |  |
| `MapObjActor::initCaseUseSwitchB` | h self (MapObjActor), h rInfo |  |
| `MapObjActor::initialize` | h self (MapObjActor), h rIter, h rInfo |  |
| `MapObjActor::isObjectName` | h self (MapObjActor), g pName | b |
| `MapObjActor::kill` | h self (MapObjActor) |  |
| `MapObjActor::makeSubModels` | h self (MapObjActor), h , h  |  |
| `MapObjActor::pauseMapPartsFunctions` | h self (MapObjActor) |  |
| `MapObjActor::setStateWait` | h self (MapObjActor) |  |
| `MapObjActor::startClipped` | h self (MapObjActor) |  |
| `MapObjActor::startMapPartsFunctions` | h self (MapObjActor) |  |
| `MapObjActor::tryCreateBreakModel` | h self (MapObjActor), h rInfo | b |
| `MapObjActor::tryDeleteWaitEffect` | h self (MapObjActor) | b |
| `MapObjActor::tryEmitWaitEffect` | h self (MapObjActor) | b |
| `MapObjActor::updateProjmapMtx` | h self (MapObjActor) |  |
| `MarioActor::IsMarioSwimming` | h self (MarioActor) | b |
| `MarioActor::MarioStopWalk` | h self (MarioActor) |  |
| `MarioActor::addRushSensor` | h self (MarioActor), h , b  |  |
| `MarioActor::attackOrPushPolygons` | h self (MarioActor) |  |
| `MarioActor::attackOrPushSensor` | h self (MarioActor), h , d  |  |
| `MarioActor::attackOrPushSensorInDamage` | h self (MarioActor), h , d  |  |
| `MarioActor::attackOrPushSensorInRush` | h self (MarioActor), h , d  |  |
| `MarioActor::attackSensor` | h self (MarioActor), h , h  |  |
| `MarioActor::beginRush` | h self (MarioActor) |  |
| `MarioActor::binderFilter` | h self (MarioActor), h  | b |
| `MarioActor::bodyClap` | h self (MarioActor) |  |
| `MarioActor::calc1stPersonView` | h self (MarioActor) |  |
| `MarioActor::calcAndSetBaseMtx` | h self (MarioActor) |  |
| `MarioActor::calcAnim` | h self (MarioActor) |  |
| `MarioActor::calcAnimInMovement` | h self (MarioActor) |  |
| `MarioActor::calcBaseFrontVec` | h self (MarioActor), e  |  |
| `MarioActor::calcCenterPos` | h self (MarioActor) |  |
| `MarioActor::calcCylinderToCenter` | h self (MarioActor), h , E  | d |
| `MarioActor::calcFogLighting` | h self (MarioActor) |  |
| `MarioActor::calcHeadPos` | h self (MarioActor) |  |
| `MarioActor::calcScreenBoxRange` | h self (MarioActor) |  |
| `MarioActor::calcSpinEffect` | h self (MarioActor) |  |
| `MarioActor::calcViewAndEntry` | h self (MarioActor) |  |
| `MarioActor::calcViewBlurModel` | h self (MarioActor) |  |
| `MarioActor::calcViewFootPrint` | h self (MarioActor) |  |
| `MarioActor::calcViewMainModel` | h self (MarioActor) |  |
| `MarioActor::calcViewReflectionModel` | h self (MarioActor) |  |
| `MarioActor::calcViewSearchLight` | h self (MarioActor) |  |
| `MarioActor::calcViewSilhouetteModel` | h self (MarioActor) |  |
| `MarioActor::calcViewWallShadowModel` | h self (MarioActor) |  |
| `MarioActor::canTakePowerUp` | h self (MarioActor), c playerMode | b |
| `MarioActor::captureScreenBox` | h self (MarioActor) |  |
| `MarioActor::careMovingObject` | h self (MarioActor) |  |
| `MarioActor::changeAnimation` | h self (MarioActor), g , g ? |  |
| `MarioActor::changeAnimationNonStop` | h self (MarioActor), g  |  |
| `MarioActor::changeAnimationUpper` | h self (MarioActor), g  |  |
| `MarioActor::changeDisplayMode` | h self (MarioActor), c  |  |
| `MarioActor::changeGameOverAnimation` | h self (MarioActor) |  |
| `MarioActor::changeHandMaterial` | h self (MarioActor) |  |
| `MarioActor::changeMaxLife` | h self (MarioActor), c  |  |
| `MarioActor::changeMorphString` | h self (MarioActor), g  | g |
| `MarioActor::changeNullAnimation` | h self (MarioActor), g , c  |  |
| `MarioActor::changeSpecialModeAnimation` | h self (MarioActor), g  |  |
| `MarioActor::changeTeresaAnimation` | h self (MarioActor), g , c  |  |
| `MarioActor::checkAndTryTrampleAttack` | h self (MarioActor), h , d , b  | b |
| `MarioActor::checkBeeCeilStick` | h self (MarioActor), E  | b |
| `MarioActor::checkBeeFloorStick` | h self (MarioActor), E  | b |
| `MarioActor::checkBeeWallStick` | h self (MarioActor), E  | b |
| `MarioActor::checkButtonType` | h self (MarioActor), c , b  | b |
| `MarioActor::checkClapCatchStart` | h self (MarioActor) | b |
| `MarioActor::checkEffectWaterColumn` | h self (MarioActor) | b |
| `MarioActor::checkPriorRushTarget` | h self (MarioActor) |  |
| `MarioActor::clearNullAnimation` | h self (MarioActor), c  |  |
| `MarioActor::control` | h self (MarioActor) |  |
| `MarioActor::control2` | h self (MarioActor) |  |
| `MarioActor::controlMain` | h self (MarioActor) |  |
| `MarioActor::copyMaterial` | h self (MarioActor), h , c , c  |  |
| `MarioActor::createIceFloor:he` | h self (MarioActor), e  |  |
| `MarioActor::createIceFloor:hee` | h self (MarioActor), e , e  |  |
| `MarioActor::createIceWall` | h self (MarioActor), e , e  |  |
| `MarioActor::createRainbowMesh` | h self (MarioActor) |  |
| `MarioActor::cylinderHitCheck` | h self (MarioActor), h , E  | b |
| `MarioActor::cylinderHorizontalCheck` | h self (MarioActor), h  | b |
| `MarioActor::cylinderPushCheck` | h self (MarioActor), e , d , d , d  | b |
| `MarioActor::damageDropThrowMemoSensor` | h self (MarioActor) |  |
| `MarioActor::decLife` | h self (MarioActor), c  |  |
| `MarioActor::decLifeLarge` | h self (MarioActor) |  |
| `MarioActor::decLifeMiddle` | h self (MarioActor) |  |
| `MarioActor::decLifeSmall` | h self (MarioActor) |  |
| `MarioActor::decideShadowMode` | h self (MarioActor) |  |
| `MarioActor::doFreezeAttack:h` | h self (MarioActor) |  |
| `MarioActor::doFreezeAttack:hh` | h self (MarioActor), h  | b |
| `MarioActor::doPressing` | h self (MarioActor) | b |
| `MarioActor::doRush` | h self (MarioActor) | b |
| `MarioActor::doStun` | h self (MarioActor) | b |
| `MarioActor::doTrampleJump` | h self (MarioActor), h  |  |
| `MarioActor::draw` | h self (MarioActor) |  |
| `MarioActor::drawColdWaterDamage` | h self (MarioActor) |  |
| `MarioActor::drawDarkMask` | h self (MarioActor) | b |
| `MarioActor::drawHand` | h self (MarioActor) |  |
| `MarioActor::drawIndirect` | h self (MarioActor) |  |
| `MarioActor::drawIndirectHand` | h self (MarioActor) |  |
| `MarioActor::drawIndirectModel` | h self (MarioActor) |  |
| `MarioActor::drawLifeUp` | h self (MarioActor) |  |
| `MarioActor::drawMarioModel` | h self (MarioActor) |  |
| `MarioActor::drawModelBlur` | h self (MarioActor) |  |
| `MarioActor::drawMosaic` | h self (MarioActor) |  |
| `MarioActor::drawPreWipe` | h self (MarioActor) |  |
| `MarioActor::drawRasterScroll` | h self (MarioActor), d , c , d  |  |
| `MarioActor::drawReflectModel` | h self (MarioActor) |  |
| `MarioActor::drawScreenBlend` | h self (MarioActor) |  |
| `MarioActor::drawSearchLight` | h self (MarioActor) |  |
| `MarioActor::drawShadow` | h self (MarioActor) |  |
| `MarioActor::drawSilhouette` | h self (MarioActor) |  |
| `MarioActor::drawSphereMask` | h self (MarioActor) |  |
| `MarioActor::drawSpinEffect` | h self (MarioActor) |  |
| `MarioActor::drawSpinInhibit` | h self (MarioActor) |  |
| `MarioActor::drawWallShade` | h self (MarioActor), e , e , d  |  |
| `MarioActor::emitEffectWaterColumn` | h self (MarioActor), e , e  |  |
| `MarioActor::endRush` | h self (MarioActor), h  |  |
| `MarioActor::entryWallWalkMode` | h self (MarioActor), e , e  |  |
| `MarioActor::exeGameOver` | h self (MarioActor) |  |
| `MarioActor::exeGameOverAbyss` | h self (MarioActor) |  |
| `MarioActor::exeGameOverBlackHole2` | h self (MarioActor) |  |
| `MarioActor::exeGameOverFire` | h self (MarioActor) |  |
| `MarioActor::exeGameOverNonStop` | h self (MarioActor) |  |
| `MarioActor::exeGameOverSink` | h self (MarioActor) |  |
| `MarioActor::exeTimeWait` | h self (MarioActor) |  |
| `MarioActor::exeWait` | h self (MarioActor) |  |
| `MarioActor::finalizeFreezeModel` | h self (MarioActor) | b |
| `MarioActor::flushCoinPull` | h self (MarioActor) |  |
| `MarioActor::forceGameOver` | h self (MarioActor) |  |
| `MarioActor::forceGameOverAbyss` | h self (MarioActor) |  |
| `MarioActor::forceGameOverBlackHole` | h self (MarioActor) |  |
| `MarioActor::forceGameOverNonStop` | h self (MarioActor) |  |
| `MarioActor::forceGameOverSink` | h self (MarioActor) |  |
| `MarioActor::forceKill` | h self (MarioActor), c  |  |
| `MarioActor::forceSetBaseMtx` | h self (MarioActor), F  |  |
| `MarioActor::getAirGravityVec` | h self (MarioActor) | e |
| `MarioActor::getCarrySensor` | h self (MarioActor) | h |
| `MarioActor::getConst` | h self (MarioActor) | h |
| `MarioActor::getDrawStates` | h self (MarioActor) | h |
| `MarioActor::getFaceLookHeight` | h self (MarioActor), g  | d |
| `MarioActor::getFloorMaterialIndex` | h self (MarioActor), c  | c |
| `MarioActor::getFrontVec` | h self (MarioActor), E  |  |
| `MarioActor::getGlobalJointMtx` | h self (MarioActor), g  | f |
| `MarioActor::getGravityInfo` | h self (MarioActor) | h |
| `MarioActor::getGravityLevel` | h self (MarioActor) | c |
| `MarioActor::getGravityRatio` | h self (MarioActor) | d |
| `MarioActor::getGravityVec` | h self (MarioActor) | e |
| `MarioActor::getGravityVector:h` | h self (MarioActor) | e |
| `MarioActor::getGravityVector:hE` | h self (MarioActor), E  |  |
| `MarioActor::getGroundPos` | h self (MarioActor), E dst |  |
| `MarioActor::getHealth` | h self (MarioActor) | c |
| `MarioActor::getHighPriorityTarget` | h self (MarioActor) | h |
| `MarioActor::getJointCtrl` | h self (MarioActor), g  | h |
| `MarioActor::getLastMove:h` | h self (MarioActor) | e |
| `MarioActor::getLastMove:hE` | h self (MarioActor), E  |  |
| `MarioActor::getLookTargetSensor` | h self (MarioActor) | h |
| `MarioActor::getMario` | h self (MarioActor) | h |
| `MarioActor::getModel` | h self (MarioActor) | h |
| `MarioActor::getModelData` | h self (MarioActor) | h |
| `MarioActor::getMovementStates` | h self (MarioActor) | h |
| `MarioActor::getNearestJumpTarget` | h self (MarioActor) | h |
| `MarioActor::getNearestRushTarget` | h self (MarioActor), b  | h |
| `MarioActor::getPrevDrawStates` | h self (MarioActor) | h |
| `MarioActor::getRealMtx` | h self (MarioActor), F , g  |  |
| `MarioActor::getRealPos` | h self (MarioActor), g , E  |  |
| `MarioActor::getRescueTarget` | h self (MarioActor) | h |
| `MarioActor::getResourceTable` | h self (MarioActor) | h |
| `MarioActor::getShadowPos` | h self (MarioActor) | e |
| `MarioActor::getSideVec` | h self (MarioActor), E  |  |
| `MarioActor::getSimpleModel` | h self (MarioActor) | h |
| `MarioActor::getSpecialMode` | h self (MarioActor) | c |
| `MarioActor::getStickValue` | h self (MarioActor), D , D  |  |
| `MarioActor::getThrowVec` | h self (MarioActor), E  |  |
| `MarioActor::getTransForCamera` | h self (MarioActor) | e |
| `MarioActor::getUpVec` | h self (MarioActor), E  |  |
| `MarioActor::hideBeeFur` | h self (MarioActor) |  |
| `MarioActor::hideFreezeModel` | h self (MarioActor) |  |
| `MarioActor::incLife` | h self (MarioActor), c  |  |
| `MarioActor::init` | h self (MarioActor), h  |  |
| `MarioActor::init2` | h self (MarioActor), e , e , c  |  |
| `MarioActor::init2D` | h self (MarioActor) |  |
| `MarioActor::initActionMatrix` | h self (MarioActor) |  |
| `MarioActor::initAfterOpeningDemo` | h self (MarioActor) |  |
| `MarioActor::initAfterPlacement` | h self (MarioActor) |  |
| `MarioActor::initBeeMario` | h self (MarioActor) |  |
| `MarioActor::initBlackHoleOut` | h self (MarioActor) |  |
| `MarioActor::initBlink` | h self (MarioActor) |  |
| `MarioActor::initBlur` | h self (MarioActor) |  |
| `MarioActor::initBoneMario` | h self (MarioActor) |  |
| `MarioActor::initCommonEffect` | h self (MarioActor) |  |
| `MarioActor::initDarkMask` | h self (MarioActor) |  |
| `MarioActor::initDrawAndModel` | h self (MarioActor) |  |
| `MarioActor::initEffect` | h self (MarioActor) |  |
| `MarioActor::initFace` | h self (MarioActor) |  |
| `MarioActor::initFireBall` | h self (MarioActor) |  |
| `MarioActor::initForJump` | h self (MarioActor) |  |
| `MarioActor::initHand` | h self (MarioActor) |  |
| `MarioActor::initHopperMario` | h self (MarioActor) |  |
| `MarioActor::initIceMario` | h self (MarioActor) |  |
| `MarioActor::initInvincibleMario` | h self (MarioActor) |  |
| `MarioActor::initMaterialEffect` | h self (MarioActor) |  |
| `MarioActor::initMember` | h self (MarioActor) |  |
| `MarioActor::initModPowerUpModels` | h self (MarioActor) |  |
| `MarioActor::initMorphStringTable` | h self (MarioActor) |  |
| `MarioActor::initParts` | h self (MarioActor) |  |
| `MarioActor::initScouter` | h self (MarioActor) |  |
| `MarioActor::initScreenBox` | h self (MarioActor) |  |
| `MarioActor::initSearchLight` | h self (MarioActor) |  |
| `MarioActor::initShadow` | h self (MarioActor) |  |
| `MarioActor::initTeresaMario` | h self (MarioActor) |  |
| `MarioActor::initTeresaMarioAnimation` | h self (MarioActor) |  |
| `MarioActor::initThrowing` | h self (MarioActor) |  |
| `MarioActor::initTornadoMario` | h self (MarioActor) |  |
| `MarioActor::isActionOk` | h self (MarioActor), g  | b |
| `MarioActor::isAllHidden` | h self (MarioActor) | b |
| `MarioActor::isAnimationRun` | h self (MarioActor), g  | b |
| `MarioActor::isBeeMarioOnFur` | h self (MarioActor) | b |
| `MarioActor::isBeeWallWalk` | h self (MarioActor) | b |
| `MarioActor::isCameraStateOn` | h self (MarioActor), c  | b |
| `MarioActor::isCommonEffect` | h self (MarioActor), g  | b |
| `MarioActor::isDamaging` | h self (MarioActor) | b |
| `MarioActor::isEnableMoveMario` | h self (MarioActor) | b |
| `MarioActor::isEnableNerveChange` | h self (MarioActor) | b |
| `MarioActor::isEnableSpinPunch` | h self (MarioActor) | b |
| `MarioActor::isFastDrop` | h self (MarioActor) | b |
| `MarioActor::isFastRise` | h self (MarioActor) | b |
| `MarioActor::isFixJumpRushSensor` | h self (MarioActor), h  | b |
| `MarioActor::isHidden` | h self (MarioActor) | b |
| `MarioActor::isInPunchTimerRange` | h self (MarioActor) | b |
| `MarioActor::isInZeroGravitySpot` | h self (MarioActor) | b |
| `MarioActor::isItemSwinging` | h self (MarioActor) | b |
| `MarioActor::isJumpRising` | h self (MarioActor) | b |
| `MarioActor::isJumping` | h self (MarioActor) | b |
| `MarioActor::isKeepJump` | h self (MarioActor) | b |
| `MarioActor::isKeepJump2P` | h self (MarioActor) | b |
| `MarioActor::isLandEffectRushSensor` | h self (MarioActor), h  | b |
| `MarioActor::isLongDrop` | h self (MarioActor) | b |
| `MarioActor::isMaterialEffect` | h self (MarioActor), g  | b |
| `MarioActor::isNeedDamageFog` | h self (MarioActor) | b |
| `MarioActor::isPunching` | h self (MarioActor) | b |
| `MarioActor::isRefuseTalk` | h self (MarioActor) | b |
| `MarioActor::isRequestHipDrop` | h self (MarioActor) | b |
| `MarioActor::isRequestJump` | h self (MarioActor) | b |
| `MarioActor::isRequestJump2P` | h self (MarioActor) | b |
| `MarioActor::isRequestRush` | h self (MarioActor) | b |
| `MarioActor::isRequestSpin` | h self (MarioActor) | b |
| `MarioActor::isRequestSpinJump2P` | h self (MarioActor) | b |
| `MarioActor::isRequestThrow` | h self (MarioActor) | b |
| `MarioActor::isSleeping` | h self (MarioActor) | b |
| `MarioActor::isStaggering` | h self (MarioActor) | b |
| `MarioActor::isStopNullAnimation` | h self (MarioActor) | b |
| `MarioActor::isTurning` | h self (MarioActor) | b |
| `MarioActor::isUnderTarget` | h self (MarioActor), h  | b |
| `MarioActor::isUseScreenBox` | h self (MarioActor) | b |
| `MarioActor::jumpHop` | h self (MarioActor) |  |
| `MarioActor::lockOnPointer` | h self (MarioActor) |  |
| `MarioActor::memorizeSensorThrow` | h self (MarioActor), h  |  |
| `MarioActor::movement` | h self (MarioActor) |  |
| `MarioActor::offTakingFlag` | h self (MarioActor) |  |
| `MarioActor::playCommonEffect` | h self (MarioActor), g  | h |
| `MarioActor::playEffect` | h self (MarioActor), g  | h |
| `MarioActor::playEffectRT` | h self (MarioActor), g , e , e  |  |
| `MarioActor::playEffectRTW` | h self (MarioActor), g , e , e  |  |
| `MarioActor::playEffectRTZ` | h self (MarioActor), g , e , e  |  |
| `MarioActor::playEffectSRT` | h self (MarioActor), g , d , e , e  |  |
| `MarioActor::playEffectTrans` | h self (MarioActor), g , e  |  |
| `MarioActor::playMaterialEffect` | h self (MarioActor), g  | h |
| `MarioActor::playSound` | h self (MarioActor), g , c  |  |
| `MarioActor::printHitMark` | h self (MarioActor), h  |  |
| `MarioActor::reactionPunch` | h self (MarioActor), h  |  |
| `MarioActor::receiveMsgEnemyAttack` | h self (MarioActor), c , h , h  | b |
| `MarioActor::receiveMsgPush` | h self (MarioActor), h , h  | b |
| `MarioActor::receiveMsgTaken` | h self (MarioActor), h , h  | b |
| `MarioActor::receiveOtherMsg` | h self (MarioActor), c , h , h  | b |
| `MarioActor::recordScoutingObject` | h self (MarioActor), h  |  |
| `MarioActor::releaseThrowMemoSensor` | h self (MarioActor) | b |
| `MarioActor::resetCondition` | h self (MarioActor) |  |
| `MarioActor::resetFog` | h self (MarioActor) |  |
| `MarioActor::resetPadSwing` | h self (MarioActor) |  |
| `MarioActor::resetPlayerModeOnDamage` | h self (MarioActor) |  |
| `MarioActor::resetPlayerModeOnNoDamage` | h self (MarioActor) |  |
| `MarioActor::resetSensorCount` | h self (MarioActor) |  |
| `MarioActor::resetWaterLife` | h self (MarioActor) |  |
| `MarioActor::runTeresaBaseAnimation` | h self (MarioActor) |  |
| `MarioActor::rushDropThrowMemoSensor` | h self (MarioActor) |  |
| `MarioActor::scaleMtx` | h self (MarioActor), F  |  |
| `MarioActor::selectAction` | h self (MarioActor), g  | c |
| `MarioActor::selectAutoBind` | h self (MarioActor), g , C  | b |
| `MarioActor::selectCustomEffectSpinHitSound` | h self (MarioActor), g  | b |
| `MarioActor::selectDamageFireColor` | h self (MarioActor), g  | b |
| `MarioActor::selectDamagePop` | h self (MarioActor), h  | b |
| `MarioActor::selectHandyRush` | h self (MarioActor), h  | b |
| `MarioActor::selectHideFlyMeter` | h self (MarioActor), h  | b |
| `MarioActor::selectHipDropRush` | h self (MarioActor), h  | b |
| `MarioActor::selectHomingInSuperHipDrop` | h self (MarioActor), g  | b |
| `MarioActor::selectInvalidMovingCollision` | h self (MarioActor), g  | b |
| `MarioActor::selectJumpRushSensor` | h self (MarioActor), g  | b |
| `MarioActor::selectLandEffect` | h self (MarioActor), h  | b |
| `MarioActor::selectNoShadow` | h self (MarioActor), h  | b |
| `MarioActor::selectNotHomingSensor` | h self (MarioActor), h  | b |
| `MarioActor::selectOnDamageRush` | h self (MarioActor), g  | b |
| `MarioActor::selectPushOff` | h self (MarioActor), h  | b |
| `MarioActor::selectQuickResetBeeWallGravity` | h self (MarioActor), g  | b |
| `MarioActor::selectRebindTimer` | h self (MarioActor), h  | b |
| `MarioActor::selectRecoverFlyMeter` | h self (MarioActor), h  | b |
| `MarioActor::selectSpinCatchInRush` | h self (MarioActor), g  | b |
| `MarioActor::selectTeresaThru` | h self (MarioActor), h  | b |
| `MarioActor::selectTiltPress` | h self (MarioActor), h  | b |
| `MarioActor::selectWaterInOutEffect` | h self (MarioActor), g  | b |
| `MarioActor::selectWaterInOutRush` | h self (MarioActor), h  | b |
| `MarioActor::sendBodyAttack` | h self (MarioActor), h  | b |
| `MarioActor::sendMsgToSensor` | h self (MarioActor), h , c  | b |
| `MarioActor::sendMsgUpperPunch` | h self (MarioActor), h  | b |
| `MarioActor::sendPunch` | h self (MarioActor), h , b  | b |
| `MarioActor::sendWallTouch` | h self (MarioActor), h , h  |  |
| `MarioActor::setAnimationFrozen` | h self (MarioActor), b frozen |  |
| `MarioActor::setBlendMtxTimer` | h self (MarioActor), c  |  |
| `MarioActor::setBlink` | h self (MarioActor), g  |  |
| `MarioActor::setPlayerMode` | h self (MarioActor), c , b  |  |
| `MarioActor::setPress` | h self (MarioActor), c , c  |  |
| `MarioActor::setPunchHitTimer` | h self (MarioActor), c  |  |
| `MarioActor::setRasterScroll` | h self (MarioActor), c , c , c  |  |
| `MarioActor::setUpperRotateY` | h self (MarioActor), d  |  |
| `MarioActor::settingRush` | h self (MarioActor) |  |
| `MarioActor::setupSensors` | h self (MarioActor) |  |
| `MarioActor::shootFireBall` | h self (MarioActor) |  |
| `MarioActor::showBeeFur` | h self (MarioActor) |  |
| `MarioActor::showFreezeModel` | h self (MarioActor) |  |
| `MarioActor::showJointByPlugin` | h self (MarioActor), g pName, b show |  |
| `MarioActor::stopAnimation` | h self (MarioActor), g  |  |
| `MarioActor::stopCommonEffect` | h self (MarioActor), g  |  |
| `MarioActor::stopEffect` | h self (MarioActor), g  |  |
| `MarioActor::stopEffectForce` | h self (MarioActor), g  |  |
| `MarioActor::stopMaterialEffect` | h self (MarioActor), g  |  |
| `MarioActor::stopSpinTicoEffect` | h self (MarioActor), b  |  |
| `MarioActor::swapTexture` | h self (MarioActor), g , c  |  |
| `MarioActor::swapTextureInit` | h self (MarioActor) |  |
| `MarioActor::syncJumpBeeStickMode` | h self (MarioActor) |  |
| `MarioActor::takeSensor` | h self (MarioActor), h  | b |
| `MarioActor::touchSensor` | h self (MarioActor), h  |  |
| `MarioActor::trampleJump` | h self (MarioActor), d , d  |  |
| `MarioActor::tryAddClapCoin` | h self (MarioActor), h  |  |
| `MarioActor::tryAttackMsg` | h self (MarioActor), c , h , B  | b |
| `MarioActor::tryCoinPull` | h self (MarioActor) |  |
| `MarioActor::tryCoinPullInRush` | h self (MarioActor) |  |
| `MarioActor::tryCoinPullOne` | h self (MarioActor), h  | b |
| `MarioActor::tryCounterJetAttack` | h self (MarioActor), h  |  |
| `MarioActor::tryGetItem` | h self (MarioActor), h  | b |
| `MarioActor::tryHipDropAttack` | h self (MarioActor), h  | b |
| `MarioActor::tryJetAttack` | h self (MarioActor), h  | b |
| `MarioActor::tryJumpRush` | h self (MarioActor) | b |
| `MarioActor::tryPullTrans` | h self (MarioActor), E , e  |  |
| `MarioActor::tryPunchAirAfter` | h self (MarioActor), h  | b |
| `MarioActor::tryReleaseBombTeresa` | h self (MarioActor) |  |
| `MarioActor::tryReleaseDirect` | h self (MarioActor), h  |  |
| `MarioActor::tryReleaseWithMsg` | h self (MarioActor), c  |  |
| `MarioActor::tryRushInRush` | h self (MarioActor) |  |
| `MarioActor::trySetLockOnTarget` | h self (MarioActor), h  |  |
| `MarioActor::trySpinPunch` | h self (MarioActor) | b |
| `MarioActor::tryStandardRush` | h self (MarioActor) | b |
| `MarioActor::tryStartRush` | h self (MarioActor), b  | b |
| `MarioActor::tryThrow` | h self (MarioActor) | b |
| `MarioActor::tryTornadoAttack` | h self (MarioActor), h  | b |
| `MarioActor::tryTornadoPull` | h self (MarioActor), h  |  |
| `MarioActor::tryTrampleAttack` | h self (MarioActor), h  | b |
| `MarioActor::tryVectorAttackMsg` | h self (MarioActor), c , e  | b |
| `MarioActor::update2D` | h self (MarioActor) |  |
| `MarioActor::updateActionTrigger` | h self (MarioActor) |  |
| `MarioActor::updateAlphaList` | h self (MarioActor), c  |  |
| `MarioActor::updateBaseMtxTeresa` | h self (MarioActor), F  |  |
| `MarioActor::updateBaseScaleMtx` | h self (MarioActor) |  |
| `MarioActor::updateBeeModeGravity` | h self (MarioActor), E  |  |
| `MarioActor::updateBeeStickMode` | h self (MarioActor), E  |  |
| `MarioActor::updateBeeWingAnimation` | h self (MarioActor) |  |
| `MarioActor::updateBehavior` | h self (MarioActor) |  |
| `MarioActor::updateBindRatio` | h self (MarioActor) |  |
| `MarioActor::updateBlink` | h self (MarioActor) |  |
| `MarioActor::updateCameraInfo` | h self (MarioActor) |  |
| `MarioActor::updateControllerSwing` | h self (MarioActor) |  |
| `MarioActor::updateControllerSwingLeft` | h self (MarioActor) |  |
| `MarioActor::updateDarkMask` | h self (MarioActor), c  |  |
| `MarioActor::updateEffect` | h self (MarioActor) |  |
| `MarioActor::updateFace` | h self (MarioActor) |  |
| `MarioActor::updateFairyStar` | h self (MarioActor) |  |
| `MarioActor::updateForCamera` | h self (MarioActor) |  |
| `MarioActor::updateGravityVec` | h self (MarioActor), b , b  |  |
| `MarioActor::updateHand` | h self (MarioActor) |  |
| `MarioActor::updateHandAtMorph` | h self (MarioActor) |  |
| `MarioActor::updateHitSensor` | h self (MarioActor), h  |  |
| `MarioActor::updateLife` | h self (MarioActor) |  |
| `MarioActor::updateLightList` | h self (MarioActor), h , h , h , d  |  |
| `MarioActor::updatePlayerMode` | h self (MarioActor) |  |
| `MarioActor::updatePunching` | h self (MarioActor) |  |
| `MarioActor::updateRandomTexture` | h self (MarioActor), d  |  |
| `MarioActor::updateRasterScroll` | h self (MarioActor) |  |
| `MarioActor::updateRealMtx` | h self (MarioActor) |  |
| `MarioActor::updateReflectAlphaList` | h self (MarioActor), c  |  |
| `MarioActor::updateRotationInfo` | h self (MarioActor) |  |
| `MarioActor::updateScouter` | h self (MarioActor) |  |
| `MarioActor::updateSearchLight` | h self (MarioActor) |  |
| `MarioActor::updateSimpleAlphaList` | h self (MarioActor), c  |  |
| `MarioActor::updateSpecialModeAnimation` | h self (MarioActor) |  |
| `MarioActor::updateSwingAction` | h self (MarioActor) |  |
| `MarioActor::updateSwingTimer` | h self (MarioActor) |  |
| `MarioActor::updateTakingPosition` | h self (MarioActor) |  |
| `MarioActor::updateTeresaAnimation` | h self (MarioActor) |  |
| `MarioActor::updateThrowVector` | h self (MarioActor) |  |
| `MarioActor::updateThrowing` | h self (MarioActor) |  |
| `MarioActor::updateTornado` | h self (MarioActor) |  |
| `MarioActor::updateTransForCamera` | h self (MarioActor) |  |
| `MarioActor::writeBackScreenBox` | h self (MarioActor) |  |
| `ModelObj::calcAndSetBaseMtx` | h self (ModelObj) |  |
| `ModelObj::init` | h self (ModelObj), h  |  |
| `NPCActor::attackSensor` | h self (NPCActor), h pSender, h pReceiver |  |
| `NPCActor::calcAndSetBaseMtx` | h self (NPCActor) |  |
| `NPCActor::calcJointScale` | h self (NPCActor), F , h  | b |
| `NPCActor::control` | h self (NPCActor) |  |
| `NPCActor::equipment` | h self (NPCActor), h , b  |  |
| `NPCActor::exeNull` | h self (NPCActor) |  |
| `NPCActor::exeReaction` | h self (NPCActor) |  |
| `NPCActor::exeTalk` | h self (NPCActor) |  |
| `NPCActor::exeWait` | h self (NPCActor) |  |
| `NPCActor::getMsgCtrl` | h self (NPCActor) | h |
| `NPCActor::init` | h self (NPCActor), h  |  |
| `NPCActor::initAfterPlacement` | h self (NPCActor) |  |
| `NPCActor::initTalkCtrl` | h self (NPCActor), h , g , e , F  | b |
| `NPCActor::initTalkCtrlDirect` | h self (NPCActor), h , g , e , F  | b |
| `NPCActor::initialize` | h self (NPCActor), h , h  |  |
| `NPCActor::isEmptyNerve` | h self (NPCActor) | b |
| `NPCActor::isPointingSe` | h self (NPCActor) | b |
| `NPCActor::isPointingStart` | h self (NPCActor) | b |
| `NPCActor::isReactionStart` | h self (NPCActor) | b |
| `NPCActor::isScaleAnim` | h self (NPCActor) | b |
| `NPCActor::isSpinAttackedStart` | h self (NPCActor) | b |
| `NPCActor::isTrampledStart` | h self (NPCActor) | b |
| `NPCActor::kill` | h self (NPCActor) |  |
| `NPCActor::makeActorAppeared` | h self (NPCActor) |  |
| `NPCActor::makeActorDead` | h self (NPCActor) |  |
| `NPCActor::popAndPushNerve` | h self (NPCActor), h  | h |
| `NPCActor::popNerve` | h self (NPCActor) | h |
| `NPCActor::pushNerve` | h self (NPCActor), h  |  |
| `NPCActor::receiveMsgPlayerAttack` | h self (NPCActor), c msg, h pSender, h pReceiver | b |
| `NPCActor::setBaseMtx:hF` | h self (NPCActor), F  |  |
| `NPCActor::setBaseMtx:hf` | h self (NPCActor), f  |  |
| `NPCActor::setDefaults2:h` | h self (NPCActor) |  |
| `NPCActor::setDefaults2:hgggg` | h self (NPCActor), g pReaction, g pPointing, g pSpin, g pTrampled |  |
| `NPCActor::setDefaults3` | h self (NPCActor), g pReaction, g pPointing, g pSpin, g pTrampled |  |
| `NPCActor::setDefaults4` | h self (NPCActor), g pPointingAction, g pReactionAction, g pTrampleAction, g pSpinAction |  |
| `NPCActor::setDefaults:h` | h self (NPCActor) |  |
| `NPCActor::setDefaults:hgggg` | h self (NPCActor), g pReaction, g pPointing, g pTrampled, g pSpin |  |
| `NPCActor::setDefaultsParam` | h self (NPCActor) |  |
| `NPCActor::setInitPose` | h self (NPCActor) |  |
| `NPCActor::setTalkAction:hg` | h self (NPCActor), g pActionName |  |
| `NPCActor::setTalkAction:hgg` | h self (NPCActor), g pActionName, g pActionName2 |  |
| `NPCActor::setToDefault` | h self (NPCActor) |  |
| `NPCActor::tryPushNullNerve` | h self (NPCActor) | b |
| `NPCActor::turnToDefault` | h self (NPCActor), d  | b |
| `NPCActor::turnToPlayer:h` | h self (NPCActor) | b |
| `NPCActor::turnToPlayer:hd` | h self (NPCActor), d turnSpeedDeg | b |
| `NPCActor::turnToPlayer:hddd` | h self (NPCActor), d turnSpeedDeg, d tiltSpeedDeg, d maxTiltDeg | b |
| `NPCActor::updateReaction` | h self (NPCActor) |  |
| `NPCActor::updateScaleCtrl` | h self (NPCActor) |  |
| `NameObj::calcAnim` | h self (NameObj) |  |
| `NameObj::calcViewAndEntry` | h self (NameObj) |  |
| `NameObj::draw` | h self (NameObj) |  |
| `NameObj::executeMovement` | h self (NameObj) |  |
| `NameObj::getName` | h self (NameObj) | g |
| `NameObj::init` | h self (NameObj), h rIter |  |
| `NameObj::initAfterPlacement` | h self (NameObj) |  |
| `NameObj::initWithoutIter` | h self (NameObj) |  |
| `NameObj::movement` | h self (NameObj) |  |
| `NameObj::requestResume` | h self (NameObj) |  |
| `NameObj::requestSuspend` | h self (NameObj) |  |
| `NameObj::setName` | h self (NameObj), g pName |  |
| `NameObj::syncWithFlags` | h self (NameObj) |  |
| `PartsModel::calcAndSetBaseMtx` | h self (PartsModel) |  |
| `PartsModel::calcAnim` | h self (PartsModel) |  |
| `PartsModel::calcViewAndEntry` | h self (PartsModel) |  |
| `PartsModel::init` | h self (PartsModel), h  |  |
| `PartsModel::initFixedPosition:hFee` | h self (PartsModel), F , e , e  |  |
| `PartsModel::initFixedPosition:heeg` | h self (PartsModel), e , e , g  |  |
| `PartsModel::initFixedPosition:hg` | h self (PartsModel), g  |  |
| `PartsModel::isInitFixedPosition` | h self (PartsModel) | b |
| `PartsModel::loadFixedPosition` | h self (PartsModel), g  |  |
| `PartsModel::makeActorAppeared` | h self (PartsModel) |  |
| `PartsModel::makeActorDead` | h self (PartsModel) |  |
| `PartsModel::movement` | h self (PartsModel) |  |
| `PartsModel::offFixedPosNormalizeScale` | h self (PartsModel) |  |
| `nebula.actor_name` | h actor | g |
| `nebula.actor_placed_name` | h actor | g |
| `nebula.add_hungry_luma` | g galaxy, c slot, c star_bits | c |
| `nebula.add_player_animation` | g name, g file, d rate, c blend_frames | c |
| `nebula.add_text` | g id, g utf8 | c |
| `nebula.add_text_like` | g id, g utf8, g like_id | c |
| `nebula.animation_frame` | h actor | d |
| `nebula.animation_length` | h actor | d |
| `nebula.appear` | h actor |  |
| `nebula.appear_item` | h actor, c what, e xyz, c count |  |
| `nebula.attack_actor` | h target, h by?, c how | c |
| `nebula.back_to_dome` |  |  |
| `nebula.buttons_held` |  | c |
| `nebula.buttons_pressed` |  | c |
| `nebula.camera_fov` |  | d |
| `nebula.camera_position` | E xyz |  |
| `nebula.camera_target` | E xyz |  |
| `nebula.change_stage` | g galaxy, c mission |  |
| `nebula.circle_wipe` | c open, c frames | c |
| `nebula.declare_item` | h actor, c what, c count |  |
| `nebula.distance_to_player` | h actor | d |
| `nebula.draw_text` | c line, d x, d y, d size, c rgba, g utf8 | c |
| `nebula.end_camera` | g name, c frames |  |
| `nebula.end_cutscene` | h actor, g name |  |
| `nebula.event_value` | g name | c |
| `nebula.fade_in` | c frames |  |
| `nebula.fade_out` | c frames |  |
| `nebula.find_actor` | g name, h after? | h |
| `nebula.game_camera` | E position, E target | c |
| `nebula.get_front` | h actor, E xyz |  |
| `nebula.get_gravity` | h actor, E xyz |  |
| `nebula.get_joint_position` | h actor, g joint, E xyz |  |
| `nebula.get_position` | h actor, E xyz |  |
| `nebula.get_rotation` | h actor, E xyz |  |
| `nebula.get_scale` | h actor, E xyz |  |
| `nebula.get_tunable` | g key, D value, D game_value | c |
| `nebula.get_velocity` | h actor, E xyz |  |
| `nebula.give_grand_stars` | c count | c |
| `nebula.give_star` | g galaxy, c star, c grand | c |
| `nebula.grant_spin_and_pointer` |  | c |
| `nebula.gravity_at` | e xyz, E out |  |
| `nebula.gravity_scale` |  | d |
| `nebula.has_rail` | h actor | c |
| `nebula.has_star` | g galaxy, c star | c |
| `nebula.has_switch` | h actor, c which | c |
| `nebula.hide_message` |  |  |
| `nebula.hit_player` | h actor, c how | c |
| `nebula.is_animation_done` | h actor | c |
| `nebula.is_cutscene_playing` |  | c |
| `nebula.is_dead` | h actor | c |
| `nebula.is_event_flag_on` | g name | c |
| `nebula.is_in_area` | g area, e xyz | c |
| `nebula.is_in_water` | e xyz | c |
| `nebula.is_movie_playing` |  | c |
| `nebula.is_on_ground` | h actor | c |
| `nebula.is_switch_on` | h actor, c which | c |
| `nebula.is_touching_wall` | h actor | c |
| `nebula.joint_name` | h actor, c index | g |
| `nebula.jump_player` | e velocity |  |
| `nebula.kill` | h actor |  |
| `nebula.kill_player` |  |  |
| `nebula.layout_is_playing` | h layout | c |
| `nebula.layout_move_pane` | h layout, g pane, d x, d y |  |
| `nebula.layout_open` | g name | h |
| `nebula.layout_play` | h layout, g animation |  |
| `nebula.layout_set_alpha` | h layout, d alpha |  |
| `nebula.layout_set_number` | h layout, g pane, c number |  |
| `nebula.layout_set_text` | h layout, g pane, g utf8 |  |
| `nebula.layout_show` | h layout, c show |  |
| `nebula.layout_show_pane` | h layout, g pane, c show |  |
| `nebula.log` | g text |  |
| `nebula.move_on_rail` | h actor, d speed |  |
| `nebula.nerve` | h actor | g |
| `nebula.nerve_at` | c index | g |
| `nebula.nerve_frames` | h actor | c |
| `nebula.open_scenario_select` | g galaxy |  |
| `nebula.option` | g id | g |
| `nebula.pause_timer` | c paused |  |
| `nebula.place_talk_box` | d x, d y, c on |  |
| `nebula.placement_number` | h actor, g column, D value | c |
| `nebula.placement_text` | h actor, g column | g |
| `nebula.play` | h actor, c what, g name | c |
| `nebula.play_movie` | g file | c |
| `nebula.play_music` | g name | c |
| `nebula.play_system_sound` | g name |  |
| `nebula.player` |  | h |
| `nebula.player_constant` | c character, g name, c index | d |
| `nebula.player_front` | E xyz |  |
| `nebula.pointer` | J xy | c |
| `nebula.rail_at_end` | h actor | c |
| `nebula.rail_coord` | h actor | d |
| `nebula.rail_length` | h actor | d |
| `nebula.rail_turn_back` | h actor |  |
| `nebula.raycast` | e from, e to, E hit | c |
| `nebula.registry_count` | g kind | c |
| `nebula.registry_find` | g kind, g name | c |
| `nebula.registry_mod` | g kind, c index | g |
| `nebula.registry_name` | g kind, c index | g |
| `nebula.registry_value` | g kind, g name | c |
| `nebula.save_game` |  |  |
| `nebula.screen_position` | e xyz, J xy | c |
| `nebula.set_animation_frame` | h actor, d frame |  |
| `nebula.set_animation_rate` | h actor, d rate |  |
| `nebula.set_camera` | e position, e target, d fov |  |
| `nebula.set_camera_near` | d distance |  |
| `nebula.set_comet_time_limit` | g galaxy, c mission, c seconds | c |
| `nebula.set_event_flag_on` | g name |  |
| `nebula.set_event_value` | g name, c value |  |
| `nebula.set_front` | h actor, e xyz |  |
| `nebula.set_game_speed` | d speed |  |
| `nebula.set_gravity_scale` | d scale |  |
| `nebula.set_nerve` | h actor, g nerve | c |
| `nebula.set_object_change` | c where, g object, g becomes | c |
| `nebula.set_player_character` | c luigi | c |
| `nebula.set_player_constant` | c character, g name, c index, d value | c |
| `nebula.set_player_control` | c on |  |
| `nebula.set_position` | h actor, e xyz |  |
| `nebula.set_rail_coord` | h actor, d coord |  |
| `nebula.set_rotation` | h actor, e xyz |  |
| `nebula.set_scale` | h actor, e xyz |  |
| `nebula.set_star` | g galaxy, c star, c have |  |
| `nebula.set_state` | h actor, c state |  |
| `nebula.set_switch` | h actor, c which, c on |  |
| `nebula.set_timer` | d seconds_left | c |
| `nebula.set_touchable` | h actor, c touchable |  |
| `nebula.set_tunable` | g key, d value | c |
| `nebula.set_value` | c what, c value |  |
| `nebula.set_velocity` | h actor, e xyz |  |
| `nebula.shake_camera` |  |  |
| `nebula.shake_camera_by` | c how |  |
| `nebula.show_hud` | c parts, c show |  |
| `nebula.show_joint` | h actor, g joint, c on |  |
| `nebula.show_message` | g utf8_or_id, c centre |  |
| `nebula.show_model` | h actor, c show |  |
| `nebula.spawn` | g name, e xyz | h |
| `nebula.stage_name` |  | g |
| `nebula.stars_in_galaxy` | g galaxy | c |
| `nebula.stars_won_in_galaxy` | g galaxy | c |
| `nebula.start_camera` | g name, c frames |  |
| `nebula.start_cutscene` | h actor, g name | c |
| `nebula.state` | h actor | c |
| `nebula.state_frames` | h actor | c |
| `nebula.step_game` | c steps |  |
| `nebula.stick` | D x, D y |  |
| `nebula.stop_effect` | h actor, g name |  |
| `nebula.stop_movie` |  |  |
| `nebula.stop_music` | c frames |  |
| `nebula.stop_scene` | c frames |  |
| `nebula.stop_sound` | h actor, g name |  |
| `nebula.store_get` | g key, c missing | c |
| `nebula.store_set` | g key, c value |  |
| `nebula.store_set_text` | g key, g text |  |
| `nebula.talking_actor` |  | h |
| `nebula.timer` | D seconds_left, D seconds_limit | c |
| `nebula.toolkit_find` | g name | c |
| `nebula.turn_to_player` | h actor, d degrees |  |
| `nebula.unlock_achievement` | g id | c |
| `nebula.value` | c what | c |
| `nebula.water_point` | c index, E xyz | c |

## Left out

What the toolkit cannot offer yet, and why.

| Name | Why |
|---|---|
| `MR::Functor` | parameter type void (*)() |
| `MR::addNewLine` | parameter type char16_t * |
| `MR::addNumberFontTag` | parameter type char16_t * |
| `MR::addNumberFontTag` | takes a variable number of arguments |
| `MR::addPictureFontCode` | parameter type char16_t * |
| `MR::addPictureFontTag` | parameter type char16_t * |
| `MR::appearInformationMessage:gb` | two overloads with the same value kinds |
| `MR::appearInformationMessage:gb` | two overloads with the same value kinds |
| `MR::appearInformationMessageCenter:gb` | two overloads with the same value kinds |
| `MR::appearInformationMessageCenter:gb` | two overloads with the same value kinds |
| `MR::calcAttractMarioLauncherOrZero` | parameter type const void * |
| `MR::calcBoundingBox:hhd` | two overloads with the same value kinds |
| `MR::calcBoundingBox:hhd` | two overloads with the same value kinds |
| `MR::calcDropShadowVector` | parameter type const void * |
| `MR::calcDropShadowVector` | parameter type const void * |
| `MR::calcDropShadowVectorOrZero` | parameter type const void * |
| `MR::calcGravityAndDropShadowVector` | parameter type const void * |
| `MR::calcGravityAndDropShadowVectorOrZero` | parameter type const void * |
| `MR::calcGravityAndMagnetVector` | parameter type const void * |
| `MR::calcGravityVector` | parameter type const void * |
| `MR::calcGravityVector` | parameter type const void * |
| `MR::calcGravityVectorOrZero` | parameter type const void * |
| `MR::calcGravityVectorOrZero` | parameter type const void * |
| `MR::calcNerveEaseInOutValue:hccdd` | two overloads with the same value kinds |
| `MR::calcNerveEaseInOutValue:hccdd` | two overloads with the same value kinds |
| `MR::calcNerveEaseInOutValue:hcdd` | two overloads with the same value kinds |
| `MR::calcNerveEaseInOutValue:hcdd` | two overloads with the same value kinds |
| `MR::calcNerveEaseInRate:hc` | two overloads with the same value kinds |
| `MR::calcNerveEaseInRate:hc` | two overloads with the same value kinds |
| `MR::calcNerveEaseInRate:hc` | two overloads with the same value kinds |
| `MR::calcNerveEaseInValue:hccdd` | two overloads with the same value kinds |
| `MR::calcNerveEaseInValue:hccdd` | two overloads with the same value kinds |
| `MR::calcNerveEaseInValue:hcdd` | two overloads with the same value kinds |
| `MR::calcNerveEaseOutRate:hc` | two overloads with the same value kinds |
| `MR::calcNerveEaseOutRate:hc` | two overloads with the same value kinds |
| `MR::calcNerveEaseOutRate:hcc` | two overloads with the same value kinds |
| `MR::calcNerveRate:hc` | two overloads with the same value kinds |
| `MR::calcNerveRate:hc` | two overloads with the same value kinds |
| `MR::calcNerveRate:hc` | two overloads with the same value kinds |
| `MR::calcNerveRate:hcc` | two overloads with the same value kinds |
| `MR::calcNerveRate:hcc` | two overloads with the same value kinds |
| `MR::calcNerveValue:hccdd` | two overloads with the same value kinds |
| `MR::calcNerveValue:hcdd` | two overloads with the same value kinds |
| `MR::calcNerveValue:hcdd` | two overloads with the same value kinds |
| `MR::callMethodAllSceneNameObj` | parameter type void (NameObj::*)() |
| `MR::clamp` | declared, never defined |
| `MR::connectToScene:hcccc` | two overloads with the same value kinds |
| `MR::connectToScene:hcccc` | two overloads with the same value kinds |
| `MR::convertUTF16ToASCII` | parameter type char * |
| `MR::copyJointAnimation:hh` | two overloads with the same value kinds |
| `MR::copyJointAnimation:hh` | two overloads with the same value kinds |
| `MR::copyJointPos:hcE` | two overloads with the same value kinds |
| `MR::copyJointPos:hcE` | two overloads with the same value kinds |
| `MR::copyJointPos:hgE` | two overloads with the same value kinds |
| `MR::copyJointPos:hgE` | two overloads with the same value kinds |
| `MR::copyMemory` | parameter type void * |
| `MR::copyString` | parameter type char * |
| `MR::copyString` | parameter type char16_t * |
| `MR::copyText` | parameter type char16_t * |
| `MR::createActorCameraInfoIfExist` | parameter type ActorCameraInfo ** |
| `MR::createAdaptorAndConnectToDrawBloomModel` | parameter type const MR::FunctorBase & |
| `MR::createCollisionPartsFromLiveActor` | parameter type MR::CollisionScaleType |
| `MR::createCollisionPartsFromLiveActor` | parameter type MR::CollisionScaleType |
| `MR::createCollisionPartsFromResourceHolder` | parameter type MR::CollisionScaleType |
| `MR::createCsvParser` | takes a variable number of arguments |
| `MR::createCsvParser` | takes a variable number of arguments |
| `MR::createDrawAdaptor` | parameter type const MR::FunctorBase & |
| `MR::createPlacementIdFromClippingGroupId` | result type PlacementId |
| `MR::createPlacementIdFromGroupId` | result type PlacementId |
| `MR::createTriangleFilterFunc` | parameter type TriangleFunc |
| `MR::declareEventCameraAnim` | parameter type void * |
| `MR::deleteEffect:hg` | two overloads with the same value kinds |
| `MR::deleteEffect:hg` | two overloads with the same value kinds |
| `MR::deleteEffectAll:h` | two overloads with the same value kinds |
| `MR::deleteEffectAll:h` | two overloads with the same value kinds |
| `MR::emitEffect:hg` | two overloads with the same value kinds |
| `MR::emitEffect:hg` | two overloads with the same value kinds |
| `MR::endStarPointerMode` | parameter type void * |
| `MR::fillMemory` | parameter type void * |
| `MR::fillScreenSetup` | result type SiteMaterial |
| `MR::findBckNameStringInResource` | parameter type const char ** |
| `MR::forceDeleteEffect:hg` | two overloads with the same value kinds |
| `MR::forceDeleteEffect:hg` | two overloads with the same value kinds |
| `MR::forceDeleteEffectAll:h` | two overloads with the same value kinds |
| `MR::forceDeleteEffectAll:h` | two overloads with the same value kinds |
| `MR::formatText` | takes a variable number of arguments |
| `MR::formatTextV` | parameter type char16_t * |
| `MR::getAreaIn` | declared, never defined |
| `MR::getCameraRailInfo` | parameter type const Table ** |
| `MR::getCameraRailInfoFromRailDataIndex` | parameter type const Table ** |
| `MR::getChildObjName` | parameter type const char ** |
| `MR::getCsvDataStr` | parameter type const char ** |
| `MR::getCsvDataStrOrNULL` | parameter type const char ** |
| `MR::getCurrentGalaxyNameOnCurrentLanguage` | result type const char16_t * |
| `MR::getCurrentScenarioNameOnCurrentLanguage` | result type const char16_t * |
| `MR::getCurrentScenarioStartAnimCameraData` | parameter type void ** |
| `MR::getEffect:hg` | two overloads with the same value kinds |
| `MR::getEffect:hg` | two overloads with the same value kinds |
| `MR::getFloorCodeIndex:h` | two overloads with the same value kinds |
| `MR::getFloorCodeIndex:h` | two overloads with the same value kinds |
| `MR::getGalaxyNameOnCurrentLanguage` | result type const char16_t * |
| `MR::getGalaxyNameShortOnCurrentLanguage` | result type const char16_t * |
| `MR::getGameArchive` | parameter type NebulaArena ** |
| `MR::getGameMessageDirect` | result type const char16_t * |
| `MR::getGeneralPosData` | parameter type const char ** |
| `MR::getJoint:hc` | two overloads with the same value kinds |
| `MR::getJoint:hg` | two overloads with the same value kinds |
| `MR::getJoint:hg` | two overloads with the same value kinds |
| `MR::getJointMtx:hc` | two overloads with the same value kinds |
| `MR::getJointMtx:hg` | two overloads with the same value kinds |
| `MR::getJointMtx:hg` | two overloads with the same value kinds |
| `MR::getLayoutMessageDirect` | result type const char16_t * |
| `MR::getLightNumMax:h` | two overloads with the same value kinds |
| `MR::getLightNumMax:h` | two overloads with the same value kinds |
| `MR::getMapPartsArgMoveSpeed` | declared, never defined |
| `MR::getMapPartsArgRailRotateSpeed` | declared, never defined |
| `MR::getMapPartsArgRailRotateTime` | declared, never defined |
| `MR::getMaterial:hc` | two overloads with the same value kinds |
| `MR::getMaterial:hc` | two overloads with the same value kinds |
| `MR::getMaterial:hc` | two overloads with the same value kinds |
| `MR::getMaterial:hg` | two overloads with the same value kinds |
| `MR::getMaterialNo:hg` | two overloads with the same value kinds |
| `MR::getMaterialNo:hg` | two overloads with the same value kinds |
| `MR::getMessageLine` | parameter type char16_t * |
| `MR::getNextLinkRailInfo` | parameter type const Table ** |
| `MR::getNextMessagePage` | result type const char16_t * |
| `MR::getObjectName` | parameter type const char ** |
| `MR::getParentJoint:hh` | two overloads with the same value kinds |
| `MR::getParentJoint:hh` | two overloads with the same value kinds |
| `MR::getPlacementArgNoInit` | declared, never defined |
| `MR::getPlacementArgNoInit` | declared, never defined |
| `MR::getPlacementArgNoInit` | declared, never defined |
| `MR::getPlacementRailArg` | declared, never defined |
| `MR::getRaceNameOnCurrentLanguage` | result type const char16_t * |
| `MR::getRailInfo` | parameter type const Table ** |
| `MR::getResTIMG:hc` | two overloads with the same value kinds |
| `MR::getResTIMG:hc` | two overloads with the same value kinds |
| `MR::getScenarioNameOnCurrentLanguage` | result type const char16_t * |
| `MR::getSoundCodeIndex:h` | two overloads with the same value kinds |
| `MR::getSoundCodeIndex:h` | two overloads with the same value kinds |
| `MR::getStageCameraData` | parameter type void ** |
| `MR::getStarPointerLastPointedPort` | result type s32 * |
| `MR::getStarPointerProjMtx` | result type Mtx44Ptr |
| `MR::getSystemMessageDirect` | result type const char16_t * |
| `MR::hideJoint:h` | two overloads with the same value kinds |
| `MR::hideJoint:hg` | two overloads with the same value kinds |
| `MR::hideJoint:hg` | two overloads with the same value kinds |
| `MR::hideJointAndChildren:h` | two overloads with the same value kinds |
| `MR::hideJointAndChildren:hg` | two overloads with the same value kinds |
| `MR::hideJointAndChildren:hg` | two overloads with the same value kinds |
| `MR::hideMaterial:hg` | two overloads with the same value kinds |
| `MR::hideMaterial:hg` | two overloads with the same value kinds |
| `MR::initActorCamera` | parameter type ActorCameraInfo ** |
| `MR::initDemoSheetTalkAnimFunctor` | parameter type const MR::FunctorBase & |
| `MR::initEffectSyncBck` | parameter type const char *const * |
| `MR::initMultiActorCamera` | parameter type ActorCameraInfo ** |
| `MR::invalidateCollisionParts:h` | two overloads with the same value kinds |
| `MR::invalidateCollisionParts:h` | two overloads with the same value kinds |
| `MR::isBckPlaying:hg` | two overloads with the same value kinds |
| `MR::isBckPlaying:hg` | two overloads with the same value kinds |
| `MR::isDead:h` | two overloads with the same value kinds |
| `MR::isDead:h` | two overloads with the same value kinds |
| `MR::isExistEffectKeeper:h` | two overloads with the same value kinds |
| `MR::isExistEffectKeeper:h` | two overloads with the same value kinds |
| `MR::isExistEffectTexMtx:h` | two overloads with the same value kinds |
| `MR::isExistEffectTexMtx:h` | two overloads with the same value kinds |
| `MR::isExistFileInArc` | takes a variable number of arguments |
| `MR::isExistString` | parameter type const char *const * |
| `MR::isFirstStep:h` | two overloads with the same value kinds |
| `MR::isFirstStep:h` | two overloads with the same value kinds |
| `MR::isFirstStep:h` | two overloads with the same value kinds |
| `MR::isGreaterEqualStep:hc` | two overloads with the same value kinds |
| `MR::isGreaterEqualStep:hc` | two overloads with the same value kinds |
| `MR::isGreaterEqualStep:hc` | two overloads with the same value kinds |
| `MR::isGreaterStep:hc` | two overloads with the same value kinds |
| `MR::isGreaterStep:hc` | two overloads with the same value kinds |
| `MR::isGreaterStep:hc` | two overloads with the same value kinds |
| `MR::isIntervalStep:hc` | two overloads with the same value kinds |
| `MR::isIntervalStep:hc` | two overloads with the same value kinds |
| `MR::isIntervalStep:hc` | two overloads with the same value kinds |
| `MR::isLessEqualStep:hc` | two overloads with the same value kinds |
| `MR::isLessEqualStep:hc` | two overloads with the same value kinds |
| `MR::isLessStep:hc` | two overloads with the same value kinds |
| `MR::isLessStep:hc` | two overloads with the same value kinds |
| `MR::isLessStep:hc` | two overloads with the same value kinds |
| `MR::isNear:hed` | two overloads with the same value kinds |
| `MR::isNear:hhd` | two overloads with the same value kinds |
| `MR::isNear:hhd` | two overloads with the same value kinds |
| `MR::isNearPlayer:ed` | two overloads with the same value kinds |
| `MR::isNearPlayer:hd` | two overloads with the same value kinds |
| `MR::isNearPlayer:hd` | two overloads with the same value kinds |
| `MR::isNewNerve:h` | two overloads with the same value kinds |
| `MR::isNewNerve:h` | two overloads with the same value kinds |
| `MR::isNewNerve:h` | two overloads with the same value kinds |
| `MR::isOnPlayer:h` | two overloads with the same value kinds |
| `MR::isOnPlayer:h` | two overloads with the same value kinds |
| `MR::isRegisteredEffect:hg` | two overloads with the same value kinds |
| `MR::isRegisteredEffect:hg` | two overloads with the same value kinds |
| `MR::isStep:hc` | two overloads with the same value kinds |
| `MR::isStep:hc` | two overloads with the same value kinds |
| `MR::isStep:hc` | two overloads with the same value kinds |
| `MR::lerp` | parameter type NebulaColor8 |
| `MR::listenNameObjStageSwitchOffB` | parameter type const MR::FunctorBase & |
| `MR::listenNameObjStageSwitchOnA` | parameter type const MR::FunctorBase & |
| `MR::listenNameObjStageSwitchOnAppear` | parameter type const MR::FunctorBase & |
| `MR::listenNameObjStageSwitchOnB` | parameter type const MR::FunctorBase & |
| `MR::listenNameObjStageSwitchOnOffA` | parameter type const MR::FunctorBase & |
| `MR::listenNameObjStageSwitchOnOffAppear` | parameter type const MR::FunctorBase & |
| `MR::listenNameObjStageSwitchOnOffB` | parameter type const MR::FunctorBase & |
| `MR::listenStageSwitchOffB` | parameter type const MR::FunctorBase & |
| `MR::listenStageSwitchOnA` | parameter type const MR::FunctorBase & |
| `MR::listenStageSwitchOnAppear` | parameter type const MR::FunctorBase & |
| `MR::listenStageSwitchOnB` | parameter type const MR::FunctorBase & |
| `MR::listenStageSwitchOnOffA` | parameter type const MR::FunctorBase & |
| `MR::listenStageSwitchOnOffAppear` | parameter type const MR::FunctorBase & |
| `MR::listenStageSwitchOnOffB` | parameter type const MR::FunctorBase & |
| `MR::loadResourceFromArc` | result type void * |
| `MR::makeClearTimeString` | result type const char16_t * |
| `MR::makeDateString` | result type const char16_t * |
| `MR::makeMinuteAndSecondString` | result type const char16_t * |
| `MR::makeMtxFrontUp:Fee` | two overloads with the same value kinds |
| `MR::makeMtxFrontUp:Fee` | two overloads with the same value kinds |
| `MR::makeMtxUpFront:Fee` | two overloads with the same value kinds |
| `MR::makeMtxUpFront:Fee` | two overloads with the same value kinds |
| `MR::makeMtxUpSide:Fee` | two overloads with the same value kinds |
| `MR::makeMtxUpSide:Fee` | two overloads with the same value kinds |
| `MR::makeRaceBestTimeString` | result type const char16_t * |
| `MR::makeRaceCurrentTimeString` | result type const char16_t * |
| `MR::makeTimeString` | result type const char16_t * |
| `MR::multMtx` | parameter type f32 (*)[4] |
| `MR::offCalcAnim:h` | two overloads with the same value kinds |
| `MR::offCalcAnim:h` | two overloads with the same value kinds |
| `MR::onCalcAnim:h` | two overloads with the same value kinds |
| `MR::onCalcAnim:h` | two overloads with the same value kinds |
| `MR::pauseOffEffectAll:h` | two overloads with the same value kinds |
| `MR::pauseOffEffectAll:h` | two overloads with the same value kinds |
| `MR::registerDemoActionFunctor` | parameter type const MR::FunctorBase & |
| `MR::registerDemoActionFunctorDirect` | parameter type const MR::FunctorBase & |
| `MR::registerDemoSimpleCastAll:h` | two overloads with the same value kinds |
| `MR::registerDemoSimpleCastAll:h` | two overloads with the same value kinds |
| `MR::registerDemoSimpleCastAll:h` | two overloads with the same value kinds |
| `MR::registerPreDrawFunction` | parameter type const MR::FunctorBase & |
| `MR::releaseAnimFrame:h` | two overloads with the same value kinds |
| `MR::releaseAnimFrame:h` | two overloads with the same value kinds |
| `MR::requestMovementOn:h` | two overloads with the same value kinds |
| `MR::requestMovementOn:h` | two overloads with the same value kinds |
| `MR::requestMovementOn:h` | two overloads with the same value kinds |
| `MR::requestPointLight` | parameter type Color8 |
| `MR::requestStarPointerModeBigBubble` | parameter type void * |
| `MR::requestStarPointerModeBlueStarReady` | parameter type void * |
| `MR::requestStarPointerModeErrorWindow` | parameter type void * |
| `MR::requestStarPointerModePauseMenu` | parameter type void * |
| `MR::requestStarPointerModePictureBook` | parameter type void * |
| `MR::requestStarPointerModeSaveLoad` | parameter type void * |
| `MR::requestStartDemoWithoutCinemaFrame:hghh` | two overloads with the same value kinds |
| `MR::requestStartDemoWithoutCinemaFrame:hghh` | two overloads with the same value kinds |
| `MR::requestTicoSeedGuidance` | declared, never defined |
| `MR::setEffectHostMtx:hgF` | two overloads with the same value kinds |
| `MR::setEffectHostMtx:hgF` | two overloads with the same value kinds |
| `MR::setNerveAtStep:hhc` | two overloads with the same value kinds |
| `MR::setNerveAtStep:hhc` | two overloads with the same value kinds |
| `MR::setNerveAtStep:hhc` | two overloads with the same value kinds |
| `MR::setShadowSurfaceOvalColor` | parameter type Color8 |
| `MR::setTextBoxFormatRecursive` | takes a variable number of arguments |
| `MR::showJoint:h` | two overloads with the same value kinds |
| `MR::showJoint:hg` | two overloads with the same value kinds |
| `MR::showJoint:hg` | two overloads with the same value kinds |
| `MR::showJointAndChildren:h` | two overloads with the same value kinds |
| `MR::showJointAndChildren:hg` | two overloads with the same value kinds |
| `MR::showJointAndChildren:hg` | two overloads with the same value kinds |
| `MR::showMaterial` | declared, never defined |
| `MR::startLevelSound` | parameter type SoundID |
| `MR::startSound` | parameter type SoundID |
| `MR::startSoundObjectLevel` | parameter type SoundID |
| `MR::startSoundObjectLevelParam` | parameter type SoundID |
| `MR::startStarPointerMode1PInvalid2PValid` | parameter type void * |
| `MR::startStarPointerModeBlueStar` | parameter type void * |
| `MR::startStarPointerModeChooseYesNo` | parameter type void * |
| `MR::startStarPointerModeCommandStream` | parameter type void * |
| `MR::startStarPointerModeDemo` | parameter type void * |
| `MR::startStarPointerModeDemoMarioDeath` | parameter type void * |
| `MR::startStarPointerModeDemoWithHandPointerFinger` | parameter type void * |
| `MR::startStarPointerModeDemoWithStarPointer` | parameter type void * |
| `MR::startStarPointerModeEnding` | parameter type void * |
| `MR::startStarPointerModeFileSelect` | parameter type void * |
| `MR::startStarPointerModeGame` | parameter type void * |
| `MR::startStarPointerModeMarioLauncher` | parameter type void * |
| `MR::startStarPointerModePauseMenu` | parameter type void * |
| `MR::startStarPointerModePowerStarGetDemo` | parameter type void * |
| `MR::startStarPointerModeScenarioSelectScene` | parameter type void * |
| `MR::startStarPointerModeSphereSelectorFinger` | parameter type void * |
| `MR::startStarPointerModeSphereSelectorOnReaction` | parameter type void * |
| `MR::startStarPointerModeStarPieceTarget` | parameter type void * |
| `MR::startStarPointerModeTitle` | parameter type void * |
| `MR::startSystemLevelSE` | parameter type SoundID |
| `MR::startSystemSE` | parameter type SoundID |
| `MR::stopAnimFrame:h` | two overloads with the same value kinds |
| `MR::stopAnimFrame:h` | two overloads with the same value kinds |
| `MR::stopSound` | parameter type SoundID |
| `MR::stopSystemSE` | parameter type SoundID |
| `MR::syncMaterialAnimation:hh` | two overloads with the same value kinds |
| `MR::syncMaterialAnimation:hh` | two overloads with the same value kinds |
| `MR::textLength` | result type size_t |
| `MR::tryCreateCollisionAllOtherCategory` | parameter type CollisionParts ** |
| `MR::tryCreateCollisionAllOtherCategory` | parameter type CollisionParts ** |
| `MR::tryCreateCsvParser` | takes a variable number of arguments |
| `MR::tryCreateCsvParser` | takes a variable number of arguments |
| `MR::tryRegisterDemoActionFunctor` | parameter type const MR::FunctorBase & |
| `MR::tryRegisterDemoActionFunctorDirect` | parameter type const MR::FunctorBase & |
| `MR::tryRumbleDefaultHit` | parameter type const void * |
| `MR::tryRumblePad` | parameter type const void * |
| `MR::tryRumblePadMiddle` | parameter type const void * |
| `MR::tryRumblePadStrong` | parameter type const void * |
| `MR::tryRumblePadVeryStrong` | parameter type const void * |
| `MR::tryRumblePadVeryStrongLong` | parameter type const void * |
| `MR::tryRumblePadVeryWeak` | parameter type const void * |
| `MR::tryRumblePadWeak` | parameter type const void * |
| `MR::tryStartDemo:hg` | two overloads with the same value kinds |
| `MR::tryStartDemo:hg` | two overloads with the same value kinds |
| `MR::tryStartDemoWithoutCinemaFrameValidHandPointerFinger:hg` | two overloads with the same value kinds |
| `MR::tryStartDemoWithoutCinemaFrameValidHandPointerFinger:hg` | two overloads with the same value kinds |
| `MR::tryStartTimeKeepDemoMarioPuppetable:hgg` | two overloads with the same value kinds |
| `MR::tryStartTimeKeepDemoMarioPuppetable:hgg` | two overloads with the same value kinds |
| `MR::updateActorState:h` | two overloads with the same value kinds |
| `MR::updateActorState:hh` | two overloads with the same value kinds |
| `MR::updateActorState:hh` | two overloads with the same value kinds |
| `MR::updateActorStateAndNextNerve:hh` | two overloads with the same value kinds |
| `MR::updateActorStateAndNextNerve:hhh` | two overloads with the same value kinds |
| `MR::updateActorStateAndNextNerve:hhh` | two overloads with the same value kinds |
| `MR::validateCollisionParts:h` | two overloads with the same value kinds |
| `MR::validateCollisionParts:h` | two overloads with the same value kinds |
| `MR::zeroMemory` | parameter type void * |
| `MarioActor::cylinderPushCheck` | declared, never defined |
| `NameObj::initStep` | parameter type uint64_t |
| `nebula.actor_data` | result type void* |
| `nebula.actor_value` | result type NebulaValue |
| `nebula.actors_near` | parameter type NebulaActor** |
| `nebula.add_galaxy` | only while plugins start |
| `nebula.add_mod_script` | starts a script (luma.json "scripts" does) |
| `nebula.add_music` | only while plugins start |
| `nebula.add_power_up` | parameter type const NebulaPowerUpClass* |
| `nebula.add_script_object` | adds a script object (luma.json "objects" does) |
| `nebula.add_sound_effect` | only while plugins start |
| `nebula.alloc` | result type void* |
| `nebula.class_name` | parameter type uint32_t* |
| `nebula.field_find` | parameter type const NebulaValue* |
| `nebula.field_get` | parameter type const NebulaValue* |
| `nebula.field_set` | parameter type const NebulaValue* |
| `nebula.find_function` | result type void* |
| `nebula.format` | parameter type char* |
| `nebula.get_joint_matrix` | parameter type float* |
| `nebula.hook` | parameter type void* |
| `nebula.hook_nerve` | only while plugins start |
| `nebula.is_a` | parameter type const NebulaValue* |
| `nebula.list_materials` | parameter type NebulaMaterialCallback |
| `nebula.luma_start` | parameter type const struct NebulaModApi* |
| `nebula.register_object` | parameter type const NebulaObjectClass* |
| `nebula.release` | parameter type void* |
| `nebula.set_actor_data` | parameter type void* |
| `nebula.set_area_music` | only while plugins start |
| `nebula.set_class_hooks` | parameter type const NebulaClassHooks* |
| `nebula.set_class_script` | only while plugins start |
| `nebula.set_comet_schedule` | only while plugins start |
| `nebula.set_event_flag` | only while plugins start |
| `nebula.set_object_class` | only while plugins start |
| `nebula.set_stage_music` | only while plugins start |
| `nebula.set_stage_rules` | only while plugins start |
| `nebula.set_story` | only while plugins start |
| `nebula.store_get_text` | parameter type char* |
| `nebula.subscribe` | only while plugins start |
| `nebula.toolkit_call` | parameter type const NebulaValue* |
| `nebula.tunable_at` | parameter type const char** |
| `nebula.value_actor` | parameter type const NebulaValue* |
