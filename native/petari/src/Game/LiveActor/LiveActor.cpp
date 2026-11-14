#include "Game/LiveActor/LiveActor.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Util/MotionSignal.hpp"
#include "Game/AudioLib/AudAnmSoundObject.hpp"
#include "Game/LiveActor/ActorAnimKeeper.hpp"
#include "Game/LiveActor/ActorLightCtrl.hpp"
#include "Game/LiveActor/ActorPadAndCameraCtrl.hpp"
#include "Game/LiveActor/AllLiveActorGroup.hpp"
#include "Game/LiveActor/Binder.hpp"
#include "Game/LiveActor/ClippingDirector.hpp"
#include "Game/LiveActor/EffectKeeper.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/HitSensorKeeper.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/LiveActor/RailRider.hpp"
#include "Game/LiveActor/ShadowController.hpp"
#include "Game/LiveActor/Spine.hpp"
#include "Game/Map/StageSwitch.hpp"
#include "Game/NameObj/NameObjExecuteHolder.hpp"
#include "Game/Screen/StarPointerTarget.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_module.h>
#include "Game/System/LumaCore.hpp"

LiveActor::LiveActor(const char* pName)
    : NameObj(pName), mPosition(0.0f, 0.0f, 0.0f), mRotation(0.0f, 0.0f, 0.0f), mScale(1.0f, 1.0f, 1.0f), mVelocity(0.0f, 0.0f, 0.0f),
      mGravity(0.0f, -1.0f, 0.0f), mModelManager(), mAnimKeeper(), mSpine(), mSensorKeeper(), mBinder(), mRailRider(), mEffectKeeper(),
      mSoundObject(), mShadowControllerList(), mCollisionParts(), mStageSwitchCtrl(), mStarPointerTarget(), mActorLightCtrl(), mCameraCtrl() {
    lumacore::setClass(this, lumacore::kClassId< LiveActor >);
    MR::getAllLiveActorGroup()->registerActor(this);
    MR::getClippingDirector()->registerActor(this);
}

void LiveActor::init(const TableIter& /*rIter*/) {
}

void LiveActor::appear() {
    makeActorAppeared();
}

void LiveActor::makeActorAppeared() {
    NebulaMods::fire(NEBULA_MOD_EVENT_ACTOR_APPEAR, 0, this, mName);
    if (mSensorKeeper != nullptr) {
        mSensorKeeper->validateBySystem();
    }

    if (MR::isClipped(this)) {
        endClipped();
    }

    mFlag.mIsDead = false;

    if (mCollisionParts != nullptr) {
        MR::validateCollisionParts(this);
    }

    MR::resetPosition(this);

    if (mActorLightCtrl != nullptr) {
        mActorLightCtrl->reset();
    }

    MR::tryUpdateHitSensorsAll(this);
    MR::addToClippingTarget(this);
    MR::connectToSceneTemporarily(this);

    if (!MR::isNoEntryDrawBuffer(this)) {
        MR::connectToDrawTemporarily(this);
    }
}

void LiveActor::kill() {
    makeActorDead();
}

void LiveActor::makeActorDead() {
    NebulaMods::fire(NEBULA_MOD_EVENT_ACTOR_KILL, 0, this, mName);
    MotionSignal::died(this);
    mVelocity.zero();

    MR::clearHitSensors(this);

    if (mSensorKeeper != nullptr) {
        mSensorKeeper->invalidateBySystem();
    }

    if (getBinder() != nullptr) {
        mBinder->clear();
    }

    if (mEffectKeeper != nullptr) {
        mEffectKeeper->clear();
    }

    if (mCollisionParts != nullptr) {
        MR::invalidateCollisionParts(this);
    }

    mFlag.mIsDead = true;
    mFlag.mIsDrawnClipped = false;

    MR::removeFromClippingTarget(this);
    MR::disconnectToSceneTemporarily(this);
    MR::disconnectToDrawTemporarily(this);
}

