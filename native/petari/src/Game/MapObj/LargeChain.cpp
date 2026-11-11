#include "Game/MapObj/LargeChain.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/LargeChainParts.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_createChainParts_f1, "createChainParts.f1", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createChainParts_f2, "createChainParts.f2", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createChainParts_f3, "createChainParts.f3", 200.0f);

namespace {
    NEBULA_TUNABLE(f32, sPartsLength, 200.0f);
    NEBULA_TUNABLE(s32, sBreakInterval, 5);
};  // namespace

namespace NrvLargeChain {
    NEW_NERVE(LargeChainNrvWait, LargeChain, Wait);
    NEW_NERVE(LargeChainNrvBreak, LargeChain, Break);
};  // namespace NrvLargeChain

LargeChain::LargeChain(const char* pName) : LiveActor(pName), mBottomFixParts(), mTopFixParts(), mChainArray(), mChainCount(1) {
    lumacore::setClass(this, lumacore::kClassId< LargeChain >);
}

void LargeChain::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    s32 chainLen = 0;
    MR::getPlacementArg0NoInit(rIter, &chainLen);
    mChainCount = static_cast< s32 >(chainLen / ::sPartsLength);

    // A chain has at least one link (the fixed parts and the break step read link 0).
    if (mChainCount < 1) {
        mChainCount = 1;
    }

    createChainParts();

    MR::connectToSceneMapObjMovement(this);

    if (MR::tryRegisterDemoCast(this, rIter)) {
        MR::registerDemoActionNerve(this, GET_NERVE(LargeChain, LargeChainNrvBreak), nullptr);
    }

    MR::useStageSwitchSleep(this, rIter);

    initNerve(GET_NERVE(LargeChain, LargeChainNrvWait));

    makeActorAppeared();
}

void LargeChain::makeActorAppeared() {
    for (s32 i = 0; i < mChainCount; i++) {
        mChainArray[i].makeActorAppeared();
    }

    mBottomFixParts->makeActorAppeared();
    mTopFixParts->makeActorAppeared();
    LiveActor::makeActorAppeared();
}

void LargeChain::makeActorDead() {
    for (s32 i = 0; i < mChainCount; i++) {
        mChainArray[i].makeActorDead();
    }

    mBottomFixParts->makeActorDead();
    mTopFixParts->makeActorDead();
    LiveActor::makeActorDead();
}

void LargeChain::exeWait() {
}

void LargeChain::exeBreak() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
        invalidateClippingAllChainParts();
    }

    if (getNerveStep() % ::sBreakInterval != 0) {
        return;
    }

    s32 step = getNerveStep() / ::sBreakInterval;

    if (step == 0) {
        mBottomFixParts->breakChainParts();
    } else if (static_cast< u32 >(step) == mChainCount - 1) {
        mTopFixParts->breakChainParts();
    }

    mChainArray[step].breakChainParts();

    if (static_cast< u32 >(step) == mChainCount - 1) {
        kill();
    }
}

namespace {
    inline LargeChainParts* createChainArray(u32 count) {
        return new LargeChainParts[count];
    }
};  // namespace

void LargeChain::createChainParts() {
    mBottomFixParts = new LargeChainParts("固定鎖 下");
    mTopFixParts = new LargeChainParts("固定鎖 上");
    mChainArray = ::createChainArray(mChainCount);

    for (u32 i = 0; i < mChainCount; i++) {
        Vec3f pos(mPosition);
        Vec3f trans(0.0f, static_cast< f32 >(i), 0.0f);
        trans.y *= ::sPartsLength;
        pos.add(trans);

        mChainArray[i].initChainParts(&pos, &mRotation, &mScale, false);

        if (i == 0) {
            mBottomFixParts->initChainParts(&pos, &mRotation, &mScale, true);
        } else if (i == mChainCount - 1) {
            Vec3f rot(mRotation);
            rot.y += cNum_createChainParts_f1;
            rot.x += cNum_createChainParts_f2;
            pos.y += cNum_createChainParts_f3;
            mTopFixParts->initChainParts(&pos, &rot, &mScale, true);
        }
    }
}

void LargeChain::invalidateClippingAllChainParts() {
    for (s32 i = 0; i < mChainCount; i++) {
        MR::invalidateClipping(&mChainArray[i]);
    }
}
