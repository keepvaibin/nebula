#include "Game/MapObj/SubmarineVolcanoBigColumn.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/AirBubbleHolder.hpp"
#include "Game/Util.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);

namespace {
    NEBULA_TUNABLE(f32, sShakeDistance, 800.0f);
    static const s32 sGenerateInterval = NebulaFrameClock::stepsOfSeconds(6);
    NEBULA_TUNABLE(s32, sGenerateLife, 360);
    NEBULA_TUNABLE(f32, sGenerateOffsetY, 1250.0f);
};  // namespace

namespace NrvSubmarineVolcanoBigColumn {
    NEW_NERVE(SubmarineVolcanoBigColumnNrvWait, SubmarineVolcanoBigColumn, Wait);
    NEW_NERVE(SubmarineVolcanoBigColumnNrvBreak, SubmarineVolcanoBigColumn, Break);
};  // namespace NrvSubmarineVolcanoBigColumn

SubmarineVolcanoBigColumn::SubmarineVolcanoBigColumn(const char* pName) : LiveActor(pName) {
    mBreakModel = nullptr;
    mIsSmallColumn = false;
}

void SubmarineVolcanoBigColumn::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    const char* objName;
    MR::getObjectName(&objName, rIter);
    mIsSmallColumn = MR::isEqualString("SubmarineVolcanoSmallColumn", objName);
    initModelManagerWithAnm(objName, nullptr, false);
    initBreakModel(objName);
    MR::connectToSceneMapObj(this);
    initHitSensor(1);
    MR::addBodyMessageSensorMapObj(this);
    MR::initCollisionParts(this, objName, getSensor("body"), nullptr);
    initEffectKeeper(0, nullptr);
    initSound(2, false);
    MR::setClippingTypeSphereContainsModelBoundingBox(this, cNum_init_f1);
    MR::joinToGroupArray(this, rIter, nullptr, 32);

    if (MR::tryRegisterDemoCast(this, rIter)) {
        MR::tryRegisterDemoCast(mBreakModel, rIter);
    }

    if (mIsSmallColumn) {
        MR::createAirBubbleHolder();
    }

    initNerve(GET_NERVE(SubmarineVolcanoBigColumn, SubmarineVolcanoBigColumnNrvWait));
    makeActorAppeared();
}

void SubmarineVolcanoBigColumn::kill() {
    mBreakModel->kill();
    LiveActor::kill();
}

void SubmarineVolcanoBigColumn::exeWait() {
    if (mIsSmallColumn) {
        if (MR::isStep(this, ::sGenerateInterval)) {
            Vec3f offset;
            Vec3f upVec;
            MR::calcUpVec(&upVec, this);
            offset.scale(::sGenerateOffsetY, upVec);
            MR::appearAirBubble(mPosition + offset, ::sGenerateLife);
            setNerve(GET_NERVE(SubmarineVolcanoBigColumn, SubmarineVolcanoBigColumnNrvWait));
        }
    }
}

void SubmarineVolcanoBigColumn::exeBreak() {
    if (MR::isFirstStep(this)) {
        MR::startRumbleWithShakeCameraWeak(this, "強", "弱", ::sShakeDistance, FLOAT_MAX);
        MR::hideModel(this);
        MR::invalidateHitSensors(this);
        MR::invalidateCollisionParts(this);
        mBreakModel->appear();
        MR::startBck(mBreakModel, "Break");

        if (mIsSmallColumn) {
            MR::startSound(this, "SE_OJ_SMN_VOLC_COL_S_BREAK");
        } else {
            MR::startSound(this, "SE_OJ_SMN_VOLC_COL_L_BREAK");
        }
    }

    if (MR::isBckStopped(mBreakModel)) {
        kill();
    }
}

void SubmarineVolcanoBigColumn::pauseOff() {
    MR::requestMovementOn(this);
    MR::requestMovementOn(mBreakModel);
}

bool SubmarineVolcanoBigColumn::receiveMsgEnemyAttack(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (isNerve(GET_NERVE(SubmarineVolcanoBigColumn, SubmarineVolcanoBigColumnNrvWait))) {
        setNerve(GET_NERVE(SubmarineVolcanoBigColumn, SubmarineVolcanoBigColumnNrvBreak));
        return true;
    }

    return false;
}

void SubmarineVolcanoBigColumn::initBreakModel(const char* pModelName) {
    MR::FormattedName modelName("%sBreak", pModelName);
    mBreakModel = MR::createModelObjMapObj("海底火山石柱壊れモデル", modelName.c_str(), getBaseMtx());
    MR::invalidateClipping(mBreakModel);
    mBreakModel->makeActorDead();
}
