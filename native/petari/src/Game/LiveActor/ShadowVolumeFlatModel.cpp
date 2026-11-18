#include "Game/LiveActor/ShadowVolumeFlatModel.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    // The joint and every joint under it take the matrix.
    void writeSubtree(f32 (*pJoints)[4], const Joint* pJoint, const Mtx mtx) {
        memcpy(pJoints[pJoint->getJntNo() * 3], mtx, sizeof(Mtx));
        for (const Joint* pChild = pJoint->getChild(); pChild != nullptr; pChild = pChild->getYounger()) {
            writeSubtree(pJoints, pChild, mtx);
        }
    }
}  // namespace

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
    // Every shipped flat shadow has two joints, the root and the drop joint under it (P-178). A model with one is a
    // shadow that does not stretch; one with more has its other joints follow the root or the drop joint.
    mDropJoint = root->getChild() != nullptr ? root->getChild()->getJntNo() : mRootJoint;
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

    const s32 jointCount = mModelData->getJointCount();
    f32 (*pJoints)[4] = static_cast< f32 (*)[4] >(Frame::arena(jointCount * sizeof(Mtx), 16));
    // The frame arena is not cleared: a row left unwritten would draw from an old frame's bytes (P-178).
    for (s32 i = 0; i < jointCount; i++) {
        memcpy(pJoints[i * 3], rootMtx.mMtx, sizeof(Mtx));
    }
    if (mDropJoint != mRootJoint) {
        ::writeSubtree(pJoints, mModelData->getRootNode()->getChild(), dropMtx.mMtx);
    }
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
