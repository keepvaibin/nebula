#include "Game/Enemy/MogucchiHill.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const char* const sPieceModelTable[] = {
        "MogucchiHillA",
        "MogucchiHillB",
        "MogucchiHillC",
    };
    const f32 sPieceSensorRadius = 120.0f;
    const f32 sMinScale = 0.6f;
    const s32 sCrumbleFrame = 40;
    const f32 sAppearDist = 300.0f;
};  // namespace

namespace {
    NEW_NERVE(MogucchiHillPieceNrvPieceAppear, MogucchiHillPiece, Appear);
    NEW_NERVE(MogucchiHillPieceNrvPieceWait, MogucchiHillPiece, Wait);
    NEW_NERVE(MogucchiHillPieceNrvPieceCrumble, MogucchiHillPiece, Crumble);
};  // namespace

namespace {
    NEW_NERVE(MogucchiHillNrvMove, MogucchiHill, Move);
    NEW_NERVE(MogucchiHillNrvWait, MogucchiHill, Wait);
};  // namespace

MogucchiHillPiece::MogucchiHillPiece(const char* pName) : LiveActor(pName), mHost(), mModelName(), mJointName(), mAutoEffectName(), mIsHostLeft(), mIsHostTouching() {
    lumacore::setClass(this, lumacore::kClassId< MogucchiHillPiece >);
    mMtx.identity();
}

void MogucchiHillPiece::init(const TableIter& /*rIter*/) {
    initModelManagerWithAnm(mModelName, nullptr, false);
    MR::connectToSceneMapObjDecorationStrongLight(this);
    initHitSensor(1);
    MR::addHitSensorAtJointMapObj(this, "body", mJointName, 8, mScale.x * ::sPieceSensorRadius, Vec3f(0.0f, 0.0f, 0.0f));

    if (mAutoEffectName != nullptr) {
        initEffectKeeper(0, mAutoEffectName);
    }

    if (mHasLightCtrl) {
        MR::initLightCtrl(this);
    }

    initSound(1, false);
    initNerve(GET_NERVE_ANON(MogucchiHillPieceNrvPieceWait));
    makeActorDead();
}

void MogucchiHillPiece::control() {
    f32 sqrDist = (mPosition - *MR::getPlayerCenterPos()).squared();

    if (MR::isPlayerHipDropLand() && sqrDist < ::sAppearDist * ::sAppearDist) {
        if (MR::isExistEffectKeeper(this)) {
            MR::emitEffect(this, "Break");
        }

        kill();
    }
}

void MogucchiHillPiece::appear() {
    LiveActor::appear();
    setNerve(GET_NERVE_ANON(MogucchiHillPieceNrvPieceAppear));
    MR::hideModel(this);

    mIsHostLeft = false;

    MR::validateClipping(this);
}

void MogucchiHillPiece::initWithModelName(const char* pModelName, const char* pJointName, const char* pAutoEffectName, bool hasLightCtrl) {
    mModelName = pModelName;
    mJointName = pJointName;
    mAutoEffectName = pAutoEffectName;
    mHasLightCtrl = hasLightCtrl;

    initWithoutIter();
}

void MogucchiHillPiece::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::showModel(this);
        MR::startBck(this, "Lead");
        MR::startBrk(this, "Normal");
        MR::startSound(this, "SE_EM_MOGHILL_APPEAR");
    }

    MR::setNerveAtBckStopped(this, GET_NERVE_ANON(MogucchiHillPieceNrvPieceWait));
}

void MogucchiHillPiece::exeWait() {
    if (mHost->isNerve(GET_NERVE_ANON(MogucchiHillNrvWait))) {
        return;
    }

    if (!mIsHostTouching) {
        mIsHostLeft = true;
    }

    mIsHostTouching = false;
}

void MogucchiHillPiece::exeCrumble() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Fade");
        MR::startBrk(this, "Fade");
    }

    if (MR::isBckStopped(this)) {
        kill();
    }
}

void MogucchiHillPiece::crumble() {
    if (MR::isDead(this)) {
        return;
    }

    if (MR::isClipped(this)) {
        kill();
        return;
    }

    if (isNerve(GET_NERVE_ANON(MogucchiHillPieceNrvPieceCrumble))) {
        return;
    }

    MR::invalidateClipping(this);
    setNerve(GET_NERVE_ANON(MogucchiHillPieceNrvPieceCrumble));
}

void MogucchiHillPiece::destroy() {
    if (MR::isExistEffectKeeper(this)) {
        MR::emitEffect(this, "Break");
    }

    kill();
}

void MogucchiHillPiece::setSize(f32 size) {
    mScale.setAll< f32 >(size);
    HitSensor* pSensor = getSensor("body");
    pSensor->mRadius = ::sPieceSensorRadius * size;
}

