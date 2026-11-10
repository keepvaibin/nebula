#include "Game/MapObj/RevolvingWay.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <nebula_math.h>
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEBULA_TUNABLE(f32, sDecayHeld, 0.98f);
    NEBULA_TUNABLE(f32, sDecay, 0.9f);
    NEBULA_TUNABLE(f32, sStrokeRate, 0.04f);
    NEBULA_TUNABLE(f32, sMomentMax, 0.15f);
};  // namespace

namespace NrvRevolvingWay {
    NEW_NERVE(RevolvingWayNrvWait, RevolvingWay, Wait);
};  // namespace NrvRevolvingWay

RevolvingWay::RevolvingWay(const char* pName) : LiveActor(pName), mRotateQuat(0.0f, 1.0f), mRotateMoment(0.0f, 0.0f, 0.0f), mRadius(500.0f) {
    lumacore::setClass(this, lumacore::kClassId< RevolvingWay >);
}

void RevolvingWay::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("RevolvingWay", nullptr, false);
    MR::connectToSceneMapObj(this);
    MR::makeQuatFromRotate(&mRotateQuat, this);
    initHitSensor(1);
    MR::addHitSensorMapObj(this, "body", 16, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initCollisionParts(this, "RevolvingWay", getSensor("body"), nullptr);
    MR::initStarPointerTarget(this, mRadius, Vec3f(0.0f, 0.0f, 0.0f));
    initNerve(GET_NERVE(RevolvingWay, RevolvingWayNrvWait));
    makeActorAppeared();
}

void RevolvingWay::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mRotateQuat);
}

void RevolvingWay::exeWait() {
    addAccelMoment();
    MR::rotateQuatMoment(&mRotateQuat, mRotateMoment);
    mRotateMoment *= MR::isHeldFire(PLAYER_1) ? ::sDecayHeld : ::sDecay;
}

void RevolvingWay::addAccelMoment() {
    Vec3f rotateMoment;
    if (MR::isStarPointerPointing(this, 0, true, "弱") && MR::isHeldFire(PLAYER_1) &&
        MR::calcStarPointerStrokeRotateMoment(&rotateMoment, mPosition, mRadius, 0)) {
        mRotateMoment += rotateMoment * ::sStrokeRate;
        f32 mag = mRotateMoment.length();
        if (mag > ::sMomentMax) {
            mRotateMoment *= (::sMomentMax / mag);
        }
    }
}