void LiveActor::movement() {
    if (mModelManager != nullptr && !mFlag.mIsStoppedAnim) {
        mModelManager->update();

        if (mAnimKeeper != nullptr) {
            mAnimKeeper->update();
        }
    }

    if (MR::isCalcGravity(this)) {
        MR::calcGravity(this);
    }

    if (mSensorKeeper != nullptr) {
        mSensorKeeper->doObjCol();
    }

    if (mFlag.mIsDead) {
        return;
    }

    if (mSpine != nullptr) {
        mSpine->update();
    }

    if (mFlag.mIsDead) {
        return;
    }

    control();

    if (mFlag.mIsDead) {
        return;
    }

    updateBinder();

    if (mEffectKeeper != nullptr) {
        mEffectKeeper->update();
    }

    if (mCameraCtrl != nullptr) {
        mCameraCtrl->update();
    }

    if (mActorLightCtrl != nullptr) {
        MR::updateLightCtrl(this);
    }

    MR::tryUpdateHitSensorsAll(this);
    MR::actorSoundMovement(this);
    MR::requestCalcActorShadow(this);
}

void LiveActor::calcAnim() {
    if (mFlag.mIsNoCalcAnim) {
        return;
    }

    mFlag.mHasPose = true;
    calcAnmMtx();

    if (mCollisionParts == nullptr) {
        return;
    }

    MR::setCollisionMtx(this);
}

void LiveActor::calcAnmMtx() {
    if (mModelManager == nullptr) {
        return;
    }

    MR::getModel(this)->setBaseScale(mScale);
    calcAndSetBaseMtx();
    mModelManager->calcAnim();
}

void LiveActor::calcViewAndEntry() {
    if (mFlag.mIsNoCalcView) {
        return;
    }

    if (mModelManager == nullptr) {
        return;
    }

    mModelManager->calcView();
}

bool LiveActor::receiveMessage(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (msg == ACTMES_PUSH) {
        return receiveMsgPush(pSender, pReceiver);
    }

    bool isMsgPlayerAttack = msg > ACTMES_PLAYER_ATTACK_START && msg < ACTMES_PLAYER_ATTACK_END;

    if (isMsgPlayerAttack) {
        return receiveMsgPlayerAttack(msg, pSender, pReceiver);
    }

    bool isMsgEnemyAttack = msg > ACTMES_ENEMY_ATTACK_START && msg < ACTMES_ENEMY_ATTACK_END;

    if (isMsgEnemyAttack) {
        return receiveMsgEnemyAttack(msg, pSender, pReceiver);
    }

    if (msg == ACTMES_TAKE) {
        return receiveMsgTake(pSender, pReceiver);
    }

    if (msg == ACTMES_TAKEN) {
        return receiveMsgTaken(pSender, pReceiver);
    }

    if (msg == ACTMES_THROW) {
        return receiveMsgThrow(pSender, pReceiver);
    }

    if (msg == ACTMES_APART) {
        return receiveMsgApart(pSender, pReceiver);
    }

    return receiveOtherMsg(msg, pSender, pReceiver);
}

void LiveActor::calcAndSetBaseMtx() {
    if (MR::getTaken(this)) {
        MR::setBaseTRMtx(this, MR::getTaken(this)->mHost->getTakingMtx());
    } else {
        Pos3f mtx;

        if (mRotation.x == 0.0f && mRotation.z == 0.0f) {
            MR::makeMtxTransRotateY(mtx, this);
        } else {
            MR::makeMtxTR(mtx, this);
        }

        MR::setBaseTRMtx(this, mtx);
    }
}

MtxPtr LiveActor::getTakingMtx() const {
    return getBaseMtx();
}

void LiveActor::setNerve(const Nerve* pNerve) {
    mSpine->setNerve(pNerve);
}

bool LiveActor::isNerve(const Nerve* pNerve) const {
    return mSpine->getCurrentNerve() == pNerve;
}

s32 LiveActor::getNerveStep() const {
    return mSpine->mStep;
}

HitSensor* LiveActor::getSensor(const char* pName) const {
    if (mSensorKeeper != nullptr) {
        return mSensorKeeper->getSensor(pName);
    }

    return nullptr;
}

