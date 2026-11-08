#include "Game/MapObj/BenefitItemInvincible.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_format.h>
#include "Game/Util/StringUtil.hpp"

namespace {
    f32 cAppearThrowUpSpd = 10.0f;
    f32 cAppearThrowFwdSpd = 5.0f;
    f32 cBoundUpSpd = 20.0f;
};  // namespace

BenefitItemInvincible::BenefitItemInvincible(const char* pName) : BenefitItemObj(pName, "PowerupInvincible") {
    if (MR::isGalaxyDarkCometAppearInCurrentStage()) {
        mIsDisabled = 1;
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

    if (mShadowType) {
        MR::setShadowVolumeCylinderRadius(this, nullptr, 60.0f);
    }

    mSensorInvalidStep = 0;

    Vec3f axisY, axisZ;
    MR::getRotatedAxisZ(&axisZ, mRotation);
    mFwdAxis.set(axisZ);

    MR::getRotatedAxisY(&axisY, mRotation);
    mUpAxis.set(axisY);
}

void BenefitItemInvincible::initModelAndEfx() {
    initModelManagerWithAnm(mModelName, nullptr, false);
    initEffectKeeper(5, "BenefitItemObj");

    initBinder(60.0f, 60.0f, 0);
    initHitSensor(1);
    MR::addHitSensorMapObj(this, "body", 4, 60.0f, Vec3f(0.0f, 60.0f, 0.0f));

    MR::startBrk(this, "PowerupInvincible");
}

void BenefitItemInvincible::appearThrowUp() {
    if (!mEscapeType) {
        BenefitItemObj::appearThrowUp();
        return;
    }

    MR::startSystemSE("SE_SY_ITEM_APPEAR");

    setNerve(GET_NERVE(BenefitItemObj, HostTypeNrvShoot));

    mVelocity = mUpAxis * ::cAppearThrowUpSpd + mFwdAxis * ::cAppearThrowFwdSpd;

    MR::invalidateClipping(this);
}

void BenefitItemInvincible::initEscape() {
    BenefitItemObj::initEscape();

    MR::calcGravity(this);

    mEscapeVelocity = -mGravity * ::cBoundUpSpd;
    mEscapeVelocity += mFwdAxis * 10.0f;

    mSensorInvalidStep = 60;
}
void BenefitItemInvincible::doEscape() {
    MR::calcGravity(this);

    MR::addVelocityToGravity(this, 1.0f);

    if (MR::isBindedGround(this)) {
        MR::vecKillElement(mFwdAxis, mGravity, &mFwdAxis);
        MR::normalizeOrZero(&mFwdAxis);

        mEscapeVelocity = mFwdAxis * 10.0f - mGravity * ::cBoundUpSpd;

        MR::startSound(this, "SE_OJ_POW_INVINCIBLE_BOUND");

        mFallVelocity.zero();
    } else {
        mEscapeVelocity.x *= 0.98f;
        mEscapeVelocity.y *= 0.98f;
        mEscapeVelocity.z *= 0.98f;
        mIsInAir = 1;

        mFallVelocity += mGravity * 0.5f;

        if (mFallVelocity.length() > 20.0f) {
            mFallVelocity.setLength(20.0f);
        }
    }

    if (mSensorInvalidStep > 0) {
        mSensorInvalidStep--;
    }

    if (mSensorInvalidStep == 0) {
        MR::validateHitSensors(this);
    }
}

void BenefitItemInvincible::runEfx(const char* pEffect) {
    MR::FormattedName str;
    if (!strcmp(pEffect, "Glow")) {
        str.format("%sPowerUpInvincible", pEffect);
        MR::emitEffect(this, str.c_str());
    }
}

void BenefitItemInvincible::stopEfx(const char* pEffect) {
    MR::FormattedName str;
    if (!strcmp(pEffect, "Glow")) {
        str.format("%sPowerUpInvincible", pEffect);
        MR::emitEffect(this, str.c_str());
    }
}

BenefitItemInvincible::~BenefitItemInvincible() {
}

void BenefitItemInvincible::runBck(const char*) {
}
