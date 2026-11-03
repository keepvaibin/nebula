#include "Game/LiveActor/MirrorCamera.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util.hpp"
#include "Game/Util/MtxUtil.hpp"
#include <JSystem/J3DGraphAnimator/J3DModelData.hpp>
#include <JSystem/JGeometry/TPartition3.hpp>

MirrorCamera::MirrorCamera(const char* pName) : NameObj(pName), _C(0.0f, 0.0f, 0.0f), _18(0.0f, 1.0f, 0.0f), _24() {
    mViewMtx.identity();
    mModelTexMtx.identity();
}

void MirrorCamera::init(const JMapInfoIter& rIter) {
    MR::connectToScene(this, MR::MovementType_MirrorCamera, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_None);
}

void MirrorCamera::setMirrorMapInfo(const TVec3f& rNormal, const TVec3f& rPosition) {
    _18.set(rNormal);
    _C.set(rPosition);
    _24 = -NebulaVecDotProduct(_18, _C);
}

void MirrorCamera::setMirrorMapInfo(J3DModelData* pModelData) {
    TVec3f position;
    TVec3f normal;
    const f32* pPositions = static_cast< const f32* >(pModelData->getVtxPosArray());
    position.set< f32 >(pPositions[0], pPositions[1], pPositions[2]);
    const f32* pNormals = static_cast< const f32* >(pModelData->getVtxNrmArray());
    normal.set< f32 >(pNormals[0], pNormals[1], pNormals[2]);

    setMirrorMapInfo(normal, position);
}

void MirrorCamera::updateViewMtx() {
    TPos3f cameraInv;
    cameraInv.set(MR::getCameraInvViewMtx());
    TVec3f y;
    TVec3f z;
    TVec3f trans;
    cameraInv.getYDir(y);
    cameraInv.getZDir(z);
    cameraInv.getTrans(trans);
    y -= _18 * (_18.dot(y) * 2.0f);
    z -= _18 * (_18.dot(z) * 2.0f);
    TVec3f x = y.cross(z);
    trans -= _18 * ((_18.dot(trans) + _24) * 2.0f);
    mViewMtx.setTR(x, y, z, trans);
    mViewMtx.invert(mViewMtx);
}

void MirrorCamera::updateModelTexMtx() {
    TProj3f mtx = MR::getCameraProjectionMtx();
    mtx[2][0] = 0.0f;
    mtx[2][1] = 0.0f;
    mtx[2][2] = -1.0f;
    mtx[2][3] = 0.0f;
    mtx[3][0] = 0.0f;
    mtx[3][1] = 0.0f;
    mtx[3][2] = 0.0f;
    mtx[3][3] = 1.0f;
    MR::multMtx(mModelTexMtx.mMtx, mViewMtx.mMtx, mtx);
}

f32 MirrorCamera::getDistance(const TVec3f& rPosition) const {
    JGeometry::TPartition3< f32 > plane;
    plane.mNormal.set(_18);
    plane.mDot = plane.mNormal.dot(_C);
    return plane.mNormal.dot(rPosition) - plane.mDot;
}

namespace MR {
    MirrorCamera* getMirrorCamera() {
        return MR::getSceneObj< MirrorCamera >(SceneObj_MirrorCamera);
    }

    f32 getDistanceToMirror(const TVec3f& rVec) {
        return getMirrorCamera()->getDistance(rVec);
    }
};  // namespace MR

MirrorCamera::~MirrorCamera() {
}

void MirrorCamera::movement() {
    if (MR::isPlayerInAreaObj("MirrorArea")) {
        updateViewMtx();
        updateModelTexMtx();
    }
}