MtxPtr LiveActor::getBaseMtx() const {
    if (MR::getModel(this) != nullptr) {
        return MR::getModel(this)->mBaseTransformMtx;
    }

    return nullptr;
}

LiveActor::ClipScope LiveActor::sClipScope = LiveActor::ClipScope_All;

void LiveActor::startClipped() {
    mFlag.mIsClipped = true;

    if (sClipScope != ClipScope_Seen) {
        if (getSensorKeeper() != nullptr) {
            mSensorKeeper->invalidateBySystem();
        }

        MR::disconnectToSceneTemporarily(this);
    }

    if (sClipScope == ClipScope_Run) {
        return;
    }

    if (mEffectKeeper != nullptr) {
        mEffectKeeper->stopEmitterOnClipped();
    }

    if (MR::isNoEntryDrawBuffer(this)) {
        return;
    }

    MR::disconnectToDrawTemporarily(this);
}

void LiveActor::endClipped() {
    if (sClipScope == ClipScope_All) {
        mFlag.mIsDrawnClipped = false;
    }

    if (sClipScope != ClipScope_Seen) {
        mFlag.mIsClipped = false;

        if (getSensorKeeper() != nullptr) {
            mSensorKeeper->validateBySystem();
            MR::updateHitSensorsAll(this);
        }

        MR::connectToSceneTemporarily(this);
    }

    if (sClipScope == ClipScope_Run) {
        return;
    }

    if (mEffectKeeper != nullptr) {
        mEffectKeeper->playEmitterOffClipped();
    }

    if (MR::isNoEntryDrawBuffer(this)) {
        return;
    }

    MR::connectToDrawTemporarily(this);
}

// The model as its actor would show it standing still, for an actor seen before it has run: its place and its light.
// Only the base class's own code: an actor's calcAndSetBaseMtx may read what its control sets.
void LiveActor::prepareDrawClipped() {
    if (!mFlag.mHasPose && mModelManager != nullptr) {
        LiveActor::calcAndSetBaseMtx();
        MR::calcModel(this);
    }

    if (mActorLightCtrl != nullptr) {
        MR::updateLightCtrlDirect(this);
    }
}

void LiveActor::initModelManagerWithAnm(const char* pModelName, const char* pAnimName, bool deferMaterials) {
    mModelManager = new ModelManager();
    // A class of the mod's: its files in place of those the object it is made as asks for.
    const char* const pAskedName = pAnimName != nullptr ? pAnimName : pModelName;
    pModelName = NebulaMods::modelFor(mName, pModelName);
    if (pAnimName != nullptr) {
        pAnimName = NebulaMods::modelFor(mName, pAnimName);
    }
    mModelManager->init(pModelName, pAnimName, deferMaterials);

    // The tables and sound animations its archive lacks are those of the archive it stands for.
    ResourceHolder* const pHolder = mModelManager->getResourceHolder();
    const char* const pGivenName = pAnimName != nullptr ? pAnimName : pModelName;
    if (pGivenName != pAskedName && pHolder->mLender == nullptr && MR::isExistObjectOrLayoutArchive(pAskedName)) {
        pHolder->mLender = MR::createAndAddResourceHolder(pAskedName);
        pHolder->takePlayModesFromLender();
    }

    MR::getModel(this)->setBaseScale(mScale);
    LiveActor::calcAndSetBaseMtx();
    MR::calcModel(this);

    mAnimKeeper = ActorAnimKeeper::tryCreate(this);
    mCameraCtrl = ActorPadAndCameraCtrl::tryCreate(mModelManager, &mPosition);
}

void LiveActor::initNerve(const Nerve* pNerve) {
    mSpine = new Spine(this, pNerve);
}

void LiveActor::initHitSensor(int numSensors) {
    mSensorKeeper = new HitSensorKeeper(numSensors);
}

void LiveActor::initBinder(f32 radius, f32 offsetY, u32 planeNum) {
    mBinder = new Binder(getBaseMtx(), &mPosition, &mGravity, radius, offsetY, planeNum);

    MR::onBind(this);

    if (mEffectKeeper != nullptr) {
        mEffectKeeper->setBinder(mBinder);
    }
}

