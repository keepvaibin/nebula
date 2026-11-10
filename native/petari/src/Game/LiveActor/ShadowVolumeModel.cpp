#include "Game/LiveActor/ShadowVolumeModel.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

ShadowVolumeModel::ShadowVolumeModel(const char* pName) : ShadowVolumeDrawer(pName) {
    lumacore::setClass(this, lumacore::kClassId< ShadowVolumeModel >);
    mModelData = 0;
}

void ShadowVolumeModel::initVolumeModel(const char* pModelName) {
    mModelData = MR::getModelData(pModelName);
}

void ShadowVolumeModel::drawShape(TypedDraw& rDraw) const {
    rDraw.drawModelFile(mModelData);
}
