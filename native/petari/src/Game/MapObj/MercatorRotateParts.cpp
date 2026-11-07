#include "Game/MapObj/MercatorRotateParts.hpp"
#include "Game/AreaObj/MercatorTransformCube.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"

MercatorRotateParts::MercatorRotateParts(const char* pName) : LiveActor(pName) {
    mRotator = nullptr;
    mAppearController = nullptr;
}

void MercatorRotateParts::initAfterPlacement() {
    Vec3f trans;
    Pos3f rotate;
    MR::convertMercatorPlaneToSphereTransAndRotate(&trans, &rotate, mLocalTrans, false);
    mPosition.x = trans.x;
    mPosition.y = trans.y;
    mPosition.z = trans.z;
    mAppearController->storeCurrentPosture();
}

bool MercatorRotateParts::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    return mAppearController->receiveMsg(msg);
}

void MercatorRotateParts::control() {
    mRotator->movement();
}

void MercatorRotateParts::calcAndSetBaseMtx() {
    Vec3f trans;
    Pos3f rotate;
    Mtx34f mtx;
    Pos3f baseMtx;
    baseMtx.identity();
    MR::makeMtxRotate(mtx.toMtxPtr(), mLocalRotate.x, mLocalRotate.y, mLocalRotate.z);
    baseMtx.concat(mRotator->getRotateMtx(), mtx);
    MR::convertMercatorPlaneToSphereTransAndRotate(&trans, &rotate, mLocalTrans, false);
    baseMtx.concat(rotate, baseMtx);
    baseMtx.mMtx[0][3] = trans.x;
    baseMtx.mMtx[1][3] = trans.y;
    baseMtx.mMtx[2][3] = trans.z;
    MR::setBaseTRMtx(this, baseMtx);
}

void MercatorRotateParts::init(const TableIter& rIter) {
    char objName[0x100];
    MR::getMapPartsObjectName(objName, sizeof(objName), rIter);
    MR::initDefaultPos(this, rIter);
    MR::getPlacementTrans(rIter, &mLocalTrans);
    MR::getPlacementRotate(rIter, &mLocalRotate);
    initModelManagerWithAnm(objName, nullptr, false);
    MR::connectToSceneMapObj(this);
    initHitSensor(1);
    MR::addHitSensorMapObj(this, "body", 4, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initCollisionParts(this, objName, getSensor("body"), nullptr);
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

MercatorRotateParts::~MercatorRotateParts() {
}
