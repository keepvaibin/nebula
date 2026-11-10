#include "Game/AreaObj/WarpCube.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/LiveActor/ActorCameraInfo.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/PlacementId.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_movement_f1, "movement.f1", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f1, "draw.f1", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f2, "draw.f2", 120.0f);

WarpCube* WarpCubeMgr::getPairCube(const AreaObj* pObj) {
    const PlacementId* pId = static_cast< const WarpCube* >(pObj)->mMapIdInfo;
    if (pId == nullptr) {
        return nullptr;
    }

    for (AreaObj* pArea : mArray) {
        WarpCube* pCube = static_cast< WarpCube* >(pArea);
        if (pCube == pObj || pCube->mMapIdInfo == nullptr) {
            continue;
        }

        if (*pCube->mMapIdInfo == *pId) {
            return pCube;
        }
    }

    return nullptr;
}

void WarpCubeMgr::setInvalidateTimer(AreaObj* pAreaObj, u16 timer) {
    WarpCube* pWarpCube = static_cast< WarpCube* >(pAreaObj);

    pWarpCube->mInvalidateTimer = timer;
    pWarpCube->mIsValid = false;
}

void WarpCubeMgr::startEventCamera(const AreaObj* pAreaObj) {
    const WarpCube* pWarpCube = static_cast< const WarpCube* >(pAreaObj);

    MR::startEventCameraNoTarget(pWarpCube->mCameraInfo, pWarpCube->mEventName);

    mWarpCube = pWarpCube;
}

void WarpCubeMgr::endEventCamera() {
    if (mWarpCube == nullptr) {
        return;
    }

    MR::endEventCamera(mWarpCube->mCameraInfo, mWarpCube->mEventName, true);
    mWarpCube = nullptr;
}

void WarpCube::movement() {
    if (mInvalidateTimer == 0) {
        return;
    }

    if (mInvalidateTimer == 1) {
        Vec3f pos;
        MR::calcCubePos(this, &pos);

        if (MR::calcDistanceToPlayer(pos) < cNum_movement_f1) {
            return;
        }
    }

    mInvalidateTimer--;

    if (mInvalidateTimer == 0) {
        mIsValid = true;
    }
}

void WarpCube::draw() const {
    SiteMaterial material;
    TDDraw::setupNative(&material, 0, 1, 0);
    material.setDepth(true, NEBULA_COMPARE_LEQUAL, false);

    Vec3f cubeRotate;
    Vec3f up(0.0f, 1.0f, 0.0f);
    MR::calcCubeRotate(this, &cubeRotate);

    Mtx matrix;
    MR::makeMtxTR(matrix, 0.0f, 0.0f, 0.0f, cubeRotate.x, cubeRotate.y, cubeRotate.z);
    NebulaMtxMultVecSR(matrix, &up, &up);

    Vec3f cubePos;
    MR::calcCubePos(this, &cubePos);

    u32 flags = 0x1FFF0080;
    if (!isValid()) {
        flags = 0x1FCF0010;
    }

    TypedDraw site = TDDraw::site(material, 0);
    TDDraw::drawSphere(site, cubePos + up * cNum_draw_f1, cNum_draw_f2, flags, 16);
}

void WarpCube::init(const TableIter& rIter) {
    AreaObj::init(rIter);

    s32 groupID = -1;
    MR::getPlacementGroupID(rIter, &groupID);

    if (groupID >= 0) {
        mMapIdInfo = new PlacementId(groupID, rIter);
    }

    mCameraInfo = new ActorCameraInfo(rIter);

    s32 cameraIndex;
    MR::getPlacementArg0WithInit(rIter, &cameraIndex);

    const MR::FormattedName eventName("ワープカメラ %d-%c", groupID, 'A' + cameraIndex);

    MR::declareEventCamera(mCameraInfo, eventName.c_str());

    const size_t eventNameSize = strlen(eventName.c_str()) + 1;
    mEventName = new char[eventNameSize];
    memcpy(mEventName, eventName.c_str(), eventNameSize);

    MR::connectToScene(this, MR::MovementType_AreaObj, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_NormalMap);
    mInvalidateTimer = 0;
}
