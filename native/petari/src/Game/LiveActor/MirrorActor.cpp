#include "Game/LiveActor/MirrorActor.hpp"
#include "Game/LiveActor/MirrorCamera.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/NameObj/NameObjExecuteHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 0.5f);

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
    center.lerp(box.f, box.i, cNum_init_f1);
    mRadius = cNum_init_f2 * (box.f - box.i).length();
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
