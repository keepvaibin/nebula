#include "Game/System/GameDataConst.hpp"
#include "Game/System/GalaxyInfoTable.hpp"
#include "Game/System/GameEventFlag.hpp"
#include "Game/System/GameEventFlagTable.hpp"
#include "Game/Util.hpp"
#include "Game/Util/StringUtil.hpp"

s32 GameDataConst::getPowerStarNumToOpenGalaxy(const char* pGalaxyName) {
    Table info;
    info.attach(NebulaDataTables::getGalaxyInfoTable());

    TableIter element = info.findElement< const char* >("name", pGalaxyName, 0);

    u32 powerStarNum = 0;
    element.getValue< u32 >("PowerStarNum", &powerStarNum);

    return powerStarNum;
}

bool GameDataConst::isPowerStarGreen(const char* pGalaxyName, s32 starId) {
    return isPowerStarSpecial(pGalaxyName, starId, "SpecialStarGreen");
}

bool GameDataConst::isPowerStarRed(const char* pGalaxyName, s32 starId) {
    return isPowerStarSpecial(pGalaxyName, starId, "SpecialStarRed");
}

bool GameDataConst::isPowerStarLuigiHas(const char* pGalaxyName, s32 starId) {
    return isPowerStarSpecial(pGalaxyName, starId, "SpecialStarFindingLuigi");
}

bool GameDataConst::isGrandStar(const char* pGalaxyName, s32 starId) {
    return isPowerStarSpecial(pGalaxyName, starId, "SpecialStarGrand");
}

bool GameDataConst::isGalaxyLuigiArrested(const char* pGalaxyName, s32 starId) {
    GameEventFlagAccessor accessor = GameEventFlagTable::makeAccessor("SpecialStarLuigiRescued");

    return MR::isEqualString(accessor.getGalaxyName(), pGalaxyName) && (starId == -1 || starId == accessor.getStarId());
}

// The flag named Appear<galaxy>, when it is of the open-star kind.
bool GameDataConst::isQuestionGalaxy(const char* pGalaxyName) {
    MR::FormattedName flagName("Appear%s", pGalaxyName);
    const GameEventFlag* pFlag = GameEventFlagTable::findFlag(flagName.c_str());

    return pFlag != nullptr && GameEventFlagAccessor(pFlag).isTypeGalaxyOpenStar();
}

bool GameDataConst::isGalaxyAppearGreenDriver(const char* pGalaxyName) {
    MR::FormattedName flagName("Appear%s", pGalaxyName);

    return !GameEventFlagTable::isExist(flagName.c_str()) ? false : GameEventFlagTable::isDependedAnother(flagName.c_str(), "SpecialStarGreenAll");
}

u32 GameDataConst::getIncludedGrandGalaxyId(const char* pGalaxyName) {
    Table info;
    info.attach(NebulaDataTables::getGalaxyInfoTable());

    TableIter element = info.findElement< const char* >("name", pGalaxyName, 0);

    u32 grandGalaxyNo = 0;
    element.getValue< u32 >("GrandGalaxyNo", &grandGalaxyNo);

    return grandGalaxyNo;
}

bool GameDataConst::isPowerStarSpecial(const char* pGalaxyName, s32 starId, const char* pSpecialStarName) {
    return GameEventFlagTable::findSpecialPowerStar(pGalaxyName, starId, pSpecialStarName) != nullptr;
}
