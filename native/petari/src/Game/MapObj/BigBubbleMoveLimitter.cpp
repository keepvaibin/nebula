#include "Game/MapObj/BigBubbleMoveLimitter.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/BigBubbleHolder.hpp"
#include "Game/Util.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 1000.0f);

BigBubbleMoveLimitter::BigBubbleMoveLimitter(const char* pName, s32 limitterType)
    : NameObj(pName), mID(-1), mPosition(0, 0, 0), mAxis(0, 1, 0), mSide(1, 0, 0), mUp(0, 1, 0), mFront(0, 0, 1),
      mCylinderRadius(500.0f), mCylinderHeight(500.0f), mPlaneWidth(500.0f), mPlaneHeight(500.0f), mLimitMarginType(LimitterMarginType_Center),
      mType(limitterType) {
}

void BigBubbleMoveLimitter::init(const TableIter& rIter) {
    MR::getPlacementArg0NoInit(rIter, &mID);
    MR::getPlacementArg1WithInit(rIter, &mLimitMarginType);

    Pos3f mtx;
    MR::getPlacementMatrixFromRT(rIter, &mtx);
    mtx.getTrans(mPosition);
    mtx.getYDir(mAxis);
    mtx.getXYZDir(mSide, mUp, mFront);

    Vec3f scale;
    MR::getPlacementScale(rIter, &scale);
    mCylinderRadius = scale.x * cNum_init_f1;
    mCylinderHeight = scale.y * cNum_init_f2;
    mPlaneWidth = scale.x * cNum_init_f3;
    mPlaneHeight = scale.y * cNum_init_f4;

    MR::createBigBubbleHolder();
    MR::addBigBubbleMoveLimitter(this);
}

bool BigBubbleMoveLimitter::limitPosition(Vec3f* pPos, f32 offset) const {
    bool limitted = false;

    offset = getLimitterOffset(offset);

    if (mType == LimitterType_Cylinder) {
        Vec3f ortho;
        f32 proj = MR::vecKillElement(*pPos - mPosition, mAxis, &ortho);
        MR::normalizeOrZero(&ortho);

        if (proj < 0.0f) {
            proj = 0.0f;
            limitted = true;
        }

        if (proj > mCylinderHeight) {
            proj = mCylinderHeight;
            limitted = true;
        }

        pPos->set(mPosition + mAxis * proj + ortho * (mCylinderRadius + offset));
    } else {
        Vec3f pos;
        MR::vecKillElement(*pPos - mPosition, mFront, &pos);
        pos.add(mFront * offset);

        f32 planeX = pos.dot(mSide);
        if (planeX < -mPlaneWidth) {
            pos.add(mSide * (-mPlaneWidth - planeX));
            limitted = true;
        } else if (planeX > mPlaneWidth) {
            pos.add(mSide * (mPlaneWidth - planeX));
            limitted = true;
        }

        f32 planeY = pos.dot(mUp);
        if (planeY < 0.0f) {
            pos.add(mUp * -planeY);
            limitted = true;
        } else if (planeY > mPlaneHeight) {
            pos.add(mUp * (mPlaneHeight - planeY));
            limitted = true;
        }

        pPos->set(mPosition + pos);
    }

    return limitted;
}

bool BigBubbleMoveLimitter::limitVelocity(Vec3f* pPos, const Vec3f& rVel, f32 offset) const {
    Vec3f nextPos;
    nextPos.set(rVel + *pPos);
    bool limitted = limitPosition(&nextPos, offset);
    pPos->set(nextPos - rVel);
    return limitted;
}

NameObj* MR::createBigBubbleMoveLimitterCylinder(const char* pName) {
    return new BigBubbleMoveLimitter(pName, BigBubbleMoveLimitter::LimitterType_Cylinder);
}

NameObj* MR::createBigBubbleMoveLimitterPlane(const char* pName) {
    return new BigBubbleMoveLimitter(pName, BigBubbleMoveLimitter::LimitterType_Plane);
}
