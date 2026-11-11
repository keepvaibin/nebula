#include "Game/AreaObj/HazeCube.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEBULA_TUNABLE(f32, sDefaultDistance, 1000.0f);
};  // namespace

HazeCube::HazeCube(int formType, const char* pName) : AreaObj(formType, pName), mDistance(::sDefaultDistance) {
    lumacore::setClass(this, lumacore::kClassId< HazeCube >);
}

void HazeCube::init(const TableIter& rIter) {
    AreaObj::init(rIter);

    if (mObjArg0 >= 0) {
        mDistance = mObjArg0;
    }

    MR::createSceneObj(SceneObj_HeatHazeDirector);
}
