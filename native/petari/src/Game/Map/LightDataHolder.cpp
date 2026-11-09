#include "Game/Map/LightDataHolder.hpp"
#include "Game/Map/LightFunction.hpp"
#include "Game/Util.hpp"

namespace {
    static const char* sDefaultAreaLightName = "デフォルト";

    static LightInfoCoin sDefaultLightSetCoin = {{{0xFF, 0xFF, 0, 0}, {0.0f, 0.0f, 0.0f}, true}, {0xFF, 0xFF, 0xFF, 0}, 65.0f};
};  // namespace

LightDataHolder::LightDataHolder() {
    mLightCount = 0;
    mLights = 0;
}

void LightDataHolder::initLightData() {
    Table* data = NULL;
    mLightCount = LightFunction::createLightDataParser(&data);
    // At least one light is held (zeroed), so a name that is not found has a first light to take.
    mLights = new AreaLightInfo[mLightCount > 0 ? mLightCount : 1]();

    mIndex.reset(static_cast< u32 >(mLightCount));
    for (s32 i = 0; i < mLightCount; i++) {
        LightFunction::getAreaLightLightData(data, i, &mLights[i]);
        if (mLights[i].mAreaLightName != nullptr) {
            mIndex.add(mLights[i].mAreaLightName, static_cast< u32 >(i));
        }
    }

    mCoinLight = ::sDefaultLightSetCoin;
}

// A name not held gets the first light.
AreaLightInfo* LightDataHolder::findAreaLight(const char* pName) const {
    const u32 index = mIndex.find(pName);
    return index == MR::NameIndex::cNoValue ? mLights : getLightInfo(static_cast< s32 >(index));
}

const char* LightDataHolder::getDefaultAreaLightName() const {
    return ::sDefaultAreaLightName;
}

s32 LightDataHolder::getDefaultStepInterpolate() const {
    return 0x1E;
}