void MogucchiHillPiece::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mMtx);
}

void MogucchiHillPiece::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    bool isCrumbleEnd = isNerve(GET_NERVE_ANON(MogucchiHillPieceNrvPieceCrumble)) && MR::isGreaterEqualStep(this, ::sCrumbleFrame);

    if (isCrumbleEnd) {
        return;
    }

    if (MR::isSensorPlayerOrRide(pReceiver)) {
        if (MR::isPlayerDamaging()) {
            destroy();
            return;
        }

        if (isNerve(GET_NERVE_ANON(MogucchiHillPieceNrvPieceAppear)) && mHost->notifyAppearAttackToPlayer(pSender, pReceiver)) {
            return;
        }

        if (isNerve(GET_NERVE_ANON(MogucchiHillPieceNrvPieceWait)) && mHost->notifyWaitAttackToPlayer(pSender, pReceiver)) {
            return;
        }

        if (MR::isPlayerHipDropFalling()) {
            return;
        }

        if (isTargetGoingAway(pSender, pReceiver)) {
            return;
        }
    }

    MR::sendMsgPush(pReceiver, pSender);
}

bool MogucchiHillPiece::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isDead(this)) {
        return false;
    }

    bool isCrumbleEnd = isNerve(GET_NERVE_ANON(MogucchiHillPieceNrvPieceCrumble)) && MR::isGreaterEqualStep(this, ::sCrumbleFrame);

    if (isCrumbleEnd) {
        return false;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        MR::startSound(this, "SE_EM_MOGHILL_BREAK");
        destroy();

        return true;
    }

    if (MR::isMsgPlayerHipDrop(msg)) {
        MR::tryRumblePadWeak(this, PLAYER_1);
        MR::startSound(this, "SE_EM_MOGHILL_BREAK");
        destroy();

        return false;
    }

    if (MR::isMsgPlayerTrample(msg)) {
        MR::tryRumblePadWeak(this, PLAYER_1);
        MR::startSound(this, "SE_EM_MOGHILL_BREAK");
        destroy();

        return true;
    }

    bool isHitAll = false;

    if (MR::isMsgPlayerHitAll(msg)) {
        isHitAll = true;
    }

    if (isHitAll) {
        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::startSound(this, "SE_EM_MOGHILL_BREAK");
        mHost->killPieces(this);

        return true;
    }

    return false;
}

bool MogucchiHillPiece::receiveMsgEnemyAttack(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    bool isCrumbleEnd = isNerve(GET_NERVE_ANON(MogucchiHillPieceNrvPieceCrumble)) && MR::isGreaterEqualStep(this, ::sCrumbleFrame);

    if (isCrumbleEnd) {
        return false;
    }

    mIsHostTouching = true;

    if (!mIsHostLeft) {
        return false;
    }

    MR::tryRumblePadWeak(this, PLAYER_1);
    MR::startSound(this, "SE_EM_MOGHILL_BREAK_SOFT");
    destroy();

    return true;
}

bool MogucchiHillPiece::isTargetGoingAway(HitSensor* pSender, HitSensor* pReceiver) const {
    Vec3f toReceiver = pReceiver->mPosition - pSender->mPosition;

    return toReceiver.dot(pReceiver->mHost->mVelocity) >= 0.0f;
}

MogucchiHill::MogucchiHill(LiveActor* pHost, s32 pieceCount, const char* pName)
    : LiveActor(pName), mHost(pHost), mPieces(), mPieceNum(pieceCount), mPieceIndex(), mHasReserveMtx(), mAppearDistance(100.0f), mAppearDistanceMax(150.0f), mModelNum(3), mModelNameTable(::sPieceModelTable),
      mJointName("MogucchiHill"), mAutoEffectName("MogucchiHill"), mHasLightCtrl(), mSizeAnimState(), mSizeStep(), mAppearNum() {
    lumacore::setClass(this, lumacore::kClassId< MogucchiHill >);
    if (pieceCount > 100) {
        mPieceNum = 100;
    }

    mAppearNum = mPieceNum;

    mReserveMtx.identity();
}

void MogucchiHill::init(const TableIter& /*rIter*/) {
    MR::connectToSceneEnemyMovement(this);
    initNerve(GET_NERVE_ANON(MogucchiHillNrvMove));
    createPieces();
    makeActorDead();
}

void MogucchiHill::start() {
    if (MR::isDead(this)) {
        MR::invalidateClipping(this);
        appear();
    }

    setNerve(GET_NERVE_ANON(MogucchiHillNrvMove));

    mSizeStep = 3;
}

void MogucchiHill::end() {
    setNerve(GET_NERVE_ANON(MogucchiHillNrvWait));
}

