#include "Game/LiveActor/MirrorCamera.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Render/ModelData.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_updateViewMtx_f1, "updateViewMtx.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateViewMtx_f2, "updateViewMtx.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateViewMtx_f3, "updateViewMtx.f3", 2.0f);

MirrorCamera::MirrorCamera(const char* pName) : NameObj(pName), mMirrorPos(0.0f, 0.0f, 0.0f), mMirrorNormal(0.0f, 1.0f, 0.0f), mPlaneD() {
    lumacore::setClass(this, lumacore::kClassId< MirrorCamera >);
    mViewMtx.identity();
    mModelTexMtx.identity();
}

void MirrorCamera::init(const TableIter& /*rIter*/) {
    MR::connectToScene(this, MR::MovementType_MirrorCamera, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_None);
}

void MirrorCamera::setMirrorMapInfo(const Vec3f& rNormal, const Vec3f& rPosition) {
    mMirrorNormal.set(rNormal);
    mMirrorPos.set(rPosition);
    mPlaneD = -NebulaVecDotProduct(mMirrorNormal, mMirrorPos);
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
    y -= mMirrorNormal * (mMirrorNormal.dot(y) * cNum_updateViewMtx_f1);
    z -= mMirrorNormal * (mMirrorNormal.dot(z) * cNum_updateViewMtx_f2);
    Vec3f x = y.cross(z);
    trans -= mMirrorNormal * ((mMirrorNormal.dot(trans) + mPlaneD) * cNum_updateViewMtx_f3);
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
    return mMirrorNormal.dot(rPosition) - mMirrorNormal.dot(mMirrorPos);
}

namespace MR {
    MirrorCamera* getMirrorCamera() {
        return MR::getSceneObj< MirrorCamera >(SceneObj_MirrorCamera);
    }

    f32 getDistanceToMirror(const Vec3f& rVec) {
        return getMirrorCamera()->getDistance(rVec);
    }
};  // namespace MR

void MirrorCamera::movement() {
    if (MR::isPlayerInAreaObj("MirrorArea")) {
        updateViewMtx();
        updateModelTexMtx();
    }
}
