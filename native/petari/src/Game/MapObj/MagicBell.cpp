#include "Game/MapObj/MagicBell.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvMagicBell {
    NEW_NERVE(MagicBellNrvWait, MagicBell, Wait);
    NEW_NERVE(MagicBellNrvRing, MagicBell, Ring);
};  // namespace NrvMagicBell

namespace {
    // A ringing bell takes a new hit after this many steps.
    const s32 sRehitStep = 10;
    // The pointer must move faster than 8 screen units a frame to ring.
    const f32 sPointerFlickSpeedSq = 64.0f;
    // The bell and its clapper touch when their swing directions part this far.
    const f32 sClapperTouchCos = 0.95f;
};  // namespace

MagicBell::MagicBell(const char* pName)
    : LiveActor(pName), mBellSwinger(nullptr), mSurface2Mtx(nullptr), mBellRodSwinger(nullptr), mSurface1Mtx(nullptr),
      mHitMarkPosition(0.0f, 0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< MagicBell >);
}

void MagicBell::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("Bell", nullptr, false);
    mSurface2Mtx = MR::getJointMtx(this, "polySurface2");
    mSurface1Mtx = MR::getJointMtx(this, "polySurface1");
    MR::connectToSceneMapObjNoCalcAnim(this);
    initHitSensor(1);
    MR::addHitSensorMapObj(this, "body", 8, 70.0f, Vec3f(0.0f, -80.0f, 0.0f));
    initEffectKeeper(1, nullptr);
    MR::addEffect(this, "StarWandHitMark");
    MR::setEffectHostSRT(this, "StarWandHitMark", &mHitMarkPosition, nullptr, nullptr);
    initSound(4, false);
    initNerve(GET_NERVE(MagicBell, MagicBellNrvWait));
    MR::initShadowVolumeSphere(this, 100.0f);
    MR::useStageSwitchWriteA(this, rIter);
    MR::initStarPointerTarget(this, 100.0f, Vec3f(0.0f, -80.0f, 0.0f));
    Mtx34f mtx;
    mtx.identity();
    MR::makeMtxTR(mtx.toMtxPtr(), this);
    MR::calcGravity(this);
    mBellSwinger = new Swinger(&mPosition, mtx.toMtxPtr(), 100.0f, 0.60f, 0.99f, &mGravity);
    mBellRodSwinger = new Swinger(&mPosition, mtx.toMtxPtr(), 50.0f, 0.3f, 0.95f, &mGravity);
    makeActorAppeared();
}

// The bell and clapper joints take their swing, the actor scale and the actor position.
void MagicBell::updateJointMtx() {
    NebulaMtxCopy(mBellSwinger->mSwingMtx.toMtxPtr(), mSurface2Mtx);
    NebulaMtxCopy(mBellRodSwinger->mSwingMtx.toMtxPtr(), mSurface1Mtx);
    NebulaMtxScaleApply(mSurface2Mtx, mSurface2Mtx, mScale.x, mScale.y, mScale.z);
    NebulaMtxScaleApply(mSurface1Mtx, mSurface1Mtx, mScale.x, mScale.y, mScale.z);
    MR::setMtxTrans(mSurface2Mtx, mPosition.x, mPosition.y, mPosition.z);
    MR::setMtxTrans(mSurface1Mtx, mPosition.x, mPosition.y, mPosition.z);
}

void MagicBell::exeWait() {
    if (!tryRing()) {
        updateJointMtx();
    }
}

