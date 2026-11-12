#include "Game/MapObj/BigBubbleGenerator.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/BigBubbleHolder.hpp"
#include "Game/Ride/BigBubble.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sActiveDistance = 1500.0f;
    const f32 sNonActiveDistance = 2000.0f;
    const f32 sGenerateOffsetY = 60.0f;
};  // namespace

namespace NrvBigBubbleGenerator {
    NEW_NERVE(BigBubbleGeneratorNrvNonActive, BigBubbleGenerator, NonActive);
    NEW_NERVE(BigBubbleGeneratorNrvActive, BigBubbleGenerator, Active);
};  // namespace NrvBigBubbleGenerator

BigBubbleGenerator::BigBubbleGenerator(const char* pName, bool isObstruct)
    : LiveActor(pName), mSpawnTimer(-1), mMinSize(1.0f), mMaxSize(1.0f), mIsObstruct(isObstruct), mShape(-1), mLimitterID(-1),
      mAttachedBubble(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< BigBubbleGenerator >);
    MR::createBigBubbleHolder();
}

void BigBubbleGenerator::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("AirBubbleGenerator", nullptr, false);
    MR::connectToSceneMapObj(this);

    initHitSensor(1);
    MR::addBodyMessageSensorReceiver(this);

    MR::initCollisionParts(this, "AirBubbleGenerator", getSensor(nullptr), nullptr);
    initNerve(GET_NERVE(BigBubbleGenerator, BigBubbleGeneratorNrvNonActive));
    initFromPlacementParam(rIter);
    makeActorAppeared();
}

// Sizes are placed in percent.
void BigBubbleGenerator::initFromPlacementParam(const TableIter& rIter) {
    MR::getPlacementArg0NoInit(rIter, &mSpawnTimer);
    if (MR::getPlacementArg1NoInit(rIter, &mMinSize)) {
        mMinSize /= 100.0f;
    }
    if (MR::getPlacementArg2NoInit(rIter, &mMaxSize)) {
        mMaxSize /= 100.0f;
    }
    MR::getPlacementArg3WithInit(rIter, &mLimitterID);
    MR::getPlacementArg4WithInit(rIter, &mShape);

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::listenStageSwitchOnOffAppear(this, MR::Functor(this, &BigBubbleGenerator::onActive), MR::Functor(this, &BigBubbleGenerator::offActive));
    }
}

void BigBubbleGenerator::initAfterPlacement() {
    if (mLimitterID != -1) {
        MR::limitBigBubblePosition(&mPosition, 0.0f, mLimitterID);
    }
}

void BigBubbleGenerator::control() {
    if (mAttachedBubble != nullptr && (MR::isDead(mAttachedBubble) || mAttachedBubble->mHost != this)) {
        mAttachedBubble = nullptr;
    }
}

void BigBubbleGenerator::onActive() {
    if (isNerve(GET_NERVE(BigBubbleGenerator, BigBubbleGeneratorNrvNonActive))) {
        MR::invalidateClipping(this);
        setNerve(GET_NERVE(BigBubbleGenerator, BigBubbleGeneratorNrvActive));
    }
}

void BigBubbleGenerator::offActive() {
    if (isNerve(GET_NERVE(BigBubbleGenerator, BigBubbleGeneratorNrvActive))) {
        MR::validateClipping(this);
        setNerve(GET_NERVE(BigBubbleGenerator, BigBubbleGeneratorNrvNonActive));
    }
}

void BigBubbleGenerator::tryActive() {
    if (!MR::isValidSwitchAppear(this) && MR::isNearPlayerAnyTime(this, ::sActiveDistance)) {
        MR::invalidateClipping(this);
        setNerve(GET_NERVE(BigBubbleGenerator, BigBubbleGeneratorNrvActive));
    }
}

void BigBubbleGenerator::tryNonActive() {
    if (!MR::isValidSwitchAppear(this) && !MR::isNearPlayerAnyTime(this, ::sNonActiveDistance)) {
        MR::validateClipping(this);
        setNerve(GET_NERVE(BigBubbleGenerator, BigBubbleGeneratorNrvNonActive));
    }
}

void BigBubbleGenerator::exeNonActive() {
    tryActive();
}

// A negative timer keeps one bubble attached; otherwise one is sent every mSpawnTimer steps.
void BigBubbleGenerator::exeActive() {
    const bool isAttach = mSpawnTimer < 0;
    bool isSpawn;
    if (isAttach) {
        isSpawn = mAttachedBubble == nullptr;
    } else {
        isSpawn = (mSpawnTimer != 0 ? getNerveStep() % mSpawnTimer : getNerveStep()) == 0;
    }

    if (isSpawn) {
        Vec3f up;
        MR::calcActorAxisY(&up, this);
        Vec3f pos = mPosition + up * ::sGenerateOffsetY;
        MR::startBck(this, "Generate");
        BigBubble* pBubble = MR::generateBigBubble(this, pos, up, MR::getRandom(mMinSize, mMaxSize), isAttach, mIsObstruct, mShape, mLimitterID);

        if (isAttach) {
            mAttachedBubble = pBubble;
        }
    }

    tryNonActive();
}

NameObj* MR::createBigBubbleGenerator(const char* pName) {
    return new BigBubbleGenerator(pName, false);
}

NameObj* MR::createBigObstructBubbleGenerator(const char* pName) {
    return new BigBubbleGenerator(pName, true);
}
