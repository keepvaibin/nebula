#include "Game/Boss/KoopaFigureBall.hpp"
#include "Game/Boss/Koopa.hpp"
#include "Game/Boss/KoopaFunction.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 180.0f);

namespace {
    NEBULA_TUNABLE(f32, sWallJumpSpeed, 15.0f);
};  // namespace

KoopaFigureBall::KoopaFigureBall(const char* pName, Koopa* pKoopa, f32 myFloat, const MR::ActorMoveParam* pMoveParam)
    : LiveActor(pName), mKoopa(pKoopa), mFront(0.0f, 0.0f, 1.0f), mRadius(myFloat), mAngle(), mMoveParam(pMoveParam) {
    lumacore::setClass(this, lumacore::kClassId< KoopaFigureBall >);
    mBaseMtx.identity();
}

void KoopaFigureBall::init(const TableIter& /*rIter*/) {
    initBinder(mRadius, 0.0f, 0);

    MR::onCalcGravity(this);

    MR::invalidateClipping(this);

    makeActorDead();
}

void KoopaFigureBall::makeActorAppeared() {
    mPosition.set(mKoopa->mPosition);
    mVelocity.set(mKoopa->mVelocity);
    mFront.set(KoopaFunction::getKoopaFront(mKoopa));

    MR::calcGravity(this);

    mBaseMtx.identity();

    MR::makeMtxFrontUpPos(&mBaseMtx, mFront, -mGravity, mPosition);

    mAngle = 0.0f;

    LiveActor::makeActorAppeared();
}

void KoopaFigureBall::makeActorDead() {
    KoopaFunction::getKoopaFrontPtr(mKoopa)->set(mFront);
    LiveActor::makeActorDead();
}

void KoopaFigureBall::control() {
    MR::moveAndTurnToPlayer(this, &mFront, *mMoveParam);

    if (MR::isBindedWall(this)) {
        MR::addVelocityJump(this, ::sWallJumpSpeed);
    }

    mAngle += (mVelocity.dot(mFront) * cNum_control_f1) / (mRadius * MR::pi());
    MR::makeMtxUpFrontPos(&mBaseMtx, -mGravity, mFront, mPosition + mGravity * mRadius);

    Vec3f yDir;
    Vec3f newTrans;
    mBaseMtx.getYDir(yDir);
    mBaseMtx.getTrans(newTrans);
    newTrans += yDir * mRadius;
    mBaseMtx.setTrans(newTrans);

    Pos3f rotMtx;
    rotMtx.makeRotate(Vec3f(1.0f, 0.0f, 0.0f), MR::toRadian(mAngle));
    mBaseMtx.concat(mBaseMtx, rotMtx);
}

MtxPtr KoopaFigureBall::getBaseMtx() const {
    return const_cast< MtxPtr >(mBaseMtx.mMtx);
}
