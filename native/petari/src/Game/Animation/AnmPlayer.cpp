#include "Game/Animation/AnmPlayer.hpp"
#include "Game/System/ResourceInfo.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Render/ModelData.hpp"
#include <nebula_geometry.hpp>

AnmPlayerBase::AnmPlayerBase(const ResTable* pResTable) : mAnmRes(), mResTable(pResTable), mFrameCtrl(), mAnmName(nullptr) {
}

void AnmPlayerBase::update() {
    if (mAnmRes != nullptr) {
        mFrameCtrl.update();
    }
}

void AnmPlayerBase::reflectFrame() {
    if (mAnmRes != nullptr) {
        mAnmRes->mFrame = mFrameCtrl.mFrame;
    }
}

void AnmPlayerBase::start(const char* pResName) {
    // One table lookup per start: the clip and its table name are kept for isPlaying.
    const ResFileInfo* pInfo = mResTable->findFileInfo(pResName);

    if (pInfo == nullptr) {
        return;
    }

    Clip* pAnmRes = reinterpret_cast< Clip* >(pInfo->mResource);
    mAnmName = pInfo->mName;

    if (pAnmRes != mAnmRes) {
        changeAnimation(pAnmRes);
        mAnmRes = pAnmRes;
    }

    mFrameCtrl.init(mAnmRes->mFrameMax);
    mFrameCtrl.setAttribute(mAnmRes->mAttribute);
    mFrameCtrl.setFrame(0.0f);
    mFrameCtrl.setRate(1.0f);
}

void AnmPlayerBase::stop() {
    stopAnimation();
    mFrameCtrl.setRate(0.0f);
}

bool AnmPlayerBase::isPlaying(const char* pAnimName) const {
    return mAnmRes != nullptr && mAnmName != nullptr && MR::isEqualStringCase(pAnimName, mAnmName);
}

MaterialAnmPlayerBase::MaterialAnmPlayerBase(const ResTable* pResTable, ModelData* pModelData) : AnmPlayerBase(pResTable), mModelData(pModelData) {
}

void MaterialAnmPlayerBase::applyFrame(Model* pModel) {
    if (mAnmRes != nullptr) {
        reflectFrame();
        apply(mAnmRes, pModel);
    }
}

void AnmPlayerBase::changeAnimation(Clip*) {
}

void AnmPlayerBase::stopAnimation() {
}
