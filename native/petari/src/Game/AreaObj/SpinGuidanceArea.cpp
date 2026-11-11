#include "Game/AreaObj/SpinGuidanceArea.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/PlayerActionGuidance.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

SpinGuidanceArea::SpinGuidanceArea(int formType, const char* pName) : AreaObj(formType, pName) {
    lumacore::setClass(this, lumacore::kClassId< SpinGuidanceArea >);
}

void SpinGuidanceArea::init(const TableIter& rIter) {
    AreaObj::init(rIter);
    MR::createSceneObj(SceneObj_PlayerActionGuidance);

    const char* objName = nullptr;
    MR::getObjectName(&objName, rIter);

    if (MR::isEqualString(objName, "SpinGuidanceCube")) {
        MR::getPlayerActionGuidance()->createSpinLayout();
    } else if (MR::isEqualString(objName, "TamakoroMoveGuidanceCube") || MR::isEqualString(objName, "TamakoroJumpGuidanceCube")) {
        MR::getPlayerActionGuidance()->createTamakoroLayout();
    }
}