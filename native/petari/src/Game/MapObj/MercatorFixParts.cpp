#include "Game/MapObj/MercatorFixParts.hpp"
#include "Game/AreaObj/MercatorTransformCube.hpp"
#include "Game/MapObj/MapPartsAppearController.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapPartsUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

MercatorFixParts::MercatorFixParts(const char* pName) : LiveActor(pName), mAppearController() {
    lumacore::setClass(this, lumacore::kClassId< MercatorFixParts >);
}

bool MercatorFixParts::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return mAppearController->receiveMsg(msg);
}

void MercatorFixParts::init(const TableIter& rIter) {
    MR::FormattedName objectName;
    MR::getMapPartsObjectName(objectName, rIter);
    MR::initDefaultPosForMercator(this, rIter, false);
    initModelManagerWithAnm(objectName.c_str(), nullptr, false);
    MR::connectToSceneMapObj(this);
    initHitSensor(1);
    MR::addHitSensorMapObj(this, "body", 4, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initCollisionParts(this, objectName.c_str(), getSensor("body"), nullptr);

    mAppearController = new MapPartsAppearController(this);
    mAppearController->init(rIter);
    mAppearController->start();

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
    } else {
        makeActorAppeared();
    }

    MR::initMapPartsClipping(this, rIter, nullptr, false);
}
