#include "Game/MapObj/StarPieceDirector.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/StarPiece.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/StarPointerDirector.hpp"
#include "Game/Util.hpp"
#include "Game/System/NebulaAchievements.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcPosCameraShoot_f1, "calcPosCameraShoot.f1", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcPosCameraShoot_f2, "calcPosCameraShoot.f2", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcPosCameraShoot_f3, "calcPosCameraShoot.f3", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 160.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f2, "control.f2", 7.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f3, "control.f3", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f4, "control.f4", 0.125f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryShoot_f1, "tryShoot.f1", 999999.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryShoot_f2, "tryShoot.f2", 999999.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcShootGoalUsingPointingDepth_f1, "calcShootGoalUsingPointingDepth.f1", 16000000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcShootGoalUsingPointingDepth_f2, "calcShootGoalUsingPointingDepth.f2", 3000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcShootGoalUsingPointingDepth_f3, "calcShootGoalUsingPointingDepth.f3", 9000000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcShootGoalUsingPointingDepth_f4, "calcShootGoalUsingPointingDepth.f4", 3000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcShootGoalUsingPointingDepth_f5, "calcShootGoalUsingPointingDepth.f5", 400.0f);

namespace NrvStarPieceShooter {
    NEW_NERVE(HostTypeNrvWait, StarPieceShooter, Wait);
    NEW_NERVE(HostTypeNrvLockOn, StarPieceShooter, LockOn);
};  // namespace NrvStarPieceShooter

StarPieceDirector* MR::getStarPieceDirector() {
    return MR::getSceneObj< StarPieceDirector >(SceneObj_StarPieceDirector);
}

StarPiece* MR::getDeadStarPiece() {
    return getStarPieceDirector()->getDeadStarPiece();
}

void MR::resetChasingStarPiece() {
    getStarPieceDirector()->mResetChasingStarPiece = true;
}

bool MR::isResetChasingStarPiece() {
    return getStarPieceDirector()->mResetChasingStarPiece;
}

bool MR::gotByPlayer() {
    return getStarPieceDirector()->gotByPlayer();
}

bool MR::giftStarPieceToTarget(HitSensor* pGiftSensor, u32 numGift) {
    return getStarPieceDirector()->giftToTarget(pGiftSensor, numGift);
}

StarPieceDirector::StarPieceDirector(const char* pName)
    : LiveActorGroup(pName, 128), mColorIndex(), mNumStarPiecesLaunched(),
      mResetChasingStarPiece(), mGetSounds(), mSoundStep(), mQueueNewGetSound() {
    lumacore::setClass(this, lumacore::kClassId< StarPieceDirector >);
    MR::connectToSceneMapObjMovement(this);

    for (int i = 0; i < ARRAY_SIZE(mStarPieceShooters); i++) {
        mStarPieceShooters[i] = new StarPieceShooter(i, "発射用アクター");
        mStarPieceShooters[i]->initWithoutIter();
    }

    mHostInfos.init(128);
    mReceiverInfos.init(16);

    initCSDelay();
}

void StarPieceDirector::declare(const NameObj* pNameObj, s32 num) {
    if (num > 0) {
        StarPieceHostInfo* hostInfo = findHostInfo(pNameObj);

        if (hostInfo == nullptr) {
            hostInfo = new StarPieceHostInfo(const_cast< NameObj* >(pNameObj), 0, 0, 0);
            mHostInfos.push_back(hostInfo);
        }

        hostInfo->mNumPieces += num;
    }
}

void StarPieceDirector::declareReceiveNum(const NameObj* pNameObj, s32 num) {
    if (num > 0) {
        StarPieceReceiverInfo* receiverInfo = findReceiverInfo(pNameObj);

        if (receiverInfo == nullptr) {
            receiverInfo = new StarPieceReceiverInfo(const_cast< NameObj* >(pNameObj), 0, 0, 0);
            mReceiverInfos.push_back(receiverInfo);
        }

        receiverInfo->mNumWanted += num;
    }
}

