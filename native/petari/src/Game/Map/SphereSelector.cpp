#include "Game/Map/SphereSelector.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/SphereSelectorHandle.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    inline bool isWaitingForClick(const SphereSelector* pSelector) {
        return pSelector->mClickStep < 0;
    }

    SphereSelector* getSphereSelector() {
        return MR::getSceneObj< SphereSelector >(SceneObj_SphereSelector);
    }
};  // namespace

namespace NrvSphereSelector {
    NEW_NERVE(SphereSelectorNrvSelectStart, SphereSelector, SelectStart);
    NEW_NERVE(SphereSelectorNrvSelectWait, SphereSelector, SelectWait);
    NEW_NERVE(SphereSelectorNrvSelectCancel, SphereSelector, SelectCancel);
    NEW_NERVE(SphereSelectorNrvConfirmStart, SphereSelector, ConfirmStart);
    NEW_NERVE(SphereSelectorNrvConfirmWait, SphereSelector, ConfirmWait);
    NEW_NERVE(SphereSelectorNrvConfirmCancel, SphereSelector, ConfirmCancel);
    NEW_NERVE(SphereSelectorNrvConfirmed, SphereSelector, Confirm);
};  // namespace NrvSphereSelector

SphereSelector::SphereSelector()
    : LiveActor("スフィアセレクター"), mSphereGroup(), mHandle(), mSelectedTarget(), mNearestTarget(), mPointingTarget(), mClickStep(), mClickPos(0.0f, 0.0f),
      mIsPointingInvalid(), mIsSePlayed() {
    lumacore::setClass(this, lumacore::kClassId< SphereSelector >);
}

void SphereSelector::init(const TableIter& /*rIter*/) {
    MR::connectToSceneMapObjDecorationMovement(this);
    MR::invalidateClipping(this);
    mSphereGroup = new LiveActorGroup("スフィアセレクターアクターグループ", 32);
    initNerve(GET_NERVE(SphereSelector, SphereSelectorNrvSelectStart));
    makeActorDead();
}

void SphereSelector::appear() {
    LiveActor::appear();
    mSelectedTarget = nullptr;
    mNearestTarget = nullptr;
    mPointingTarget = nullptr;
    mIsPointingInvalid = false;
    MR::deactivateDefaultGameLayout();
    MR::startStarPointerModeSphereSelectorFinger(this);
    setNerve(GET_NERVE(SphereSelector, SphereSelectorNrvSelectStart));
}

void SphereSelector::kill() {
    sendMsgToAllActor(ACTMES_SPHERE_SELECTOR_SELECT_END);
    LiveActor::kill();
    MR::activateDefaultGameLayout();
    MR::endStarPointerMode(this);
    MR::endStarPointerMode(this);
}

void SphereSelector::registerPointingTarget(LiveActor* pActor, HandlePointingPriority priority) {
    if (mIsPointingInvalid) {
        return;
    }

    if (!isNerve(GET_NERVE(SphereSelector, SphereSelectorNrvSelectWait))) {
        return;
    }

    u32 distance;
    switch (priority) {
    case HandlePointingPriority_Last:
        distance = -1;
        break;
    case HandlePointingPriority_First:
        distance = 0;
        break;
    default:
        Vec3f viewPos;
        MR::getCameraViewMtx().mult(pActor->mPosition, viewPos);
        if (0.0f < viewPos.z) {
            return;
        }

        distance = -viewPos.z;
        break;
    }

    if (mNearestTarget == nullptr || mNearestDist > distance) {
        mNearestTarget = pActor;
        mNearestDist = distance;
    }
}

void SphereSelector::validatePointing() {
    mIsPointingInvalid = false;
    mHandle->validateRotate();
    MR::startStarPointerModeSphereSelectorOnReaction(this);
}

void SphereSelector::invalidatePointing() {
    mIsPointingInvalid = true;
    mHandle->invalidateRotate();
    MR::endStarPointerMode(this);
}

void SphereSelector::sendMsgToAllActor(u32 msg) {
    for (int i = 0; i < mSphereGroup->getObjNum(); i++) {
        MR::sendSimpleMsgToActor(msg, mSphereGroup->getActor(i));
    }
}

