#include "Game/MapObj/MercatorRotateParts.hpp"
#include "Game/AreaObj/MercatorTransformCube.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

MercatorRotateParts::MercatorRotateParts(const char* pName) : LiveActor(pName), mRotator(nullptr), mAppearController(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< MercatorRotateParts >);
}

void MercatorRotateParts::initAfterPlacement() {
    Vec3f trans;
    Pos3f rotate;
    MR::convertMercatorPlaneToSphereTransAndRotate(&trans, &rotate, mLocalTrans, false);
    mPosition.set(trans);
    mAppearController->storeCurrentPosture();
}

bool MercatorRotateParts::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return mAppearController->receiveMsg(msg);
}

void MercatorRotateParts::control() {
    mRotator->movement();
}

void MercatorRotateParts::calcAndSetBaseMtx() {
    Mtx34f localRotateMtx;
    MR::makeMtxRotate(localRotateMtx.toMtxPtr(), mLocalRotate.x, mLocalRotate.y, mLocalRotate.z);
    Pos3f baseMtx;
    baseMtx.concat(mRotator->getRotateMtx(), localRotateMtx);

    Vec3f trans;
    Pos3f rotate;
    MR::convertMercatorPlaneToSphereTransAndRotate(&trans, &rotate, mLocalTrans, false);
    baseMtx.concat(rotate, baseMtx);
    baseMtx.setTrans(trans);
    MR::setBaseTRMtx(this, baseMtx);
}

void MercatorRotateParts::init(const TableIter& rIter) {
    MR::FormattedName objName;
    MR::getMapPartsObjectName(objName, rIter);
    MR::initDefaultPos(this, rIter);
    MR::getPlacementTrans(rIter, &mLocalTrans);
    MR::getPlacementRotate(rIter, &mLocalRotate);
    initModelManagerWithAnm(objName.c_str(), nullptr, false);
    MR::connectToSceneMapObj(this);
    initHitSensor(1);
    MR::addHitSensorMapObj(this, "body", 4, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initCollisionParts(this, objName.c_str(), getSensor("body"), nullptr);
    mRotator = new MapPartsRotator(this);
    mRotator->init(rIter);
    mRotator->start();
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
