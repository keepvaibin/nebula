#include "Game/MapObj/CollapsePlane.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 200.0f);

namespace {
    NEBULA_TUNABLE(s32, sDefaultCollapseTime, 140);
    NEBULA_TUNABLE(f32, sScaleMin, 0.7f);
};  // namespace

namespace NrvCollapsePlane {
    NEW_NERVE(CollapsePlaneNrvWait, CollapsePlane, Wait);
    NEW_NERVE(CollapsePlaneNrvCollapse, CollapsePlane, Collapse);
    NEW_NERVE(CollapsePlaneNrvPointerStop, CollapsePlane, PointerStop);
    NEW_NERVE(CollapsePlaneNrvEnd, CollapsePlane, End);
};  // namespace NrvCollapsePlane

CollapsePlane::CollapsePlane(const char* pName)
    : MapObjActor(pName), mScaleController(), mStateBindStartPointer(), mJointController(), mCollapseStep(-1), mCollapseTime(::sDefaultCollapseTime) {
}

void CollapsePlane::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    info.setupDefaultPos();
    info.setupConnectToScene();
    info.setupHitSensor();
    info.setupSound(4);
    info.setupProjmapMtx(false);
    info.setupNerve(GET_NERVE(CollapsePlane, CollapsePlaneNrvWait));
    initialize(rIter, info);
    initEffectKeeper(1, nullptr);
    MR::initStarPointerTarget(this, mScale.x * cNum_init_f1, Vec3f(0.0f, 0.0f, 0.0f));

    mScaleController = new AnimScaleController(nullptr);
    mScaleController->setParamTight();

    mStateBindStartPointer = new WalkerStateBindStarPointer(this, mScaleController);

    mJointController = MR::createJointDelegatorWithNullChildFunc(this, &CollapsePlane::calcJointPlane, "Plane");

    MR::initCollisionPartsAutoEqualScale(this, "Move", getSensor(nullptr), MR::getJointMtx(this, "Plane"));
    MR::validateCollisionParts(this);
    MR::getPlacementArg0NoInit(rIter, &mCollapseTime);
}

void CollapsePlane::exeWait() {
    if (MR::isOnPlayer(this)) {
        mCollapseStep = 0;
        setNerve(GET_NERVE(CollapsePlane, CollapsePlaneNrvCollapse));
    }
}

void CollapsePlane::exeCollapse() {
    if (mCollapseStep == 1) {
        MR::startSound(this, "SE_OJ_COLLAPSE_PLANE_SHRINK");
    }

    if (mCollapseStep >= mCollapseTime) {
        MR::hideMaterial(this, "PlaneMat_v");
        MR::invalidateCollisionParts(this);
        MR::emitEffect(this, "Vanish");
        MR::startSound(this, "SE_OJ_COLLAPSE_PLANE_VANISH");
        mCollapseStep = -1;
        setNerve(GET_NERVE(CollapsePlane, CollapsePlaneNrvEnd));
    } else {
        mCollapseStep++;
    }
}

void CollapsePlane::exePointerStop() {
    if (mCollapseStep == -1 && MR::isOnPlayer(this)) {
        mCollapseStep = 0;
    }

    if (MR::updateActorState(this, mStateBindStartPointer)) {
        if (mCollapseStep != -1) {
            setNerve(GET_NERVE(CollapsePlane, CollapsePlaneNrvCollapse));
        } else {
            setNerve(GET_NERVE(CollapsePlane, CollapsePlaneNrvWait));
        }
    }
}

void CollapsePlane::exeEnd() {
}

void CollapsePlane::calcAndSetBaseMtx() {
    MapObjActor::calcAndSetBaseMtx();

    if (mCollapseStep != -1) {
        mJointController->registerCallBack();
    }

    if (MR::isInitializeStateEnd()) {
        MR::setBaseScale(this, mScaleController->mScale * mScale);
    }
}

void CollapsePlane::control() {
    mScaleController->updateNerve();
    tryPointerStop();
}

bool CollapsePlane::calcJointPlane(Pos3f* pMtx, const JointControllerInfo&) {
    // A collapse time of 0 or less (placement arg 0) is collapsed at once.
    f32 xzScale = mCollapseTime > 0 ? 1.0f - (::sScaleMin * mCollapseStep) / mCollapseTime : 1.0f - ::sScaleMin;

    Pos3f mtx;
    mtx.identity();
    MR::preScaleMtx(mtx, xzScale, 1.0f, xzScale);
    pMtx->concat(mtx);
    pMtx->setTrans(mPosition);

    return true;
}

bool CollapsePlane::tryPointerStop() {
    if (isNerve(GET_NERVE(CollapsePlane, CollapsePlaneNrvPointerStop))) {
        return false;
    }

    if (isNerve(GET_NERVE(CollapsePlane, CollapsePlaneNrvEnd))) {
        return false;
    }

    if (!mStateBindStartPointer->tryStartPointBind()) {
        return false;
    }

    setNerve(GET_NERVE(CollapsePlane, CollapsePlaneNrvPointerStop));

    return true;
}