s32 StarPieceDirector::getDeclareRemnantStarPieceCount(const NameObj* pNameObj) {
    StarPieceHostInfo* hostInfo = findHostInfo(pNameObj);
    // A host that declared no star pieces has none left.
    if (hostInfo == nullptr) {
        return 0;
    }
    return hostInfo->mNumPieces - hostInfo->mNumGot;
}

s32 StarPieceDirector::getDeclareRemnantStarPieceCountNotExist(const NameObj* pNameObj) {
    StarPieceHostInfo* hostInfo = findHostInfo(pNameObj);
    if (hostInfo == nullptr) {
        return 0;
    }
    return hostInfo->mNumPieces - hostInfo->mNumGot - hostInfo->mNumAppeared;
}

bool StarPieceDirector::hopPiece(const NameObj* pNameObj, const Vec3f& rVec1, const Vec3f& rVec2) {
    StarPieceHostInfo* hostInfo = findHostInfo(pNameObj);

    if (hostInfo == nullptr) {
        return false;
    }

    if (!hostInfo->isAppearable()) {
        return false;
    }

    StarPiece* hopPiece = getDeadStarPiece();

    if (hopPiece == nullptr) {
        return false;
    }

    hopPiece->setHostInfo(hostInfo);
    hostInfo->mNumAppeared++;
    hopPiece->appearHop(rVec1, rVec2);
    return true;
}

bool StarPieceDirector::appearPiece(const NameObj* pNameObj, const Vec3f& rPosition, s32 count, f32 randomRange, f32 speed,
                                    bool isForceEffectLight, bool notCheckInWater) {
    StarPieceHostInfo* hostInfo = findHostInfo(pNameObj);

    if (hostInfo == nullptr) {
        return false;
    }

    bool appearedStarPiece = false;

    for (int i = 0; i < count; i++) {
        if (!hostInfo->isAppearable()) {
            break;
        }

        StarPiece* appearPiece = getDeadStarPiece();

        if (appearPiece != nullptr) {
            appearedStarPiece = true;
            appearPiece->setHostInfo(hostInfo);
            hostInfo->mNumAppeared++;
            appearPiece->launch(rPosition, randomRange, speed, notCheckInWater, isForceEffectLight);
        }
    }

    return appearedStarPiece;
}

bool StarPieceDirector::appearPieceToDirection(const NameObj* pNameObj, const Vec3f& rPosition, const Vec3f& rUp, s32 count, f32 randomRange,
                                               f32 speed, bool isForceEffectLight, bool notCheckInWater) {
    StarPieceHostInfo* hostInfo = findHostInfo(pNameObj);

    if (hostInfo == nullptr) {
        return false;
    }

    bool appearedStarPiece = false;

    for (int i = 0; i < count; i++) {
        if (!hostInfo->isAppearable()) {
            break;
        }

        StarPiece* appearPiece = getDeadStarPiece();

        if (appearPiece == nullptr) {
            break;
        }

        appearedStarPiece = true;
        appearPiece->setHostInfo(hostInfo);
        hostInfo->mNumAppeared++;
        appearPiece->launch(rPosition, rUp, randomRange, speed, notCheckInWater, isForceEffectLight);
    }

    return appearedStarPiece;
}

bool StarPieceDirector::gotByPlayer() {
    if (MR::getStarPieceNum() >= 10000) {
        return false;
    }

    MR::addStarPiece(1);
    NebulaAchievements::countStat(NebulaStat_StarBits, 1);
    MR::startSystemSE("SE_SY_STAR_PIECE_GET");

    mQueueNewGetSound = true;
    MR::getStarPiecePlayer();
    return true;
}

void StarPieceDirector::movement() {
    mResetChasingStarPiece = false;
    updateSound();
}

// A get's sound plays 4/15 of a second after it: 16 steps at 60 a second, the same time at any step rate.
void StarPieceDirector::updateSound() {
    const u32 delay = (NebulaFrameClock::cStepsPerSecond * 4u + 7u) / 15u;
    if (mGetSounds.isDue(mSoundStep)) {
        MR::startCSSound("CS_STARDUST_GET", nullptr, -1);
        mGetSounds.pop();
    }

    if (mQueueNewGetSound) {
        mGetSounds.push(mSoundStep + delay);
    }

    mQueueNewGetSound = false;
    mSoundStep++;
}

