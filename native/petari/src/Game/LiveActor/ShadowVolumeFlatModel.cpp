#include "Game/LiveActor/ShadowVolumeFlatModel.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/System/LumaCore.hpp"

ShadowVolumeFlatModel::ShadowVolumeFlatModel(const char* pModelName)
    : ShadowVolumeModel("板状シャドウボリューム描画"), mBaseMtx(), mRootJoint(-1), mDropJoint(-1) {
    lumacore::setClass(this, lumacore::kClassId< ShadowVolumeFlatModel >);
    initVolumeModel(pModelName);
    initJointDrawMtxResister();
}

ShadowVolumeFlatModel::ShadowVolumeFlatModel()
    : ShadowVolumeModel("板状シャドウボリューム描画"), mBaseMtx(), mRootJoint(-1), mDropJoint(-1) {
    lumacore::setClass(this, lumacore::kClassId< ShadowVolumeFlatModel >);
}

void ShadowVolumeFlatModel::setBaseMatrixPtr(MtxPtr pMtx) {
    mBaseMtx = pMtx;
}

void ShadowVolumeFlatModel::initModel(const char* pModelName) {
    initVolumeModel(pModelName);
    initJointDrawMtxResister();
}

void ShadowVolumeFlatModel::initJointDrawMtxResister() {
    Joint* root = mModelData->getRootNode();
    mRootJoint = root->getJntNo();
    mDropJoint = root->getChild()->getJntNo();
}

void ShadowVolumeFlatModel::draw() const {
    if (getController()->isDraw()) {
        ShadowVolumeDrawer::draw();
    }
}

void ShadowVolumeFlatModel::loadModelDrawMtx(TypedDraw& rDraw) const {
    ShadowController* controller = getController();
    Pos3f rootMtx;
    Pos3f dropMtx;
    Vec3f displacement;
    controller->getDropDir(&displacement);
    displacement.scale(calcBaseDropLength());
    calcRootJoint(&rootMtx, displacement);
    if (controller->isFollowHostScale()) {
        MR::preScaleMtx(rootMtx, controller->getHost()->mScale);
    }

    calcDropJoint(&dropMtx, displacement);
    if (controller->isFollowHostScale()) {
        MR::preScaleMtx(dropMtx, controller->getHost()->mScale);
    }

    f32 (*pJoints)[4] = static_cast< f32 (*)[4] >(Frame::arena(mModelData->getJointCount() * sizeof(Mtx), 16));
    memcpy(pJoints[mRootJoint * 3], rootMtx.mMtx, sizeof(Mtx));
    memcpy(pJoints[mDropJoint * 3], dropMtx.mMtx, sizeof(Mtx));
    rDraw.setModelJoints(mModelData, pJoints);
}

void ShadowVolumeFlatModel::calcRootJoint(Pos3f* pMtx, const Vec3f& rDisplacement) const {
    pMtx->set(mBaseMtx);
    Vec3f up(mBaseMtx[0][1], mBaseMtx[1][1], mBaseMtx[2][1]);
    if (up.dot(rDisplacement) >= 0.0f) {
        MR::addTransMtx(*pMtx, rDisplacement);
    }
}

void ShadowVolumeFlatModel::calcDropJoint(Pos3f* pMtx, const Vec3f& rDisplacement) const {
    pMtx->set(mBaseMtx);
    Vec3f up(mBaseMtx[0][1], mBaseMtx[1][1], mBaseMtx[2][1]);
    if (up.dot(rDisplacement) < 0.0f) {
        MR::addTransMtx(*pMtx, rDisplacement);
    }
}

ShadowVolumeFlatModel::~ShadowVolumeFlatModel() {
}
