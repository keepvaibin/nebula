#include "Game/Render/Material.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/MapObj/FlexibleSphere.hpp"
#include "Game/Player/PlayerModel.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapPartsUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Render/ModelData.hpp"
#include <nebula_math.h>
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 3000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f1, "draw.f1", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f2, "draw.f2", 0.95f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_startQuake_shakeCameraInfinity_1, "startQuake.shakeCameraInfinity.1", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_startQuake_shakeCameraInfinity_2, "startQuake.shakeCameraInfinity.2", 3.0f);

namespace {
    // Placement args 0-2 are scales in ten-thousandths.
    NEBULA_TUNABLE(f32, cArgScaleUnit, 10000.0f);
    NEBULA_TUNABLE(f32, cDefaultMaxScale, 1.0f);
    NEBULA_TUNABLE(f32, cDefaultMinScale, 0.1f);
    NEBULA_TUNABLE(f32, cDefaultScaleRate, 0.002f);
    NEBULA_TUNABLE(f32, cQuakeHearDistance, 4000.0f);
    NEBULA_TUNABLE(u16, cQuakeFrames, 60);
    const s32 cTimerSoundInterval = NebulaFrameClock::stepsOfSeconds(1);

    // The texture coordinate scale of the sphere being drawn: the surface pattern keeps its size as the sphere shrinks.
    // Set by draw() just before directDraw() runs the hook.
    f32 sCoordScale = 1.0f;

    void flexibleCoordinates(MaterialDraw& rDraw) {
        Mtx mtx;
        NebulaMtxScale(mtx, sCoordScale, sCoordScale, 0.0f);

        rDraw.applyPass(NEBULA_FORM_PASS_FLEXIBLE_COORDINATES);
        rDraw.setCoordinate(0, mtx);
        rDraw.setCoordinate(1, mtx);
        rDraw.setCoordinate(2, mtx);
    }
};  // namespace

FlexibleSphere::FlexibleSphere(const char* pName) : LiveActor(pName), mTimer() {
    lumacore::setClass(this, lumacore::kClassId< FlexibleSphere >);
}

// Grows to its max scale and shrinks to its min, quaking at each end, once the player stands on it (or switch A turns
// on). Arg 3 = 1 shrinks only, with a countdown, and vanishes at the min.
void FlexibleSphere::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    MR::FormattedName arcName;
    MR::getMapPartsObjectNameIfExistShapeID(arcName, rIter);
    initModelManagerWithAnm(arcName.c_str(), nullptr, false);

    initHitSensor(1);
    MR::addHitSensorMapObj(this, "body", 8, cNum_init_f1, Vec3f(0.0f, 0.0f, 0.0f));
    MR::invalidateHitSensors(this);

    initEffectKeeper(0, nullptr);

    initSound(4, false);

    MR::initCollisionParts(this, arcName.c_str(), getSensor("body"), nullptr);

    mIsGrowing = true;

    MR::connectToScene(this, MR::MovementType_Planet, MR::CalcAnimType_Planet, MR::DrawBufferType_None, MR::DrawType_FlexibleSphere);

    MR::setGroupClipping(this, rIter, 32);
    MR::setClippingTypeSphere(this, cNum_init_f2);
    MR::setClippingFarMax(this);

    // ModelUtil makes FlexibleSphere models PlayerModels.
    PlayerModel* pModel = static_cast< PlayerModel* >(MR::getModel(this));
    pModel->setDrawHook(flexibleCoordinates);

    if (MR::isValidInfo(rIter)) {
        MR::useStageSwitchReadA(this, rIter);
        MR::useStageSwitchWriteDead(this, rIter);
    }

    makeActorAppeared();

    mMaxScale = cDefaultMaxScale;
    mMinScale = cDefaultMinScale;
    mScaleRate = cDefaultScaleRate;
    mVanishOnShrinkEnd = false;

    if (MR::isValidInfo(rIter)) {
        s32 maxScale = -1;
        s32 minScale = -1;
        s32 scaleRate = -1;
        s32 vanishType = -1;

        MR::getPlacementArg0NoInit(rIter, &maxScale);
        MR::getPlacementArg1NoInit(rIter, &minScale);
        MR::getPlacementArg2NoInit(rIter, &scaleRate);
        MR::getPlacementArg3NoInit(rIter, &vanishType);

        if (maxScale != -1) {
            mMaxScale = maxScale / cArgScaleUnit;
        }

        if (minScale != -1) {
            mMinScale = minScale / cArgScaleUnit;
        }

        if (scaleRate != -1) {
            mScaleRate = scaleRate / cArgScaleUnit;
        }

        mVanishOnShrinkEnd = vanishType == 1;
    }

    mShakeTimer = 0;
    mEnabled = false;

    if (mVanishOnShrinkEnd) {
        mIsGrowing = false;
        // A rate of 0 (placement arg 2) never shrinks: no steps to count.
        mTimer = mScaleRate > 0.0f ? static_cast< s32 >((mScale.x - mMinScale) / mScaleRate) : 0;
    }
}

