#include "Game/MapObj/ClipAreaMovable.hpp"
#include "Game/MapObj/MapPartsRailMover.hpp"
#include "Game/MapObj/MapPartsRotator.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/BaseMatrixFollowTargetHolder.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 1400.0f);

ClipAreaMovable::ClipAreaMovable(const char* pName) : ClipArea(pName), mMapPartsRailMover(), mMapPartsRotator() {
    lumacore::setClass(this, lumacore::kClassId< ClipAreaMovable >);
}

void ClipAreaMovable::init(const TableIter& rIter) {
    initBaseMatrix(rIter);
    MR::connectToScene(this, MR::MovementType_ClippedMapParts, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_ClipArea);
    MR::setGroupClipping(this, rIter, 0x40);
    MR::setClippingTypeSphere(this, MR::getMaxAbsElement(mScale) * cNum_init_f1);
    initMoveFunction(rIter);
    initHitSensor(1);
    MR::addHitSensorMapObj(this, "body", 0, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::useStageSwitchReadB(this, rIter);
    MR::addBaseMatrixFollowTarget(this, rIter, 0, 0);
    makeActorAppeared();
}

void ClipAreaMovable::makeActorAppeared() {
    LiveActor::makeActorAppeared();
    startMoveFunction();
}

void ClipAreaMovable::makeActorDead() {
    endMoveFunction();
    LiveActor::makeActorDead();
}

void ClipAreaMovable::control() {
    if (!MR::isValidSwitchB(this) || MR::isOnSwitchB(this)) {
        movementMoveFunction();
    }

    updateMatrix();
}

void ClipAreaMovable::initMoveFunction(const TableIter& rIter) {
    if (MR::isConnectedWithRail(rIter)) {
        initRailRider(rIter);

        mMapPartsRailMover = new MapPartsRailMover(this);
        mMapPartsRailMover->init(rIter);
    }

    mMapPartsRotator = new MapPartsRotator(this);
    mMapPartsRotator->init(rIter);
}

void ClipAreaMovable::startMoveFunction() {
    if (mMapPartsRotator != nullptr) {
        mMapPartsRotator->start();
    }

    if (mMapPartsRailMover != nullptr) {
        mMapPartsRailMover->start();
    }
}

void ClipAreaMovable::endMoveFunction() {
    if (mMapPartsRotator != nullptr) {
        mMapPartsRotator->end();
    }

    if (mMapPartsRailMover != nullptr) {
        mMapPartsRailMover->end();
    }
}

void ClipAreaMovable::movementMoveFunction() {
    if (mMapPartsRailMover != nullptr) {
        mMapPartsRailMover->movement();
    }

    if (mMapPartsRotator != nullptr) {
        mMapPartsRotator->movement();
    }
}

void ClipAreaMovable::updateMatrix() {
    Pos3f mtx;
    mtx.identity();

    if (mMapPartsRailMover != nullptr) {
        mPosition.set(mMapPartsRailMover->mStartPos);
    }

    if (mMapPartsRotator != nullptr) {
        mtx.concat(mMapPartsRotator->getRotateMtx(), mtx);
    }

    mtx.setTrans(mPosition);
    mBaseMtx = mtx;
}

NameObj* MR::createClipAreaSphere(const char* pModel) {
    ClipArea* pClipArea = new ClipAreaMovable(pModel);

    MR::setClipSphereArea(pClipArea);

    return pClipArea;
}

NameObj* MR::createClipAreaCenterBox(const char* pModel) {
    ClipArea* pClipArea = new ClipAreaMovable(pModel);

    MR::setClipCenterBoxArea(pClipArea);

    return pClipArea;
}

NameObj* MR::createClipAreaBottomBox(const char* pModel) {
    ClipArea* pClipArea = new ClipAreaMovable(pModel);

    MR::setClipBottomBoxArea(pClipArea);

    return pClipArea;
}

NameObj* MR::createClipAreaTopCone(const char* pModel) {
    ClipArea* pClipArea = new ClipAreaMovable(pModel);

    MR::setClipTopConeArea(pClipArea);

    return pClipArea;
}

NameObj* MR::createClipAreaBottomCone(const char* pModel) {
    ClipArea* pClipArea = new ClipAreaMovable(pModel);

    MR::setClipBottomConeArea(pClipArea);

    return pClipArea;
}
