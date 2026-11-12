#include "Game/LiveActor/MirrorActor.hpp"
#include "Game/LiveActor/MirrorCamera.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/NameObj/NameObjExecuteHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

MirrorActor::MirrorActor(LiveActor* pActor, const char* pName, const char* pArcName) : LiveActor(pName), mHost(pActor), mIsDisconnected() {
    lumacore::setClass(this, lumacore::kClassId< MirrorActor >);
    initModelManagerWithAnm(pArcName, nullptr, false);
}

void MirrorActor::init(const TableIter& /*rIter*/) {
    MR::connectToSceneMirrorMapObj(this);
    MR::copyJointAnimation(this, mHost);
    MR::syncMaterialAnimation(this, mHost);
    MR::invalidateClipping(this);

    Box3f box;
    MR::calcModelBoundingBox(&box, mHost);

    Vec3f center;
    center.lerp(box.f, box.i, 0.5f);
    mRadius = 0.5f * (box.f - box.i).length();
    mCenter = center;

    makeActorAppeared();
}

void MirrorActor::movement() {
    if (MR::isDead(this) || MR::isDead(mHost) || MR::isClipped(mHost) || MR::isHiddenModel(mHost) || isHostInTheOtherSideOfMirror()) {
        if (!mIsDisconnected) {
            mIsDisconnected = true;

            if (!MR::isHiddenModel(this)) {
                MR::disconnectToDrawTemporarily(this);
            }
        }
    } else {
        if (mIsDisconnected) {
            mIsDisconnected = false;

            if (!MR::isHiddenModel(this)) {
                MR::connectToDrawTemporarily(this);
            }
        }

        LiveActor::movement();
    }
}

void MirrorActor::calcAnim() {
    MR::copyJointAnimation(this, mHost);
    MR::updateMaterial(this);
}

void MirrorActor::calcViewAndEntry() {
    mModelManager->calcView();
}

bool MirrorActor::isHostInTheOtherSideOfMirror() const {
    return MR::isExistMirrorCamera() && MR::getDistanceToMirror(getHostCenterPos()) < 0.0f;
}

Vec3f MirrorActor::getHostCenterPos() const {
    return mCenter + mHost->mPosition;
}