bool SphereSelector::isMoveClickedPos() const {
    return 80.0f < FloatUtil::sqrt(mClickPos.squareDist(MR::getStarPointerScreenPositionOrEdge(0)));
}

void SphereSelector::playSelectedME() {
    switch (MR::getRandom(0, 4)) {
    case 0:
        MR::startSystemME("ME_ASTRO_DOME_SELECT1");
        break;
    case 1:
        MR::startSystemME("ME_ASTRO_DOME_SELECT2");
        break;
    case 2:
        MR::startSystemME("ME_ASTRO_DOME_SELECT3");
        break;
    case 3:
        MR::startSystemME("ME_ASTRO_DOME_SELECT4");
        break;
    }
}

void SphereSelector::playCanceledME() {
    switch (MR::getRandom(0, 4)) {
    case 0:
        MR::startSystemME("ME_ASTRO_DOME_CALCEL1");
        break;
    case 1:
        MR::startSystemME("ME_ASTRO_DOME_CALCEL2");
        break;
    case 2:
        MR::startSystemME("ME_ASTRO_DOME_CALCEL3");
        break;
    case 3:
        MR::startSystemME("ME_ASTRO_DOME_CALCEL4");
        break;
    case 4:
        MR::startSystemME("ME_ASTRO_DOME_CALCEL5");
        break;
    }
}

void SphereSelector::exeSelectWait() {
    if (MR::isFirstStep(this)) {
        mPointingTarget = nullptr;
        mNearestTarget = nullptr;
        mClickStep = -1;
        MR::startStarPointerModeSphereSelectorOnReaction(this);
    }

    if (::isWaitingForClick(this)) {
        if (mPointingTarget != nullptr && isDecideTrigger()) {
            sendSelectedMsgAndSetTarget(mPointingTarget);
        } else if (isButtonAPressed()) {
            if (mHandle->isPointing()) {
                sendSelectedMsgAndSetTarget(mHandle);
            }
        }
    } else {
        mClickStep++;

        if (isMoveClickedPos() || mClickStep > NebulaFrameClock::stepsOfMilliseconds(500)) {
            sendSelectedMsgAndSetTarget(mHandle);
            mClickStep = -1;
        } else if (!isButtonAPressed()) {
            sendSelectedMsgAndSetTarget(mPointingTarget);
            mClickStep = -1;
        }
    }

    if (mClickStep < 0) {
        if (mNearestTarget != nullptr && mNearestTarget != mPointingTarget && mNearestTarget != mHandle) {
            MR::tryRumblePadWeak(this, 0);
        }

        mPointingTarget = mNearestTarget;
        mNearestTarget = nullptr;
    }
}

void SphereSelector::exeSelectCancel() {
    if (MR::isFirstStep(this)) {
        if (!mIsSePlayed) {
            MR::startSystemSE("SE_SY_GALAXY_DECIDE_CANCEL");
            playCanceledME();
        }

        mIsSePlayed = false;
    }
}

void SphereSelector::exeConfirmStart() {
    if (MR::isFirstStep(this)) {
        sendMsgToAllActor(ACTMES_SPHERE_SELECTOR_CONFIRM_START);
        if (!mIsSePlayed) {
            MR::startSystemSE("SE_SY_GALAXY_SELECTED");
            playSelectedME();
        }

        mIsSePlayed = false;
        MR::endStarPointerMode(this);
    }

    MR::setNerveAtStep(this, GET_NERVE(SphereSelector, SphereSelectorNrvConfirmWait), SphereSelectorFunction::getConfirmStartCancelFrame());
}

void SphereSelector::exeConfirmCancel() {
    if (MR::isFirstStep(this)) {
        sendMsgToAllActor(ACTMES_SPHERE_SELECTOR_CONFIRM_CANCEL);
        mSelectedTarget = nullptr;
        mPointingTarget = nullptr;
        if (!mIsSePlayed) {
            playCanceledME();
        }

        mIsSePlayed = false;
    }

    MR::setNerveAtStep(this, GET_NERVE(SphereSelector, SphereSelectorNrvSelectWait), SphereSelectorFunction::getConfirmStartCancelFrame());
}

