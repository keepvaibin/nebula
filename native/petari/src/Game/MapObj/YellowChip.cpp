#include "Game/MapObj/YellowChip.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/System/LumaCore.hpp"

YellowChip::YellowChip(const char* pName) : ChipBase(pName, ChipBase::Type_Yellow, "YellowChip") {
    lumacore::setClass(this, lumacore::kClassId< YellowChip >);
}
