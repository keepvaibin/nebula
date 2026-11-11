#include "Game/MapObj/LavaStrangeRock.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEBULA_TUNABLE(f32, sHitRange, 870.0f);
    NEBULA_TUNABLE(f32, sHitRangeL, 1300.0f);
    NEBULA_TUNABLE(f32, sHitRangeTable, 730.0f);
    NEBULA_TUNABLE(f32, sRockSLength, 150.0f);
    NEBULA_TUNABLE(f32, sRockLLength, 200.0f);
    NEBULA_TUNABLE(f32, sRockTableLength, 285.0f);
    NEBULA_TUNABLE(f32, sPushVelocity, 50.0f);
};  // namespace

namespace NrvLavaStrangeRock {
    NEW_NERVE(LavaStrangeRockNrvWait, LavaStrangeRock, Wait);
};  // namespace NrvLavaStrangeRock

LavaStrangeRock::LavaStrangeRock(const char* pName) : LiveActor(pName), mRockType(Type_None), mHitType(), mLodCtrlPlanet() {
    lumacore::setClass(this, lumacore::kClassId< LavaStrangeRock >);
}

void LavaStrangeRock::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    s32 hitType = -1;
    MR::getPlacementArg0NoInit(rIter, &hitType);

    if (hitType == -1 || hitType == 0) {
        mHitType = 0;
    } else if (hitType == 1) {
        mHitType = 1;
    } else if (hitType == 2) {
        mHitType = 2;
    }

    const char* objName = nullptr;
    MR::getObjectName(&objName, rIter);

    if (MR::isEqualString(objName, "LavaStrangeRockTable")) {
        mRockType = Type_Table;
        setName("溶岩奇岩台形");
    } else if (MR::isEqualString(objName, "LavaStrangeRockL")) {
        mRockType = Type_Large;
        setName("溶岩奇岩大");
    } else {
        mRockType = Type_Normal;
    }

    initModelAndClipping(rIter);

    if (mRockType == Type_Large) {
        mLodCtrlPlanet = MR::createLodCtrlPlanet(this, rIter, -1);
    }

    MR::connectToSceneMapObj(this);
    initEffect();
    initSound(4, false);
    initNerve(GET_NERVE(LavaStrangeRock, LavaStrangeRockNrvWait));
    makeActorAppeared();
}

void LavaStrangeRock::initModelAndClipping(const TableIter& rIter) {
    MR::setGroupClipping(this, rIter, 16);

    switch (mRockType) {
    case Type_Normal:
        initModelManagerWithAnm("LavaStrangeRock", nullptr, false);
        MR::setClippingTypeSphere(this, ::sHitRange * mScale.y);
        break;
    case Type_Large:
        initModelManagerWithAnm("LavaStrangeRockL", nullptr, false);
        MR::setClippingTypeSphere(this, ::sHitRangeL * mScale.y);
        break;
    case Type_Table:
        initModelManagerWithAnm("LavaStrangeRockTable", nullptr, false);
        MR::setClippingTypeSphere(this, ::sHitRangeTable * mScale.y);
        break;
    }
}

void LavaStrangeRock::initEffect() {
    switch (mRockType) {
    case Type_Normal:
        initEffectKeeper(1, "LavaStrangeRock");
        break;
    case Type_Large:
        initEffectKeeper(1, "LavaStrangeRockL");
        break;
    case Type_Table:
        initEffectKeeper(1, "LavaStrangeRockTable");
        break;
    }
}

void LavaStrangeRock::control() {
    if (mRockType == Type_Large) {
        mLodCtrlPlanet->update();
    }
}

void LavaStrangeRock::exeWait() {
    Vec3f centerPos = *MR::getPlayerCenterPos();
    centerPos -= *MR::getPlayerVelocity();

    Vec3f upVec;
    MR::calcUpVec(&upVec, this);

    Vec3f hitOffset = upVec;
    f32 radius = 0.0f;

    switch (mRockType) {
    case Type_Normal:
        hitOffset *= ::sHitRange;
        radius = ::sRockSLength * mScale.y;
        break;
    case Type_Large:
        hitOffset *= ::sHitRangeL;
        radius = ::sRockLLength * mScale.y;
        break;
    case Type_Table:
        hitOffset *= ::sHitRangeTable;
        radius = ::sRockTableLength * mScale.y;
        break;
    }

    if (mHitType == 1) {
        if (!MR::isPlayerInRush()) {
            return;
        }

        if (!MR::checkHitSegmentSphere(*MR::getPlayerCenterPos(), mPosition, mPosition + hitOffset, radius, nullptr)) {
            return;
        }

        MR::emitEffect(this, "Break");

        switch (mRockType) {  // the lack of breaks here is intentional
        case Type_Normal:
            MR::startSound(this, "SE_OJ_STRANGEROCK_BREAK_S");
        case Type_Large:
            MR::startSound(this, "SE_OJ_STRANGEROCK_BREAK_L");
        case Type_Table:
            MR::startSound(this, "SE_OJ_STRANGEROCK_BREAK_M");
        }

        kill();
    } else if (mHitType == 2) {
        Vec3f hitCheckDir;

        if (MR::checkHitSegmentSphere(*MR::getPlayerCenterPos(), mPosition, mPosition + hitOffset, radius, &hitCheckDir)) {
            MR::pushPlayer(hitCheckDir * ::sPushVelocity);
        }
    }
}