namespace {
    void getHandleMtx(Pos3f* pPos) {
        pPos->identity();

        if (MR::isExistSceneObj(SceneObj_SphereSelector)) {
            SphereSelectorHandle* pSelectorHandle = ::getSphereSelector()->mHandle;
            if (pSelectorHandle != nullptr && !MR::isDead(pSelectorHandle)) {
                pPos->set(pSelectorHandle->getBaseMtx());
            }
        }
    }
};  // namespace

void SphereSelector::exeSelectStart() {
    if (MR::isFirstStep(this)) {
        sendMsgToAllActor(ACTMES_SPHERE_SELECTOR_SELECT_START);
    }

    MR::setNerveAtStep(this, GET_NERVE(SphereSelector, SphereSelectorNrvSelectWait), NebulaFrameClock::stepsOfSeconds(1));
}

void SphereSelector::exeConfirmWait() {
}

void SphereSelector::exeConfirm() {
    if (MR::isFirstStep(this)) {
        sendMsgToAllActor(ACTMES_SPHERE_SELECTOR_CONFIRMED);
        MR::stopStageBGM(90);
    }
}

void SphereSelectorFunction::registerTarget(LiveActor* pActor) {
    MR::createSceneObj(SceneObj_SphereSelector);
    ::getSphereSelector()->mSphereGroup->registerActor(pActor);
}

bool SphereSelectorFunction::isPadButton() {
    return MR::isHeldJump(PLAYER_1) && !MR::isDemoActive();
}

s32 SphereSelectorFunction::getSelectStartFrame() {
    return 60;
}

s32 SphereSelectorFunction::getConfirmStartCancelFrame() {
    return 45;
}

void SphereSelectorFunction::selectStart() {
    ::getSphereSelector()->appear();
}

void SphereSelectorFunction::selectCancel(bool isSePlayed) {
    if (isSePlayed) {
        ::getSphereSelector()->mIsSePlayed = true;
    }

    ::getSphereSelector()->setNerve(GET_NERVE(SphereSelector, SphereSelectorNrvSelectCancel));
}

void SphereSelectorFunction::selectEnd() {
    ::getSphereSelector()->kill();
}

void SphereSelectorFunction::confirmStart() {
    ::getSphereSelector()->setNerve(GET_NERVE(SphereSelector, SphereSelectorNrvConfirmStart));
}

void SphereSelectorFunction::confirmCancel(bool isSePlayed) {
    if (isSePlayed) {
        ::getSphereSelector()->mIsSePlayed = true;
    }

    ::getSphereSelector()->setNerve(GET_NERVE(SphereSelector, SphereSelectorNrvConfirmCancel));
}

void SphereSelectorFunction::confirmed() {
    ::getSphereSelector()->setNerve(GET_NERVE(SphereSelector, SphereSelectorNrvConfirmed));
}

bool SphereSelectorFunction::isSelectWait() {
    return ::getSphereSelector()->isNerve(GET_NERVE(SphereSelector, SphereSelectorNrvSelectWait));
}

bool SphereSelectorFunction::isConfirmWait() {
    return ::getSphereSelector()->isNerve(GET_NERVE(SphereSelector, SphereSelectorNrvConfirmWait));
}

bool SphereSelectorFunction::isMsgSelectStart(u32 msg) {
    return ACTMES_SPHERE_SELECTOR_SELECT_START == msg;
}

bool SphereSelectorFunction::isMsgSelectEnd(u32 msg) {
    return ACTMES_SPHERE_SELECTOR_SELECT_END == msg;
}

bool SphereSelectorFunction::isMsgConfirmStart(u32 msg) {
    return ACTMES_SPHERE_SELECTOR_CONFIRM_START == msg;
}

bool SphereSelectorFunction::isMsgConfirmCancel(u32 msg) {
    return ACTMES_SPHERE_SELECTOR_CONFIRM_CANCEL == msg;
}

bool SphereSelectorFunction::isMsgConfirmed(u32 msg) {
    return ACTMES_SPHERE_SELECTOR_CONFIRMED == msg;
}

