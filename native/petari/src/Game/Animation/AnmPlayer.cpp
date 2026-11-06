#include "Game/Animation/AnmPlayer.hpp"
#include "Game/System/ResourceInfo.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Render/ModelData.hpp"
#include <nebula_geometry.hpp>

AnmPlayerBase::AnmPlayerBase(const ResTable* pResTable) : mResTable(pResTable), mAnmRes(), mFrameCtrl() {
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
    Clip* pAnmRes = reinterpret_cast< Clip* >(mResTable->getRes(pResName));

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
    if (mAnmRes != nullptr) {
        if (MR::isEqualStringCase(pAnimName, mResTable->getResName(mAnmRes))) {
            return true;
        }
    }

    return false;
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
