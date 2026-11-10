#include "Game/LiveActor/MirrorReflectionModel.hpp"
#include "Game/NameObj/NameObjExecuteHolder.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

MirrorReflectionModel::MirrorReflectionModel(const LiveActor* pHost, const char* pName, const char* pInternalName, MtxPtr pHostMtx)
    : LiveActor(pName), mHost(pHost), mHostMtx(pHostMtx), mIsHostGone(false) {
    lumacore::setClass(this, lumacore::kClassId< MirrorReflectionModel >);
    if (pHostMtx) {
        Pos3f pos;
        pos.set(pHostMtx);
        pos.getTrans(mPosition);
    } else {
        mPosition.set(pHost->mPosition);
    }

    mRotation.set(mHost->mRotation);
    mScale.set(mHost->mScale);
    initModelManagerWithAnm(pInternalName, nullptr, false);
    MR::connectToSceneMirrorMapObjDecoration(this);
}

void MirrorReflectionModel::init(const TableIter& /*rIter*/) {
    MR::invalidateClipping(this);
    MR::registerDemoSimpleCastAll(this);
    makeActorAppeared();
    mIsHostGone = false;
}

void MirrorReflectionModel::movement() {
    if (MR::isDead(mHost) || MR::isClipped(mHost) || MR::isHiddenModel(mHost)) {
        if (!mIsHostGone) {
            mIsHostGone = true;

            if (!MR::isHiddenModel(this)) {
                MR::disconnectToDrawTemporarily(this);
            }
        }
    } else {
        if (mIsHostGone) {
            mIsHostGone = false;
            if (!MR::isHiddenModel(this)) {
                MR::connectToDrawTemporarily(this);
            }
        }

        LiveActor::movement();
    }
}

void MirrorReflectionModel::calcAnim() {
    if (!mIsHostGone && !MR::isDead(this) && !MR::isNoCalcAnim(this) && !MR::isClipped(this)) {
        LiveActor::calcAnim();
    }
}

void MirrorReflectionModel::calcViewAndEntry() {
    if (!mIsHostGone) {
        LiveActor::calcViewAndEntry();
    }
}

void MirrorReflectionModel::calcAndSetBaseMtx() {
    if (!mHostMtx) {
        LiveActor::calcAndSetBaseMtx();
    } else {
        Pos3f pos;
        pos.set(mHostMtx);
        pos.getTrans(mPosition);
        MR::setBaseTRMtx(this, mHostMtx);
    }
}
