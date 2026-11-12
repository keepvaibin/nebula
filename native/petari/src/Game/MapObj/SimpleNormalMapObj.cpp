#include "Game/MapObj/SimpleNormalMapObj.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapPartsUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

SimpleNormalMapObj::SimpleNormalMapObj(const char* pName) : NormalMapBase(pName) {
    lumacore::setClass(this, lumacore::kClassId< SimpleNormalMapObj >);
}

void SimpleNormalMapObj::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    MR::FormattedName objectName;
    MR::getMapPartsObjectNameIfExistShapeID(objectName, rIter);

    initModelManagerWithAnm(objectName.c_str(), nullptr, false);
    MR::connectToSceneNormalMapObj(this);
    MR::initLightCtrlNoDrawMapObj(this);
    initNormalMap();

    if (MR::isExistCollisionResource(this, objectName.c_str())) {
        initHitSensor(1);
        MR::initCollisionParts(this, objectName.c_str(), MR::addBodyMessageSensorMapObj(this), nullptr);
    }

    MR::setClippingTypeSphereContainsModelBoundingBox(this, 100.0f);
    makeActorAppeared();
}
