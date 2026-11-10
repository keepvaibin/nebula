#include "Game/Map/CollisionDirector.hpp"
#include "Game/Map/CollisionCategorizedKeeper.hpp"
#include "Game/Map/CollisionCode.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

CollisionDirector::CollisionDirector() : NameObj("地形コリジョン"), mCategoryKeeper(), mCode() {
    lumacore::setClass(this, lumacore::kClassId< CollisionDirector >);
    mCode = new CollisionCode();

    for (s32 i = 0; i < CollisionCategory_Num; i++) {
        mCategoryKeeper[i] = new CollisionCategorizedKeeper(i);
    }

    MR::connectToScene(this, MR::MovementType_CollisionDirector, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_None);
}

void CollisionDirector::init(const TableIter& /*rIter*/) {
}

void CollisionDirector::initAfterPlacement() {
}

void CollisionDirector::movement() {
    for (s32 i = 0; i < CollisionCategory_Num; i++) {
        mCategoryKeeper[i]->movement();
    }
}

CollisionDirector* MR::getCollisionDirector() {
    return MR::getSceneObj< CollisionDirector >(SceneObj_CollisionDirector);
}
