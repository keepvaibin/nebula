#include "Game/MapObj/BallOpener.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_exeSetCenter_f1, "exeSetCenter.f1", 0.0001f);
NEBULA_TUNABLE_NAMED(f32, cNum_bindHole_f1, "bindHole.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_bindHole_f2, "bindHole.f2", 0.1f);

namespace {
    NEBULA_TUNABLE(f32, sGravityAcc, 1.2f);
    NEBULA_TUNABLE(f32, sHoleRadius, 200.0f);
    NEBULA_TUNABLE(f32, sHoleDepth, 400.0f);
    NEBULA_TUNABLE(f32, sHoleRefrecRate, 0.9f);
    NEBULA_TUNABLE(s32, sForceSetStartTime, 40);
    const s32 sForceSetEndTime = NebulaFrameClock::stepsOfMilliseconds(1500);
    NEBULA_TUNABLE(f32, sBallToCenterFreq, 0.98f);
    NEBULA_TUNABLE(f32, sBallToCenterEndFreq, 0.7f);
    const s32 sForceSetCenterTime = NebulaFrameClock::stepsOfSeconds(3);
    NEBULA_TUNABLE(s32, sLaunchFixTime, 20);
    NEBULA_TUNABLE(s32, sSetUpTime, 45);
    NEBULA_TUNABLE(f32, sLaunchValidHeight, 160.0f);
};  // namespace

namespace NrvBallOpener {
    NEW_NERVE(BallOpenerNrvWait, BallOpener, Wait);
    NEW_NERVE(BallOpenerNrvSetCenter, BallOpener, SetCenter);
    NEW_NERVE(BallOpenerNrvOpen, BallOpener, Open);
};  // namespace NrvBallOpener

BallOpener::BallOpener(const char* pName)
    : LiveActor(pName), mBoundSensor(), mInitialVelocity(0, 0, 0), mBindVelocity(0, 0, 0), mSettledFrames() {
    mCone = new BindCone(mPosition, Vec3f(0.0f, 1.0f, 0.0f), ::sHoleDepth, ::sHoleRadius);
}

void BallOpener::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("BallOpener", nullptr, false);
    MR::connectToSceneMapObj(this);
    initHitSensor(1);
    MR::addHitSensor(this, "body", ATYPE_JUMP_HOLE, 8, ::sHoleRadius, Vec3f(0.0f, 0.0f, 0.0f));
    initEffectKeeper(1, nullptr);
    initSound(4, false);
    MR::initCollisionParts(this, "BallOpener", getSensor(nullptr), nullptr);
    mCone->setPosition(mPosition);
    initNerve(GET_NERVE(BallOpener, BallOpenerNrvWait));
    MR::useStageSwitchWriteA(this, rIter);
    appear();
}

void BallOpener::initAfterPlacement() {
    MR::calcGravityVector(this, &mHoleGravity, nullptr, 0);
    mCone->setDirection(mHoleGravity);
}

void BallOpener::control() {
}

bool BallOpener::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (msg == ACTMES_SPHERE_PLAYER_BINDED) {
        f32 radiusSquared = pReceiver->mRadius * pReceiver->mRadius;

        Vec3f offset = pReceiver->mPosition - pSender->mPosition;
        Vec3f lateral;
        lateral.killElement2(offset, mHoleGravity);

        if (lateral.squared() <= radiusSquared && offset.squared(lateral) <= ::sLaunchValidHeight * ::sLaunchValidHeight) {
            mBoundSensor = pSender;
            mInitialVelocity = pSender->mHost->mVelocity;
            mBindVelocity = mInitialVelocity;
            MR::zeroVelocity(mBoundSensor->mHost);
            return true;
        }
    }

    return false;
}

void BallOpener::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
    }

    if (mBoundSensor != nullptr) {
        bindHole();
        setNerve(GET_NERVE(BallOpener, BallOpenerNrvSetCenter));
    }
}

void BallOpener::exeSetCenter() {
    if (MR::isFirstStep(this)) {
        mSettledFrames = 0;
        MR::invalidateClipping(this);
    }

    bindHole();

    if (mBindVelocity.squared() < cNum_exeSetCenter_f1) {
        mSettledFrames++;
    } else {
        mSettledFrames = 0;
    }

    if (mSettledFrames == 1) {
        MR::shakeCameraWeak();
    }

    MR::startLevelSound(this, "SE_OJ_LV_BALL_OPN_SETTING");

    if (mSettledFrames > ::sLaunchFixTime || MR::isGreaterStep(this, ::sForceSetCenterTime)) {
        setNerve(GET_NERVE(BallOpener, BallOpenerNrvOpen));
    }
}

void BallOpener::exeOpen() {
    if (MR::isFirstStep(this)) {
        mBoundSensor->receiveMessage(ACTMES_SET_UP_JUMP_HOLE, getSensor("body"));
    }

    if (MR::isStep(this, ::sSetUpTime)) {
        MR::zeroVelocity(mBoundSensor->mHost);
        mBoundSensor->receiveMessage(ACTMES_END_BALL_BIND, getSensor("body"));
        mBoundSensor = nullptr;
        MR::startSound(this, "SE_OJ_BALL_OPN_OPEN");
        MR::startSystemSE("SE_SY_TAMAKORO_GOAL");

        if (MR::isValidSwitchA(this)) {
            MR::onSwitchA(this);
        }

        setNerve(GET_NERVE(BallOpener, BallOpenerNrvWait));
    }
}

void BallOpener::bindHole() {
    f32 radius = mBoundSensor->mRadius;

    Vec3f previous(mBoundSensor->mPosition);
    Vec3f position(previous);
    mBindVelocity += mHoleGravity * ::sGravityAcc;

    BindResult result;
    BindSphere sphere(previous, radius);
    MR::bindSpereToCone(&result, mBindVelocity, sphere, *mCone);
    MR::updateBindPositionAndVelocity(&position, &mBindVelocity, result, ::sHoleRefrecRate);

    Vec3f lateralVelocity;
    lateralVelocity.killElement2(mBindVelocity, mHoleGravity);
    Vec3f lateralOffset;
    lateralOffset.killElement2(position - mPosition, mHoleGravity);

    f32 limit = ::sHoleRadius - radius;
    if (lateralVelocity.dot(lateralOffset) > 0.0f && lateralOffset.squared() > limit * limit) {
        f32 distance;
        MR::separateScalarAndDirection(&distance, &lateralOffset, lateralOffset);
        position -= lateralOffset * (distance - limit);

        Vec3f tangent = lateralVelocity - lateralOffset * lateralVelocity.dot(lateralOffset);
        if (tangent.isZero()) {
            tangent = mHoleGravity;
        } else {
            MR::normalizeOrZero(&tangent);
        }

        mBindVelocity -= lateralVelocity;
        mBindVelocity += tangent * lateralVelocity.length();
    }

    mBindVelocity *= MR::calcNerveValue(this, ::sForceSetStartTime, ::sForceSetEndTime, ::sBallToCenterFreq, ::sBallToCenterEndFreq);
    mBoundSensor->mHost->mVelocity.set(position - previous);

    if (position.squared(previous) < cNum_bindHole_f1 * cNum_bindHole_f2) {
        mBindVelocity.zero();
    }
}