void StarPieceDirector::initCSDelay() {
    mGetSounds.clear();
}

void StarPieceDirector::initCSSound() {
    mQueueNewGetSound = false;
    initCSDelay();
    mSoundStep = 0;
}

bool StarPieceDirector::giftToTarget(HitSensor* pGiftSensor, u32 numGift) {
    if (MR::getStarPieceNum() < numGift) {
        return false;
    }

    StarPieceReceiverInfo* receiverInfo = findReceiverInfo(pGiftSensor->mHost);

    if (receiverInfo == nullptr) {
        return false;
    }

    if (receiverInfo->mNumWanted < receiverInfo->mGotCount + receiverInfo->mNumFlying + numGift) {
        return false;
    }

    StarPiece* giftPiece = getDeadStarPiece();

    if (giftPiece == nullptr) {
        return false;
    }

    MR::requestMovementOn(giftPiece);
    receiverInfo->mNumFlying += numGift;
    MR::startCSSound("CS_FEED_SPIECE", "SE_SY_CS_FEED_PIECE", 0);
    giftPiece->giftToTarget(receiverInfo, numGift, pGiftSensor, calcPosCameraShoot(0));
    return true;
}

StarPiece* StarPieceDirector::getDeadStarPiece() {
    LiveActor* deadActor = getDeadActor();

    if (deadActor == nullptr) {
        createStarPieces(1);
        deadActor = getDeadActor();
    }

    StarPiece* deadStarPiece = static_cast< StarPiece* >(deadActor);

    deadStarPiece->setColor(mColorIndex);
    mColorIndex += 1;

    if (StarPiece::getNumColor() <= mColorIndex) {
        mColorIndex = 0;
    }

    return deadStarPiece;
}

Vec3f StarPieceDirector::calcPosCameraShoot(s32 addOrSubXDir) {
    Vec3f returnVec(MR::getCamPos());
    Vec3f camXDir(MR::getCamXdir());
    Vec3f camYDir(MR::getCamYdir());
    Vec3f camZDir(MR::getCamZdir());

    if (addOrSubXDir == 0) {
        returnVec.add(camXDir * cNum_calcPosCameraShoot_f1);
    } else {
        returnVec.sub(camXDir * cNum_calcPosCameraShoot_f2);
    }

    returnVec.add(camYDir * 0.0f);
    returnVec.add(camZDir * cNum_calcPosCameraShoot_f3);
    return returnVec;
}

void StarPieceDirector::clearGotCountReceiver(const NameObj* pNameObj) {
    StarPieceReceiverInfo* receiverInfo = findReceiverInfo(pNameObj);
    if (receiverInfo != nullptr) {
        receiverInfo->mGotCount = 0;
    }
}

StarPieceHostInfo* StarPieceDirector::findHostInfo(const NameObj* pNameObj) {
    for (s32 i = 0; i < mHostInfos.size(); i++) {
        if (pNameObj == mHostInfos[i]->mObj) {
            return mHostInfos[i];
        }
    }

    return nullptr;
}

StarPieceReceiverInfo* StarPieceDirector::findReceiverInfo(const NameObj* pNameObj) {
    for (s32 i = 0; i < mReceiverInfos.size(); i++) {
        if (pNameObj == mReceiverInfos[i]->mObj) {
            return mReceiverInfos[i];
        }
    }

    return nullptr;
}

void StarPieceDirector::createStarPieces(unsigned count) {
    NebulaArena::CurrentScope scope(Arenas::scene());
    for (unsigned i = 0; i < count; i++) {
        StarPiece* pStarPiece = new StarPiece("スターピースディレクターピース");
        pStarPiece->initWithoutIter();
        pStarPiece->makeActorDead();
        registerActor(pStarPiece);
        MR::registerDemoSimpleCastAll(pStarPiece);
    }
}

StarPieceShooter::StarPieceShooter(s32 port, const char* pName)
    : LiveActor(pName), mShootSpeed(), mSearchSensor(), mSearchDist(999999.0f), mLockOnSensor(), mPort(port), mChargeStep(), mEffectPos(0, 0, 0), mEffectRot(0, 0, 0) {
    lumacore::setClass(this, lumacore::kClassId< StarPieceShooter >);
}

