#include "Game/AreaObj/SunLightArea.hpp"
#include "Game/System/LumaCore.hpp"

SunLightArea::SunLightArea(int formType, const char* pName) : AreaObj(formType, pName) {
    lumacore::setClass(this, lumacore::kClassId< SunLightArea >);
}
