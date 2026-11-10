#include "Game/MapObj/PalmIsland.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFloat_f1, "exeFloat.f1", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFloat_f2, "exeFloat.f2", 1.44f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFloat_f3, "exeFloat.f3", 1.5f);

namespace NrvPalmIsland {
    NEW_NERVE(PalmIslandNrvWait, PalmIsland, Wait);
    NEW_NERVE(PalmIslandNrvFloat, PalmIsland, Float);
};  // namespace NrvPalmIsland

PalmIsland::PalmIsland(const char* pName) : LiveActor(pName) {
    mFloatStartStep = 0;
    mRipplePos.x = 0.0f;
    mRipplePos.y = 0.0f;
    mRipplePos.z = 0.0f;
    mIsRising = 0;
}

void PalmIsland::init(const TableIter& rIter) {
    HitSensor* sensor;
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("PalmIsland", nullptr, false);
    MR::connectToSceneMapObj(this);
    initHitSensor(1);
    MR::addBodyMessageSensorMapObj(this);
    sensor = getSensor("body");
    MR::initCollisionParts(this, "PalmIsland", sensor, nullptr);
    initEffectKeeper(0, nullptr);
    initSound(2, false);
    MR::setClippingTypeSphereContainsModelBoundingBox(this, cNum_init_f1);
    mFloatStartStep = MR::getRandom(0, 60);
    initNerve(GET_NERVE(PalmIsland, PalmIslandNrvWait));
    makeActorAppeared();
}

inline void PalmIsland::exeWait() {
    if (MR::isStep(this, mFloatStartStep)) {
        setNerve(GET_NERVE(PalmIsland, PalmIslandNrvFloat));
    }
}

void PalmIsland::exeFloat() {
    if (MR::isFirstStep(this)) {
        mRipplePos = mPosition;
        MR::emitEffect(this, "Ripple");
        MR::setEffectHostSRT(this, "Ripple", &mRipplePos, nullptr, nullptr);
    }

    f32 value = MR::sinDegree(cNum_exeFloat_f1 + cNum_exeFloat_f2 * getNerveStep()) * cNum_exeFloat_f3;
    bool isRising = value > 0.0f;

    if (isRising != mIsRising) {
        MR::startSound(this, "SE_OJ_PALM_ISLAND_WAVE");
    }

    mIsRising = isRising;
    mVelocity.scale(value, *getGravity());
}
