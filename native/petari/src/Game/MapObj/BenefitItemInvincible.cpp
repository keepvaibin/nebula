#include "Game/MapObj/BenefitItemInvincible.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_doEscape_addVelocityToGravity_1, "doEscape.addVelocityToGravity.1", 1.0f);

namespace {
    NEBULA_TUNABLE(f32, cAppearThrowUpSpd, 10.0f);
    NEBULA_TUNABLE(f32, cAppearThrowFwdSpd, 5.0f);
    NEBULA_TUNABLE(f32, cBoundUpSpd, 20.0f);
    NEBULA_TUNABLE(f32, cBoundFwdSpd, 10.0f);
    NEBULA_TUNABLE(f32, cAirFriction, 0.98f);
    NEBULA_TUNABLE(f32, cFallAccel, 0.5f);
    NEBULA_TUNABLE(f32, cFallSpeedMax, 20.0f);
    NEBULA_TUNABLE(f32, cRadius, 60.0f);
    const s32 cEscapeSensorOffSteps = NebulaFrameClock::stepsOfSeconds(1);
};  // namespace

// Never on a stage where a dark comet shows.
BenefitItemInvincible::BenefitItemInvincible(const char* pName) : BenefitItemObj(pName, "PowerupInvincible") {
    lumacore::setClass(this, lumacore::kClassId< BenefitItemInvincible >);
    if (MR::isGalaxyDarkCometAppearInCurrentStage()) {
        mIsDisabled = true;
    }
}

void BenefitItemInvincible::exeCatch() {
    MR::startSystemSE("SE_SY_POWER_UP_2");
    MR::hideModel(this);
    MR::changePlayerItemStatus(PlayerItemStatus_Invincible);
    kill();
}

void BenefitItemInvincible::init(const TableIter& rIter) {
    BenefitItemObj::init(rIter);

    if (mIsDisabled) {
        return;
    }

    if (mShadowType != 0) {
        MR::setShadowVolumeCylinderRadius(this, nullptr, cRadius);
    }

    mSensorInvalidStep = 0;

    Vec3f axisZ;
    MR::getRotatedAxisZ(&axisZ, mRotation);
    mFwdAxis.set(axisZ);

    Vec3f axisY;
    MR::getRotatedAxisY(&axisY, mRotation);
    mUpAxis.set(axisY);
}

void BenefitItemInvincible::initModelAndEfx() {
    initModelManagerWithAnm(mModelName, nullptr, false);
    initEffectKeeper(5, "BenefitItemObj");

    initBinder(cRadius, cRadius, 0);
    initHitSensor(1);
    MR::addHitSensorMapObj(this, "body", 4, cRadius, Vec3f(0.0f, cRadius, 0.0f));

    MR::startBrk(this, "PowerupInvincible");
}

// An escaping one is thrown forward as well as up.
void BenefitItemInvincible::appearThrowUp() {
    if (mEscapeType == EscapeType_None) {
        BenefitItemObj::appearThrowUp();
        return;
    }

    MR::startSystemSE("SE_SY_ITEM_APPEAR");
    setNerve(GET_NERVE(BenefitItemObj, HostTypeNrvShoot));
    mVelocity = mUpAxis * cAppearThrowUpSpd + mFwdAxis * cAppearThrowFwdSpd;
    MR::invalidateClipping(this);
}

// It escapes bouncing forward, untouchable for the first steps.
void BenefitItemInvincible::initEscape() {
    BenefitItemObj::initEscape();

    MR::calcGravity(this);

    mEscapeVelocity = -mGravity * cBoundUpSpd;
    mEscapeVelocity += mFwdAxis * cBoundFwdSpd;

    mSensorInvalidStep = cEscapeSensorOffSteps;
}

void BenefitItemInvincible::doEscape() {
    MR::calcGravity(this);
    MR::addVelocityToGravity(this, cNum_doEscape_addVelocityToGravity_1);

    if (MR::isBindedGround(this)) {
        MR::vecKillElement(mFwdAxis, mGravity, &mFwdAxis);
        MR::normalizeOrZero(&mFwdAxis);

        mEscapeVelocity = mFwdAxis * cBoundFwdSpd - mGravity * cBoundUpSpd;
        MR::startSound(this, "SE_OJ_POW_INVINCIBLE_BOUND");
        mFallVelocity.zero();
    } else {
        mEscapeVelocity.scale(cAirFriction);
        mIsInAir = true;

        mFallVelocity += mGravity * cFallAccel;
        if (mFallVelocity.length() > cFallSpeedMax) {
            mFallVelocity.setLength(cFallSpeedMax);
        }
    }

    if (mSensorInvalidStep > 0) {
        mSensorInvalidStep--;
    }

    if (mSensorInvalidStep == 0) {
        MR::validateHitSensors(this);
    }
}

// Only the glow has an effect of its own.
void BenefitItemInvincible::runEfx(const char* pEffect) {
    if (MR::isEqualString(pEffect, "Glow")) {
        MR::emitEffect(this, "GlowPowerUpInvincible");
    }
}

void BenefitItemInvincible::stopEfx(const char* pEffect) {
    if (MR::isEqualString(pEffect, "Glow")) {
        MR::emitEffect(this, "GlowPowerUpInvincible");
    }
}

void BenefitItemInvincible::runBck(const char*) {
}