void MagicBell::exeRing() {
    if (MR::isFirstStep(this)) {
        if (MR::isValidSwitchA(this) && !MR::isOnSwitchA(this)) {
            MR::onSwitchA(this);
            MR::startSound(this, "SE_OJ_HAPPY_BELL_SWITCH_ON");
        }

        MR::startSound(this, "SE_OJ_HAPPY_BELL_RING");
    }

    mBellSwinger->update();
    mBellRodSwinger->update();

    const Vec3f& rRodDir = mBellRodSwinger->mSwingDir;
    if (mBellSwinger->mSwingDir.dot(rRodDir) < sClapperTouchCos) {
        // Contact normal: the bell's direction away from the clapper's, square to the clapper.
        Vec3f normal(mBellSwinger->mSwingDir);
        normal.sub(rRodDir);
        f32 along = rRodDir.dot(normal);
        normal.x -= along * rRodDir.x;
        normal.y -= along * rRodDir.y;
        normal.z -= along * rRodDir.z;
        MR::normalizeOrZero(&normal);
        f32 bellSpeed = mBellSwinger->mAcceleration.dot(normal);
        f32 rodSpeed = mBellRodSwinger->mAcceleration.dot(normal);
        f32 rodBackSpeed = rodSpeed >= 0.0f ? 0.0f : rodSpeed;

        // The bell pushes the clapper only while closing on it.
        if (bellSpeed - rodBackSpeed > 0.0f) {
            normal.scale(bellSpeed - rodBackSpeed);
            normal.sub(mBellRodSwinger->mAcceleration);
            mBellRodSwinger->accel(normal);
            mHitMarkPosition.set(mBellRodSwinger->mTipPos);
        }
    }

    updateJointMtx();

    if (MR::isGreaterStep(this, sRehitStep) && tryRing()) {
        return;
    }

    // Rings until it hangs straight down and nearly still.
    if (mBellSwinger->mSwingDir.y < -0.99f && mBellSwinger->mAcceleration.squared() < 0.1f * 0.1f) {
        MR::deleteEffect(this, "Ring");
        setNerve(GET_NERVE(MagicBell, MagicBellNrvWait));
    }
}

void MagicBell::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool MagicBell::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return false;
    }

    if (isNerve(GET_NERVE(MagicBell, MagicBellNrvWait)) ||
        (isNerve(GET_NERVE(MagicBell, MagicBellNrvRing)) && MR::isGreaterStep(this, sRehitStep))) {
        // Struck from the player's side, a little from below.
        Vec3f hitDir(mPosition);
        hitDir.sub(*MR::getPlayerPos());
        hitDir.y += 100.0f;
        MR::normalizeOrZero(&hitDir);
        Vec3f hitPos(hitDir * -200.0f);
        hitPos.add(mPosition);
        startRing(hitDir, hitPos);
        return true;
    }

    return false;
}

bool MagicBell::tryRing() {
    if (MR::isExecScenarioStarter() || !MR::isStarPointerPointing(this, 0, 0, "弱")) {
        return false;
    }

    const Vec2f* pScreenVel = MR::getStarPointerScreenVelocity(0);
    if (!(((pScreenVel->x * pScreenVel->x) + (pScreenVel->y * pScreenVel->y)) > sPointerFlickSpeedSq)) {
        return false;
    }

    Vec3f hitDir;
    MR::getStarPointerWorldVelocityDirection(&hitDir, 0);
    if (MR::isNearZero(hitDir)) {
        // Drawn z, y, x: the console's right-to-left order of the arguments (as the Windows build's), on every platform.
        const f32 z = MR::getRandom(-1.0f, 1.0f);
        const f32 y = MR::getRandom(-1.0f, 1.0f);
        const f32 x = MR::getRandom(-1.0f, 1.0f);
        hitDir.set(x, y, z);
        if (MR::isNearZero(hitDir)) {
            hitDir.set(1.0f, 0.0f, 0.0f);
        } else {
            MR::normalize(&hitDir);
        }
    }

    Vec3f hitPos;
    MR::calcStarPointerWorldPointingPos(&hitPos, mPosition, 0);
    startRing(hitDir, hitPos);
    return true;
}

// Stops the current swing and swings along rHitDir, 5 faster than before.
void MagicBell::startRing(const Vec3f& rHitDir, const Vec3f& rHitPos) {
    f32 speed = mBellSwinger->mAcceleration.length();
    mBellSwinger->accel(mBellSwinger->mAcceleration * -1.0f);
    mBellSwinger->accel(rHitDir * (5.0f + speed));
    mHitMarkPosition.set(rHitPos);
    MR::emitEffect(this, "StarWandHitMark");
    MR::emitEffect(this, "Ring");
    setNerve(GET_NERVE(MagicBell, MagicBellNrvRing));
}

MtxPtr MagicBell::getBaseMtx() const {
    return mSurface2Mtx;
}
