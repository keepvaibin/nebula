#include "Game/MapObj/FloaterFloatingForce.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/MapPartsUtil.hpp"
#include "Game/Util/MathUtil.hpp"

FloaterFloatingForce::FloaterFloatingForce(LiveActor* pHost, const char* pName) : MapPartsFunction(pHost, "浮力"), mObjName(pName), mBasePos(gZeroVec) {
    mUp = Vec3f(0.0f, 1.0f, 0.0f);
    mMaxSinkDepth = 0.0f;
}

void FloaterFloatingForce::init(const TableIter& rIter) {
    s32 maxSinkDepth = 0;
    MR::getMapPartsArgMoveConditionType(&maxSinkDepth, rIter);
    mMaxSinkDepth = maxSinkDepth;
}

void FloaterFloatingForce::start() {
    mBasePos.set(mHost->mPosition);

    Pos3f mtx;
    mtx.set(mHost->getBaseMtx());
    mtx.getYDir(mUp);
    MR::normalize(&mUp);
}
