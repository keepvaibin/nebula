#include "Game/MapObj/BlueChip.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/System/LumaCore.hpp"

BlueChip::BlueChip(const char* pName) : ChipBase(pName, ChipBase::Type_Blue, "BlueChip") {
    lumacore::setClass(this, lumacore::kClassId< BlueChip >);
}