void FlexibleSphere::kill() {
    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }

    if (MR::isRegisteredEffect(this, "Vanish")) {
        MR::emitEffect(this, "Vanish");
    }

    LiveActor::kill();
}

// Nothing moves while a quake shakes the camera.
void FlexibleSphere::control() {
    if (mShakeTimer != 0) {
        updateQuake();
        return;
    }

    if (!mEnabled && !tryStart()) {
        return;
    }

    // Equal limits (placement args 0 and 1) give the sound no range, not a division by 0.
    const f32 scaleRange = mMaxScale - mMinScale;
    MR::startLevelSound(this, "SE_OJ_LV_FLEX_SPHERE_MOVE", scaleRange != 0.0f ? ((mScale.x - mMinScale) / scaleRange) * cNum_control_f1 : 0.0f);

    f32 scale = mScale.x;

    if (mIsGrowing) {
        if (scale < mMaxScale) {
            scale = scale + mScaleRate;
            mScale.set(scale);
        } else {
            mIsGrowing = false;
            startQuake();
        }
        return;
    }

    if (scale > mMinScale) {
        scale = scale - mScaleRate;
        mScale.set(scale);

        if (mVanishOnShrinkEnd) {
            updateVanishTimer();
        }
    } else if (mVanishOnShrinkEnd) {
        MR::startSystemSE("SE_SY_TIMER_A_0");
        MR::startSound(this, "SE_OJ_FLEX_SPHERE_VANISH");
        kill();
    } else {
        mIsGrowing = true;
        startQuake();
    }
}

// Switch A when it has one, else the player standing on it.
bool FlexibleSphere::tryStart() {
    if (MR::isValidSwitchA(this)) {
        if (!MR::isOnSwitchA(this)) {
            return false;
        }

        mEnabled = true;

        if (MR::isRegisteredEffect(this, "Wind")) {
            MR::emitEffect(this, "Wind");
        }

        return true;
    }

    if (!MR::isActorOnPlayer(this)) {
        return false;
    }

    mEnabled = true;
    return true;
}

// Ticks once a second, faster-sounding in the last ten seconds and the last two; quiet while the player is dead or a star
// is being collected.
void FlexibleSphere::updateVanishTimer() {
    if (mTimer <= 0) {
        return;
    }

    if (!MR::isPlayerDead() && !MR::isPowerStarGetDemoActive() && mTimer % cTimerSoundInterval == 0) {
        if (mTimer <= 120) {
            MR::startSystemSE("SE_SY_TIMER_A_QUASI_0");
        } else if (mTimer <= 600) {
            MR::startSystemSE("SE_SY_TIMER_A_1");
        } else {
            MR::startSystemSE("SE_SY_TIMER_A_2");
        }
    }

    mTimer--;
}

void FlexibleSphere::startQuake() {
    mShakeTimer = cQuakeFrames;

    if (MR::calcDistanceToPlayer(mPosition) < cQuakeHearDistance) {
        MR::shakeCameraInfinity(this, cNum_startQuake_shakeCameraInfinity_1, cNum_startQuake_shakeCameraInfinity_2);
        MR::invalidateClipping(this);
    }

    if (MR::isActorOnPlayer(this)) {
        MR::tryRumblePad(this, "マリオ[しびれ]", PLAYER_1);
    }
}

void FlexibleSphere::updateQuake() {
    if (MR::calcDistanceToPlayer(mPosition) < cQuakeHearDistance) {
        MR::startLevelSound(this, "SE_AT_LV_EARTHQUAKE");
    }

    if (--mShakeTimer == 0) {
        MR::stopShakingCamera(this);
        MR::validateClipping(this);
    }
}

void FlexibleSphere::draw() const {
    if (!MR::isDead(this)) {
        PlayerModel* pModel = static_cast< PlayerModel* >(MR::getModel(this));
        pModel->viewCalc2();
        // A sphere shrunk to a scale of 0 (a minimum of 0 in placement arg 1) keeps the plain pattern size.
        sCoordScale = mScale.x > 0.0f ? cNum_draw_f1 / mScale.x + cNum_draw_f2 : 1.0f;
        pModel->directDraw();
    }
}
