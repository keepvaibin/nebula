#include "Game/MapObj/ClipAreaDrop.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/ClipArea.hpp"
#include "Game/MapObj/ClipAreaShape.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 sExtendsEndTime = 15;
    const s32 sShrinkStartTime = NebulaFrameClock::stepsOfSeconds(1);
    const s32 sShrinkEndTime = NebulaFrameClock::stepsOfSeconds(4);
};  // namespace

namespace NrvClipAreaDrop {
    NEW_NERVE(ClipAreaDropNrvWait, ClipAreaDrop, Wait);
};  // namespace NrvClipAreaDrop

ClipAreaDrop::ClipAreaDrop(const char* pName) : ClipArea(pName), mShapeSphere(new ClipAreaShapeSphere()), mBaseSize(500.0f) {
    lumacore::setClass(this, lumacore::kClassId< ClipAreaDrop >);
    setShape(mShapeSphere);
}

void ClipAreaDrop::init(const TableIter& rIter) {
    initBaseMatrix(rIter);
    MR::connectToScene(this, MR::MovementType_ClippedMapParts, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_ClipArea);
    MR::invalidateClipping(this);
    initNerve(GET_NERVE(ClipAreaDrop, ClipAreaDropNrvWait));
    makeActorDead();
}

void ClipAreaDrop::setBaseSize(f32 baseSize) {
    mBaseSize = baseSize;
}

void ClipAreaDrop::appear() {
    LiveActor::appear();

    mShapeSphere->mRadius = 0.0f;

    setNerve(GET_NERVE(ClipAreaDrop, ClipAreaDropNrvWait));
}

void ClipAreaDrop::control() {
    mBaseMtx.identity();
    mBaseMtx.setTrans(mPosition);
}

void ClipAreaDrop::exeWait() {
    mShapeSphere->mRadius = MR::isLessStep(this, ::sExtendsEndTime)
                                ? MR::calcNerveEaseOutValue(this, ::sExtendsEndTime, 0.0f, mBaseSize)
                                : MR::calcNerveEaseInOutValue(this, ::sShrinkStartTime, ::sShrinkEndTime, mBaseSize, 0.0f);

    if (MR::isGreaterStep(this, ::sShrinkEndTime)) {
        kill();
    }
}
