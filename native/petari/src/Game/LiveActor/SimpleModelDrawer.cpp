#include "Game/LiveActor/SimpleModelDrawer.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

SimpleModelDrawer::SimpleModelDrawer(NameObj* pObj, const char* pName, const char* pModelName, int) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< SimpleModelDrawer >);
    mMaterial = nullptr;
    mShape = nullptr;

    initModelManagerWithAnm(pModelName, nullptr, false);
    mMaterial = MR::getMaterial(this, 0);
    mShape = mMaterial->getMesh();
    makeActorDead();
}
