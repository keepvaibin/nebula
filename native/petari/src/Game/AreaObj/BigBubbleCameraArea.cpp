#include "Game/AreaObj/BigBubbleCameraArea.hpp"
#include "Game/MapObj/BigBubbleHolder.hpp"
#include "Game/System/LumaCore.hpp"

BigBubbleCameraArea::BigBubbleCameraArea(int formType, const char* pName) : CubeCameraArea(formType, pName) {
    lumacore::setClass(this, lumacore::kClassId< BigBubbleCameraArea >);
}

bool BigBubbleCameraArea::isInVolume(const Vec3f& rPos) const {
    return MR::isPlayerBindedBigBubble() && CubeCameraArea::isInVolume(rPos);
}

BigBubbleSwitchArea::BigBubbleSwitchArea(int formType, const char* pName) : SwitchArea(formType, pName) {
    lumacore::setClass(this, lumacore::kClassId< BigBubbleSwitchArea >);
}

bool BigBubbleSwitchArea::isInVolume(const Vec3f& rPos) const {
    return MR::isPlayerBindedBigBubble() && AreaObj::isInVolume(rPos);
}