void StarPieceShooter::init(const TableIter& /*rIter*/) {
    MR::connectToSceneMapObjMovementCalcAnim(this);
    MR::invalidateClipping(this);
    MR::offCalcGravity(this);

    initNerve(GET_NERVE(StarPieceShooter, HostTypeNrvWait));

    initEffectKeeper(0, "StarDust");
    MR::setEffectHostSRT(this, "Charge", &mEffectPos, &mEffectRot, nullptr);

    initHitSensor(1);
    MR::addHitSensorEye(this, "search", 64, cNum_init_f1, Vec3f(0.0f, 0.0f, 0.0f));
    makeActorAppeared();
}

void StarPieceShooter::exeWait() {
    if (mSearchSensor != nullptr) {
        mLockOnSensor = mSearchSensor;
        setNerve(GET_NERVE(StarPieceShooter, HostTypeNrvLockOn));
    }
}

void StarPieceShooter::exeLockOn() {
    if (mSearchSensor != nullptr) {
        mLockOnSensor = mSearchSensor;
        setNerve(GET_NERVE(StarPieceShooter, HostTypeNrvLockOn));
        return;
    }

    if (MR::isGreaterStep(this, 0)) {
        mLockOnSensor = nullptr;
        setNerve(GET_NERVE(StarPieceShooter, HostTypeNrvWait));
    }
}

void StarPieceShooter::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorMapObj(pReceiver) && !MR::isSensorEnemy(pReceiver)) {
        return;
    }

    if (MR::sendMsgLockOnStarPieceShoot(pReceiver, pSender)) {
        f32 distBetweenSensors = (pReceiver->mPosition - pSender->mPosition).length();

        if (distBetweenSensors < mSearchDist) {
            mSearchSensor = pReceiver;
            mSearchDist = distBetweenSensors;
        }
    }
}

void StarPieceShooter::control() {
    tryShoot();

    Vec3f playerCenterPos(*MR::getPlayerCenterPos());
    Vec3f playerUpVec;
    MR::getPlayerUpVec(&playerUpVec);

    mEffectPos.set(playerCenterPos);
    mEffectPos.sub(playerUpVec * -cNum_control_f1);

    f32 scale = mChargeStep / cNum_control_f2;
    scale *= cNum_control_f3;
    scale += cNum_control_f4;
    MR::setEffectBaseScale(this, "Charge", scale);
}

bool StarPieceShooter::shoot() {
    HitSensor* pSensor;
    StarPiece* const deadStarPiece = MR::getDeadStarPiece();

    if (deadStarPiece == nullptr) {
        return false;
    }

    pSensor = mSearchSensor;

    if (pSensor == nullptr) {
        pSensor = mLockOnSensor;
    }

    Vec3f position = mPosition;
    Vec3f vec;
    Vec3f posCameraShoot = MR::getStarPieceDirector()->calcPosCameraShoot(mPort);
    Vec3f negCameraPos = -MR::getCamPos();

    if (pSensor != nullptr) {
        negCameraPos.add(pSensor->mPosition);

        if (MR::getFirstPolyOnLineToMap(&vec, nullptr, MR::getCamPos(), negCameraPos) && vec.squared(MR::getCamPos()) < negCameraPos.squared()) {
            deadStarPiece->throwToTargetCore(vec, posCameraShoot, mGravity, mShootSpeed, true);
        } else {
            deadStarPiece->throwToTarget(pSensor, posCameraShoot, mGravity, mShootSpeed);
        }
    } else {
        negCameraPos.add(position);

        if (MR::getFirstPolyOnLineToMap(&vec, nullptr, MR::getCamPos(), negCameraPos) && vec.squared(MR::getCamPos()) < negCameraPos.squared()) {
            deadStarPiece->throwToTargetCore(vec, posCameraShoot, mGravity, mShootSpeed, true);
        } else {
            deadStarPiece->throwToTargetCore(position, posCameraShoot, mGravity, mShootSpeed, true);
        }
    }

    return true;
}