void MogucchiHill::startNaturally() {
    mSizeAnimState = 1;

    start();
}

void MogucchiHill::endNaturally() {
    mSizeAnimState = 2;
}

void MogucchiHill::killPieces(MogucchiHillPiece* pIter) {
    s32 index = pIter - mPieces - 2;

    for (s32 i = 0; i < 3; i++) {
        index = mPieceNum != 0 ? (index + mPieceNum + 1) % mPieceNum : index + 1;

        if (MR::isDead(&mPieces[index])) {
            continue;
        }

        mPieces[index].destroy();
    }
}

void MogucchiHill::pauseOff() {
    MR::requestMovementOn(this);

    for (u32 i = 0; i < mPieceNum; i++) {
        MR::requestMovementOn(&mPieces[i]);
    }
}

void MogucchiHill::killAllPieces() {
    for (u32 i = 0; i < mPieceNum; i++) {
        mPieces[i].makeActorDead();
    }
}

void MogucchiHill::setAppearNum(s32 appearNum) {
    mAppearNum = appearNum;
}

void MogucchiHill::exeMove() {
    if (MR::isFirstStep(this)) {
        reserveCurrentMtx();
    }

    if (mHasReserveMtx && getDistanceFromReserveMtx() >= mAppearDistance) {
        appearPiece();
    }

    if (getDistanceFromReserveMtx() >= mAppearDistanceMax) {
        reserveCurrentMtx();
    }
}

void MogucchiHill::exeWait() {
    mSizeStep = 0;
}

void MogucchiHill::createPieces() {
    mPieces = new MogucchiHillPiece[mPieceNum]();

    s32 modelCount = mModelNum;
    s32 previousModel = MR::getRandom(0, modelCount);
    s32 secondPreviousModel = -2;
    s32 model;
    s32 thirdPreviousModel = -3;

    for (u32 i = 0; i < mPieceNum; i++) {
        s32 offset = MR::getRandom(0, modelCount - 1);
        model = previousModel + offset + 1;
        model = (model + modelCount) % modelCount;

        if (previousModel == thirdPreviousModel && secondPreviousModel == model) {
            s32 alternativeCount = modelCount - 1;
            offset = (offset + alternativeCount + 1) % alternativeCount;
            model = previousModel + offset + 1;
            model = (model + modelCount) % modelCount;
        }

        thirdPreviousModel = secondPreviousModel;
        secondPreviousModel = previousModel;
        previousModel = model;

        mPieces[i].mHost = this;
        mPieces[i].mScale.setAll< f32 >(mScale.x);
        mPieces[i].initWithModelName(mModelNameTable[model], mJointName, mAutoEffectName, mHasLightCtrl);
    }
}

void MogucchiHill::appearPiece() {
    killPieceOverAppear();
    killPieceIfAlive();

    switch (mSizeAnimState) {
    case 0:
        break;
    case 1:
        mSizeStep++;

        if (mSizeStep >= 3) {
            mSizeStep = 3;
            mSizeAnimState = 0;
        }

        break;
    case 2:
        mSizeStep--;

        if (mSizeStep <= 0) {
            mSizeStep = 0;
            setNerve(GET_NERVE_ANON(MogucchiHillNrvWait));
            mSizeAnimState = 0;
            return;
        }

        break;
    }

    if (mSizeStep != 0) {
        mPieces[mPieceIndex].setSize(mScale.x * (::sMinScale + (1.0f - ::sMinScale) * (mSizeStep - 1) / 2.0f));
        MogucchiHillPiece* pPiece = &mPieces[mPieceIndex];
        pPiece->mMtx.setInline(mReserveMtx);
        pPiece->mMtx.getTrans(pPiece->mPosition);
        pPiece->appear();
        s32 count = mPieceNum;
        s32 index = mPieceIndex;
        mHasReserveMtx = false;
        mPieceIndex = count != 0 ? (index + count + 1) % count : index + 1;
    }
}

void MogucchiHill::killPieceOverAppear() {
    s32 index = mPieceNum != 0 ? (mPieceNum + (mPieceIndex - mAppearNum)) % mPieceNum : mPieceIndex - mAppearNum;

    mPieces[index].crumble();
}

void MogucchiHill::killPieceIfAlive() {
    if (MR::isDead(&mPieces[mPieceIndex])) {
        return;
    }

    mPieces[mPieceIndex].kill();
}

f32 MogucchiHill::getDistanceFromReserveMtx() const {
    Vec3f trans;
    mReserveMtx.getTrans(trans);

    return mHost->mPosition.distance(trans);
}

void MogucchiHill::reserveCurrentMtx() {
    mReserveMtx.setInline(mHost->getBaseMtx());
    mHasReserveMtx = true;
}
