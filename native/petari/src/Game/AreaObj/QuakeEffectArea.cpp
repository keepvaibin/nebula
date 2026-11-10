#include "Game/AreaObj/QuakeEffectArea.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/LumaCore.hpp"

QuakeEffectArea::QuakeEffectArea(int formType, const char* pName) : AreaObj(formType, pName) {
    lumacore::setClass(this, lumacore::kClassId< QuakeEffectArea >);
    if (!MR::isExistSceneObj(SceneObj_QuakeEffectGenerator)) {
        MR::createSceneObj(SceneObj_QuakeEffectGenerator);
    }
}