bool StarPieceShooter::tryShoot() {
    if (!((
              MR::isStarPointerInScreen(mPort)) &&
          MR::isStarPointerValid(mPort) && MR::getStarPieceNum() > 0 && MR::isEnableStarPointerShootStarPiece(mPort) &&
          (mPort != 0 || !MR::isStarPointer1PInvalid2PValidMode()))) {
        mSearchSensor = nullptr;
        mSearchDist = cNum_tryShoot_f1;
        mChargeStep = -1;
        MR::deleteEffect(this, "Charge");

        if ((MR::getStarPieceNum() <= 0) && MR::isStarPointerInScreen(mPort) && MR::isStarPointerValid(mPort) && MR::isPressedFire(mPort) &&
            (MR::isEnableStarPointerShootStarPiece(mPort) || MR::isStarPointerModeStarPieceTarget())) {
            MR::startCSSound("CS_NO_SPIECE", "SE_SY_CS_NO_PIECE", mPort);
        }

        return false;
    }

    calcShootGoalUsingPointingDepth();

    if (mChargeStep < 0) {
        if (!MR::isHeldFire(mPort)) {
            mChargeStep = 0;
        }
    } else if (!MR::isHeldFire(mPort)) {
        if (mChargeStep > 0) {
            MR::deleteEffect(this, "Charge");
        }

        mChargeStep = 0;
    } else {
        if (mChargeStep == 0) {
            if (MR::isPressedFire(mPort)) {
                if (StarPointerFunction::canShoot(mPort)) {
                    MR::emitEffect(this, "Charge");
                    MR::startCSSound("CS_STARDUST_CHARGE", nullptr, mPort);
                    mChargeStep++;
                } else {
                    mChargeStep = -1;
                }
            }
        } else if (MR::isHeldFire(mPort)) {
            mChargeStep++;
        }

        s32 shootStep;

        for (int i = 0; i < 2; i++) {
            shootStep = 7;

            if (i == 0) {
                shootStep = 1;
            }

            shootStep += i * 7;

            if (shootStep == mChargeStep) {
                shoot();
                MR::deleteEffect(this, "Charge");
                MR::tryRumblePadMiddle(this, mPort);
                MR::startCSSound("CS_STARDUST_SHOOT", "SE_SY_CS_STAR_PIECE_SHOOT", mPort);

                if (i == 1) {
                    mChargeStep = 1;
                }

                break;
            }
        }
    }

    mSearchSensor = nullptr;
    mSearchDist = cNum_tryShoot_f2;
    return true;
}

void StarPieceShooter::calcShootGoalUsingPointingDepth() {
    Vec3f playerPos(*MR::getPlayerPos());
    Vec3f starPointerWorldPos(*MR::getStarPointerWorldPosUsingDepth(mPort));

    mGravity.set(*MR::getPlayerGravity());

    Vec3f dir(starPointerWorldPos);
    dir.sub(playerPos);

    f32 dirsquared = dir.squared();

    if (cNum_calcShootGoalUsingPointingDepth_f1 < dirsquared) {
        Vec3f camPos(MR::getCamPos());
        Vec3f dirToPlayer(playerPos);
        dirToPlayer.sub(camPos);

        Vec2f pointingPos;
        MR::getPointerPosOnScreen(&pointingPos, mPort);

        Vec3f worldPos;
        MR::calcWorldPositionFromScreen(&worldPos, pointingPos, -1.0f);
        worldPos.sub(camPos);
        MR::normalize(&worldPos);

        starPointerWorldPos.set(camPos);

        starPointerWorldPos.add(worldPos * (cNum_calcShootGoalUsingPointingDepth_f2 + dirToPlayer.length()));
    } else if (cNum_calcShootGoalUsingPointingDepth_f3 < dirsquared) {
        dir.setLength(cNum_calcShootGoalUsingPointingDepth_f4);
        starPointerWorldPos.set(playerPos);
        starPointerWorldPos.add(dir);
    }

    mPosition.set(starPointerWorldPos);
    mShootSpeed = cNum_calcShootGoalUsingPointingDepth_f5;
}
