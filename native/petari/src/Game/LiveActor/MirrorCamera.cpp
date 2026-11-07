#include "Game/LiveActor/MirrorCamera.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Render/ModelData.hpp"
#include <nebula_geometry.hpp>

MirrorCamera::MirrorCamera(const char* pName) : NameObj(pName), _C(0.0f, 0.0f, 0.0f), _18(0.0f, 1.0f, 0.0f), _24() {
    mViewMtx.identity();
    mModelTexMtx.identity();
}

void MirrorCamera::init(const TableIter& rIter) {
    MR::connectToScene(this, MR::MovementType_MirrorCamera, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_None);
}

void MirrorCamera::setMirrorMapInfo(const Vec3f& rNormal, const Vec3f& rPosition) {
    _18.set(rNormal);
    _C.set(rPosition);
    _24 = -NebulaVecDotProduct(_18, _C);
}

void MirrorCamera::setMirrorMapInfo(ModelData* pModelData) {
    Vec3f position;
    Vec3f normal;
    const f32* pPositions = pModelData->getFirstPosition();
    position.set< f32 >(pPositions[0], pPositions[1], pPositions[2]);
    const f32* pNormals = pModelData->getFirstNormal();
    normal.set< f32 >(pNormals[0], pNormals[1], pNormals[2]);

    setMirrorMapInfo(normal, position);
}

void MirrorCamera::updateViewMtx() {
    Pos3f cameraInv;
    cameraInv.set(MR::getCameraInvViewMtx());
    Vec3f y;
    Vec3f z;
    Vec3f trans;
    cameraInv.getYDir(y);
    cameraInv.getZDir(z);
    cameraInv.getTrans(trans);
    y -= _18 * (_18.dot(y) * 2.0f);
    z -= _18 * (_18.dot(z) * 2.0f);
    Vec3f x = y.cross(z);
    trans -= _18 * ((_18.dot(trans) + _24) * 2.0f);
    mViewMtx.setTR(x, y, z, trans);
    mViewMtx.invert(mViewMtx);
}

void MirrorCamera::updateModelTexMtx() {
    Proj3f mtx = MR::getCameraProjectionMtx();
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

f32 MirrorCamera::getDistance(const Vec3f& rPosition) const {
    Plane3f plane;
    plane.mNormal.set(_18);
    plane.mDot = plane.mNormal.dot(_C);
    return plane.mNormal.dot(rPosition) - plane.mDot;
}

namespace MR {
    MirrorCamera* getMirrorCamera() {
        return MR::getSceneObj< MirrorCamera >(SceneObj_MirrorCamera);
    }

    f32 getDistanceToMirror(const Vec3f& rVec) {
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
