#include "Game/MapObj/SeesawMoveNut.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_exeHalfway_f1, "exeHalfway.f1", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHalfway_f2, "exeHalfway.f2", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHalfway_f3, "exeHalfway.f3", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHalfway_f4, "exeHalfway.f4", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_autoBackHome_f1, "autoBackHome.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_autoBackHome_f2, "autoBackHome.f2", 0.02f);

namespace NrvSeesawMoveNut {
    NEW_NERVE(SeesawMoveNutNrvHalfway, SeesawMoveNut, Halfway);
    NEW_NERVE(SeesawMoveNutNrvEdge, SeesawMoveNut, Edge);
};  // namespace NrvSeesawMoveNut

SeesawMoveNut::SeesawMoveNut(const char* pName) : MapObjActor(pName), mSpeed(100.0f), mPrevAngularSpeed(), mBackHomeStep() {
}

void SeesawMoveNut::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo initInfo;

    initInfo.setupDefaultPos();
    initInfo.setupConnectToScene();
    initInfo.setupSeesaw1AxisRotator(nullptr, 0.0f);
    initInfo.setupEffect(nullptr);
    initInfo.setupSound(6);

    initialize(rIter, initInfo);
    MR::setBodySensorType(this, ATYPE_MAP_OBJ_PRESS);
    MR::getPlacementArg0NoInit(rIter, &mSpeed);

    MR::calcNearestRailPos(&mPosition, this, mPosition);

    if (isAtEdge()) {
        initNerve(GET_NERVE(SeesawMoveNut, SeesawMoveNutNrvEdge));
        MapObjActorUtil::endRotator(this);
    } else {
        initNerve(GET_NERVE(SeesawMoveNut, SeesawMoveNutNrvHalfway));
        MapObjActorUtil::startRotator(this);
    }
}

void SeesawMoveNut::exeHalfway() {
    f32 angularSpeed = MapObjActorUtil::getSeesaw1AxisAngularSpeed(this);

    if (angularSpeed > 0.0f) {
        MR::setRailDirectionToEnd(this);
    } else if (angularSpeed < 0.0f) {
        MR::setRailDirectionToStart(this);
    }

    MR::moveCoordAndFollowTrans(this, MR::abs(angularSpeed * mSpeed * cNum_exeHalfway_f1));

    autoBackHome();

    if (mPrevAngularSpeed == 0.0f && angularSpeed != 0.0f) {
        MR::startSound(this, "SE_OJ_SEESAW_NUT_START");
    }

    mPrevAngularSpeed = angularSpeed;

    if (angularSpeed == 0.0f) {
        return;
    }

    if (MR::abs(angularSpeed) >= cNum_exeHalfway_f2) {
        MR::startLevelSound(this, "SE_OJ_LV_SEESAW_NUT_ROT_L");
    }

    if (MR::abs(angularSpeed) >= cNum_exeHalfway_f3) {
        MR::startLevelSound(this, "SE_OJ_LV_SEESAW_NUT_ROT_M");
    }

    if (MR::abs(angularSpeed) >= cNum_exeHalfway_f4) {
        MR::startLevelSound(this, "SE_OJ_LV_SEESAW_NUT_ROT_H");
    }

    if (isAtEdge()) {
        MapObjActorUtil::endRotator(this);
        MR::reverseRailDirection(this);
        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::shakeCameraNormal();
        MR::startSound(this, "SE_OJ_SEESAW_NUT_STOP");
        setNerve(GET_NERVE(SeesawMoveNut, SeesawMoveNutNrvEdge));
    }
}

void SeesawMoveNut::exeEdge() {
    if (isEnableStart()) {
        MapObjActorUtil::startRotator(this);
        mBackHomeStep = 0;
        setNerve(GET_NERVE(SeesawMoveNut, SeesawMoveNutNrvHalfway));
    } else {
        autoBackHome();
    }
}

void SeesawMoveNut::autoBackHome() {
    if (mBackHomeStep >= NebulaFrameClock::stepsOfSeconds(3)) {
        if (isNerve(GET_NERVE(SeesawMoveNut, SeesawMoveNutNrvEdge)) && MR::getRailCoord(this) == MR::getRailTotalLength(this)) {
            MapObjActorUtil::startRotator(this);
            setNerve(GET_NERVE(SeesawMoveNut, SeesawMoveNutNrvHalfway));
            return;
        }

        if (MR::abs(MapObjActorUtil::getSeesaw1AxisAngularSpeed(this)) < cNum_autoBackHome_f1) {
            MapObjActorUtil::forceRotateSeesaw1Axis(this, -cNum_autoBackHome_f2);
        }
    }

    if (MR::isOnPlayer(this)) {
        mBackHomeStep = 0;
    } else {
        mBackHomeStep++;
    }
}

bool SeesawMoveNut::isEnableStart() const {
    if (!MR::isOnPlayer(this)) {
        return false;
    }

    Vec3f frontVec;
    MR::calcFrontVec(&frontVec, this);

    Vec3f dirToPlayerNoFront;
    dirToPlayerNoFront.sub(*MR::getPlayerPos(), mPosition);
    dirToPlayerNoFront.orthogonalize(frontVec);

    MR::normalize(&dirToPlayerNoFront);

    return dirToPlayerNoFront.cross(mGravity).dot(MR::getRailDirection(this)) > 0.0f;
}

bool SeesawMoveNut::isAtEdge() const {
    return MR::getRailCoord(this) == 0.0f || MR::getRailCoord(this) == MR::getRailTotalLength(this);
}
