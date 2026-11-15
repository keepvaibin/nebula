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
#include "Game/System/LumaCore.hpp"

namespace NrvPalmIsland {
    NEW_NERVE(PalmIslandNrvWait, PalmIsland, Wait);
    NEW_NERVE(PalmIslandNrvFloat, PalmIsland, Float);
};  // namespace NrvPalmIsland

PalmIsland::PalmIsland(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< PalmIsland >);
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
    MR::setClippingTypeSphereContainsModelBoundingBox(this, 100.0f);
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

    f32 value = MR::sinDegree(90.0f + 1.44f * getNerveStep()) * 1.5f;
    bool isRising = value > 0.0f;

    if (isRising != mIsRising) {
        MR::startSound(this, "SE_OJ_PALM_ISLAND_WAVE");
    }

    mIsRising = isRising;
    mVelocity.scale(value, *getGravity());
}