bool SphereSelectorFunction::isMsgTargetSelected(u32 msg) {
    return ACTMES_SPHERE_SELECTOR_TARGET_SELECTED == msg;
}

bool SphereSelectorFunction::trySyncAppearMsgSelectStart(LiveActor* pActor, u32 msg) {
    if (isMsgSelectStart(msg)) {
        pActor->appear();
        return true;
    }

    if (isMsgSelectEnd(msg)) {
        pActor->kill();
        return true;
    }

    return false;
}

bool SphereSelectorFunction::trySyncKillMsgSelectStart(LiveActor* pActor, u32 msg) {
    if (isMsgSelectStart(msg)) {
        pActor->kill();
        return true;
    }

    if (isMsgSelectEnd(msg)) {
        pActor->appear();
        return true;
    }

    return false;
}

void SphereSelectorFunction::validatePointing() {
    ::getSphereSelector()->validatePointing();
}

void SphereSelectorFunction::invalidatePointing() {
    ::getSphereSelector()->invalidatePointing();
}

bool SphereSelectorFunction::isValidPointing() {
    return !::getSphereSelector()->mIsPointingInvalid;
}

void SphereSelectorFunction::setHandle(SphereSelectorHandle* pHandle) {
    ::getSphereSelector()->mHandle = pHandle;
}

void SphereSelectorFunction::calcHandledTrans(const Vec3f& rSrc, Vec3f* pDst) {
    Pos3f mtx;
    ::getHandleMtx(&mtx);
    mtx.mult(rSrc, *pDst);
}

void SphereSelectorFunction::calcHandledRotateMtx(const Vec3f& rSrc, Pos3f* pDst) {
    Pos3f handleMtx;
    ::getHandleMtx(&handleMtx);
    Mtx34f rotMtx;
    MR::makeMtxRotate(rotMtx, rSrc);
    pDst->concat(handleMtx, rotMtx);
}

Vec3f& SphereSelectorFunction::getHandleTrans() {
    return ::getSphereSelector()->mHandle->mPosition;
}

f32 SphereSelectorFunction::getHandleRotateSpeed() {
    return ::getSphereSelector()->mHandle->mRotateSpeed;
}

bool SphereSelectorFunction::isHandleHolding() {
    return ::getSphereSelector()->mHandle->isHolding();
}

void SphereSelectorFunction::registerPointingTarget(LiveActor* pActor, HandlePointingPriority priority) {
    ::getSphereSelector()->registerPointingTarget(pActor, priority);
}

bool SphereSelectorFunction::tryRegisterPointingTarget(LiveActor* pActor, HandlePointingPriority priority) {
    if (!isHandleHolding() && MR::isStarPointerPointing1PWithoutCheckZ(pActor, nullptr, true, false)) {
        ::getSphereSelector()->registerPointingTarget(pActor, priority);
        return true;
    }

    return false;
}

bool SphereSelectorFunction::isPointingTarget() {
    return ::getSphereSelector()->mPointingTarget != nullptr;
}

bool SphereSelectorFunction::isPointingTarget(const LiveActor* pActor) {
    return ::getSphereSelector()->mPointingTarget == pActor;
}

LiveActor* SphereSelectorFunction::getPointingTarget() {
    return ::getSphereSelector()->mPointingTarget;
}

LiveActor* SphereSelectorFunction::getSelectedTarget() {
    return ::getSphereSelector()->mSelectedTarget;
}

Vec3f& SphereSelectorFunction::getSelectedActorTrans() {
    return getSelectedTarget()->mPosition;
}

void SphereSelectorFunction::calcOffsetPos(Vec3f* pDst, const Vec3f& rVec2, const Vec3f& rVec3, const Vec3f& rVec4, const Vec3f& rVec5) {
    Vec3f vec(rVec4);
    Pos3f mtx;

    if (MR::normalizeOrZero(&vec) || MR::isSameDirection(rVec5, vec)) {
        vec.set< f32 >(0.0f, 0.0f, 1.0f);
    }

    MR::makeMtxUpFrontPos(&mtx, rVec5, vec, rVec2);
    mtx.mult(rVec3, *pDst);
}