void LiveActor::initRailRider(const TableIter& rIter) {
    mRailRider = new RailRider(rIter);
}

void LiveActor::initEffectKeeper(int extraEmitterNum, const char* pResourceName) {
    mEffectKeeper = new EffectKeeper(getName(), MR::getModelResourceHolder(this), extraEmitterNum, pResourceName);
    mEffectKeeper->init(this);

    if (mBinder != nullptr) {
        mEffectKeeper->setBinder(mBinder);
    }
}

void LiveActor::initSound(int numHandles, bool is2D) {
    if (!is2D) {
        mSoundObject = new AudAnmSoundObject(&mPosition, numHandles, NebulaArena::current());
    } else {
        mSoundObject = new AudAnmSoundObject(nullptr, numHandles, NebulaArena::current());
    }
}

void LiveActor::initShadowControllerList(u32 shadowNum) {
    mShadowControllerList = new ShadowControllerList(this, shadowNum);
}

void LiveActor::initActorCollisionParts(const char* pName, HitSensor* pSensor, ResourceHolder* pResHolder, MtxPtr pMtx, bool isAutoEqualScale,
                                        bool isNotUsingScale) {
    pName = NebulaMods::modelFor(mName, pName);
    MR::CollisionScaleType scaleType;

    if (isNotUsingScale) {
        scaleType = MR::CollisionScaleType_NotUsingScale;
    } else if (isAutoEqualScale) {
        scaleType = MR::CollisionScaleType_AutoEqualScale;
    } else {
        scaleType = MR::CollisionScaleType_Default;
    }

    // The collision is the archive's <name>.kcl. An archive without it (a mod's model with no collision beside it)
    // gives the actor no collision, said once here, and the game runs on: the file is never read through.
    const ResourceHolder* const pFrom = pResHolder != nullptr ? pResHolder : MR::getResourceHolder(this);
    if (pFrom == nullptr || !pFrom->mFileInfoTable->isExistRes(MR::FormattedName("%s.kcl", pName).c_str())) {
        nebula_log("actor %s: its collision %s.kcl is not in its archive; it has no collision\n", mName != nullptr ? mName : "",
                   pName != nullptr ? pName : "");
        return;
    }

    if (pResHolder != nullptr) {
        Pos3f mtx;

        if (pMtx != nullptr) {
            mtx.set(pMtx);
        } else {
            MR::makeMtxTRS(mtx.toMtxPtr(), this);
        }

        mCollisionParts = MR::createCollisionPartsFromResourceHolder(pResHolder, pName, pSensor, mtx, scaleType);
    } else if (pMtx == nullptr) {
        mCollisionParts = MR::createCollisionPartsFromLiveActor(this, pName, pSensor, scaleType);
    } else {
        mCollisionParts = MR::createCollisionPartsFromLiveActor(this, pName, pSensor, pMtx, scaleType);
    }

    MR::invalidateCollisionParts(this);
}

void LiveActor::initStageSwitch(const TableIter& rIter) {
    mStageSwitchCtrl = MR::createStageSwitchCtrl(this, rIter);
}

void LiveActor::initActorStarPointerTarget(f32 radius, const Vec3f* pTrans, MtxPtr pMtx, Vec3f offset) {
    mStarPointerTarget = new StarPointerTarget(radius, pTrans, pMtx, offset);
}

void LiveActor::initActorLightCtrl() {
    mActorLightCtrl = new ActorLightCtrl(this);
}

void LiveActor::attackSensor(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
}

bool LiveActor::receiveMsgApart(HitSensor* pSender, HitSensor* pReceiver) {
    MR::setHitSensorApart(pSender, pReceiver);

    return true;
}

void LiveActor::addToSoundObjHolder() {
    mSoundObject->addToSoundObjHolder();
}

void LiveActor::updateBinder() {
    if (mBinder == nullptr) {
        mPosition += mVelocity;
    } else if (mFlag.mIsNoBind) {
        mPosition += mVelocity;
        mBinder->clear();
    } else {
        mPosition += getBinder()->bind(mVelocity);
    }
}
